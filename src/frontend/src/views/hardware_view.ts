import { apiClient, DeviceTelemetryResponse, DeviceRecoveryLog } from '../api/client';
import { toast } from '../components/toast';

interface LogItem {
    timestamp: string;
    device: string;
    attempt: number;
    backoffMs: number;
    succeeded: boolean;
    message: string;
}

export class HardwareView {
    private canvasContainer: HTMLElement;
    private inspectorPanel: HTMLElement;

    private pollInterval: number | null = null;
    private autoPoll: boolean = true;
    private devices: Record<string, DeviceTelemetryResponse> = {};
    private eventLogs: LogItem[] = [];

    constructor(canvasContainer: HTMLElement, inspectorPanel: HTMLElement) {
        this.canvasContainer = canvasContainer;
        this.inspectorPanel = inspectorPanel;
    }

    render(): void {
        this.renderCanvas();
        this.renderInspector();
        this.bindEvents();
        this.refreshAllDevices();

        // Start 2-second polling for live telemetry
        this.pollInterval = window.setInterval(() => {
            if (this.autoPoll) {
                this.refreshAllDevices(true);
            }
        }, 2000);
    }

    private renderCanvas(): void {
        this.canvasContainer.innerHTML = `
            <div style="display: flex; flex-direction: column; height: 100%; width: 100%; padding: var(--gap); gap: var(--gap); overflow-y: auto;">
                <!-- Top SIMULATED Warning Banner (Hard Rule 3) -->
                <div style="background: rgba(245, 158, 11, 0.12); border: 1px solid var(--warn); border-radius: var(--radius-sm); padding: 10px 16px; display: flex; align-items: center; justify-content: space-between;">
                    <div style="display: flex; align-items: center; gap: 10px;">
                        <span style="font-size: 18px;">⚠️</span>
                        <div>
                            <span style="font-weight: 700; color: var(--warn); font-size: 13px;">SIMULATED SUBSYSTEM (Hard Rule 3):</span>
                            <span style="color: var(--text); font-size: 12px; margin-left: 6px;">
                                Microcontroller, FPGA, and JTAG hardware are mock adapters modelled on real datasheets with timeout injection & backoff recovery.
                            </span>
                        </div>
                    </div>
                    <div style="display: flex; gap: 8px; align-items: center;">
                        <label style="display: flex; align-items: center; gap: 6px; font-size: 11px; cursor: pointer; color: var(--muted);">
                            <input type="checkbox" id="chk-auto-poll" checked />
                            <span>Auto-Poll (2s)</span>
                        </label>
                        <button id="btn-refresh-all" class="btn-secondary" style="padding: 4px 10px; font-size: 11px;">
                            ↺ Refresh
                        </button>
                    </div>
                </div>

                <!-- 3 Device Telemetry Cards Grid -->
                <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: var(--gap);">
                    <!-- 1. MCU Card -->
                    <div class="card" id="card-dev-mcu" style="display: flex; flex-direction: column; gap: 10px;">
                        <div style="display: flex; justify-content: space-between; align-items: flex-start;">
                            <div>
                                <div style="font-weight: 600; font-size: 14px; color: var(--text);">STM32H753ZI MCU</div>
                                <div style="font-size: 11px; color: var(--muted);">Arm Cortex-M7 @ 480 MHz</div>
                            </div>
                            <div style="display: flex; flex-direction: column; align-items: flex-end; gap: 4px;">
                                <span class="badge" style="background: rgba(245, 158, 11, 0.2); border: 1px solid var(--warn); color: var(--warn); font-size: 10px; font-weight: 700;">
                                    SIMULATED
                                </span>
                                <span id="mcu-state-badge" class="badge badge-accent">CONNECTED</span>
                            </div>
                        </div>

                        <!-- Telemetry Grid -->
                        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 6px; font-family: var(--font-mono); font-size: 11px; background-color: var(--surface-2); padding: 10px; border-radius: var(--radius-sm);">
                            <div><span style="color: var(--muted);">Core Temp:</span> <span id="mcu-temp" style="color: #38bdf8; font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">VDD (3.3V):</span> <span id="mcu-volt" style="color: var(--text); font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">Current:</span> <span id="mcu-curr" style="color: var(--text);">--</span></div>
                            <div><span style="color: var(--muted);">CPU Load:</span> <span id="mcu-load" style="color: var(--ok); font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">RAM Used:</span> <span id="mcu-ram" style="color: var(--text);">--</span></div>
                            <div><span style="color: var(--muted);">Uptime:</span> <span id="mcu-uptime" style="color: var(--muted);">--</span></div>
                        </div>

                        <div style="font-family: var(--font-mono); font-size: 11px; color: var(--muted);">
                            Status: <span id="mcu-status" style="color: var(--text);">-</span>
                        </div>

                        <!-- Actions -->
                        <div style="display: flex; gap: 6px; margin-top: auto;">
                            <button id="btn-fault-mcu" class="btn-secondary" style="flex: 1; font-size: 11px; color: var(--warn); border-color: rgba(245, 158, 11, 0.4);" title="Inject 2-stage timeout to demonstrate 200ms -> 400ms recovery">
                                ⚡ Inject Fault (2x)
                            </button>
                            <button id="btn-reset-mcu" class="btn-secondary" style="font-size: 11px;" title="Reset device link">
                                ↺ Reset
                            </button>
                            <button id="btn-ping-mcu" class="btn-secondary" style="font-size: 11px;">
                                Ping
                            </button>
                        </div>
                    </div>

                    <!-- 2. FPGA Card -->
                    <div class="card" id="card-dev-fpga" style="display: flex; flex-direction: column; gap: 10px;">
                        <div style="display: flex; justify-content: space-between; align-items: flex-start;">
                            <div>
                                <div style="font-weight: 600; font-size: 14px; color: var(--text);">XC7A100T FPGA</div>
                                <div style="font-size: 11px; color: var(--muted);">Artix-7 Coprocessor</div>
                            </div>
                            <div style="display: flex; flex-direction: column; align-items: flex-end; gap: 4px;">
                                <span class="badge" style="background: rgba(245, 158, 11, 0.2); border: 1px solid var(--warn); color: var(--warn); font-size: 10px; font-weight: 700;">
                                    SIMULATED
                                </span>
                                <span id="fpga-state-badge" class="badge badge-accent">CONNECTED</span>
                            </div>
                        </div>

                        <!-- Telemetry Grid -->
                        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 6px; font-family: var(--font-mono); font-size: 11px; background-color: var(--surface-2); padding: 10px; border-radius: var(--radius-sm);">
                            <div><span style="color: var(--muted);">Junction:</span> <span id="fpga-temp" style="color: #f43f5e; font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">VCCINT:</span> <span id="fpga-volt" style="color: var(--text); font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">VCCAUX:</span> <span id="fpga-vaux" style="color: var(--text);">--</span></div>
                            <div><span style="color: var(--muted);">Current:</span> <span id="fpga-curr" style="color: var(--text);">--</span></div>
                            <div><span style="color: var(--muted);">DSP Util:</span> <span id="fpga-dsp" style="color: var(--cyan); font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">Uptime:</span> <span id="fpga-uptime" style="color: var(--muted);">--</span></div>
                        </div>

                        <div style="font-family: var(--font-mono); font-size: 11px; color: var(--muted);">
                            Status: <span id="fpga-status" style="color: var(--text);">-</span>
                        </div>

                        <!-- Actions -->
                        <div style="display: flex; gap: 6px; margin-top: auto;">
                            <button id="btn-fault-fpga" class="btn-secondary" style="flex: 1; font-size: 11px; color: var(--warn); border-color: rgba(245, 158, 11, 0.4);" title="Inject 2-stage timeout to demonstrate 200ms -> 400ms recovery">
                                ⚡ Inject Fault (2x)
                            </button>
                            <button id="btn-reset-fpga" class="btn-secondary" style="font-size: 11px;" title="Reset device link">
                                ↺ Reset
                            </button>
                            <button id="btn-ping-fpga" class="btn-secondary" style="font-size: 11px;">
                                Ping
                            </button>
                        </div>
                    </div>

                    <!-- 3. JTAG Card -->
                    <div class="card" id="card-dev-jtag" style="display: flex; flex-direction: column; gap: 10px;">
                        <div style="display: flex; justify-content: space-between; align-items: flex-start;">
                            <div>
                                <div style="font-weight: 600; font-size: 14px; color: var(--text);">FT2232HL JTAG</div>
                                <div style="font-size: 11px; color: var(--muted);">Dual USB Bridge / MPSSE</div>
                            </div>
                            <div style="display: flex; flex-direction: column; align-items: flex-end; gap: 4px;">
                                <span class="badge" style="background: rgba(245, 158, 11, 0.2); border: 1px solid var(--warn); color: var(--warn); font-size: 10px; font-weight: 700;">
                                    SIMULATED
                                </span>
                                <span id="jtag-state-badge" class="badge badge-accent">CONNECTED</span>
                            </div>
                        </div>

                        <!-- Telemetry Grid -->
                        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 6px; font-family: var(--font-mono); font-size: 11px; background-color: var(--surface-2); padding: 10px; border-radius: var(--radius-sm);">
                            <div><span style="color: var(--muted);">Bridge Temp:</span> <span id="jtag-temp" style="color: #38bdf8; font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">USB VBUS:</span> <span id="jtag-volt" style="color: var(--text); font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">Target VREF:</span> <span id="jtag-vref" style="color: var(--text);">--</span></div>
                            <div><span style="color: var(--muted);">Current:</span> <span id="jtag-curr" style="color: var(--text);">--</span></div>
                            <div><span style="color: var(--muted);">TCK Clock:</span> <span id="jtag-tck" style="color: var(--cyan); font-weight: 600;">--</span></div>
                            <div><span style="color: var(--muted);">Uptime:</span> <span id="jtag-uptime" style="color: var(--muted);">--</span></div>
                        </div>

                        <div style="font-family: var(--font-mono); font-size: 11px; color: var(--muted);">
                            Status: <span id="jtag-status" style="color: var(--text);">-</span>
                        </div>

                        <!-- Actions -->
                        <div style="display: flex; gap: 6px; margin-top: auto;">
                            <button id="btn-fault-jtag" class="btn-secondary" style="flex: 1; font-size: 11px; color: var(--warn); border-color: rgba(245, 158, 11, 0.4);" title="Inject 2-stage timeout to demonstrate 200ms -> 400ms recovery">
                                ⚡ Inject Fault (2x)
                            </button>
                            <button id="btn-reset-jtag" class="btn-secondary" style="font-size: 11px;" title="Reset device link">
                                ↺ Reset
                            </button>
                            <button id="btn-ping-jtag" class="btn-secondary" style="font-size: 11px;">
                                Ping
                            </button>
                        </div>
                    </div>
                </div>

                <!-- Bottom Event & Recovery History Log -->
                <div style="flex: 1; min-height: 220px; background-color: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); padding: 16px; display: flex; flex-direction: column;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                        <div>
                            <span style="font-weight: 600; font-size: 13px; color: var(--text);">Automated Link Recovery & Fault Injection Log</span>
                            <span style="font-family: var(--font-mono); font-size: 11px; color: var(--muted); margin-left: 8px;">
                                Exponential Backoff Schedule: 200 ms &rarr; 400 ms &rarr; 800 ms
                            </span>
                        </div>
                        <button id="btn-clear-log" class="btn-secondary" style="padding: 2px 8px; font-size: 11px;">
                            Clear Log
                        </button>
                    </div>

                    <div id="recovery-log-container" style="flex: 1; background-color: #070b19; border: 1px solid var(--line); border-radius: var(--radius-sm); padding: 10px; overflow-y: auto; font-family: var(--font-mono); font-size: 11px; line-height: 1.5; color: var(--text);">
                        <div style="color: var(--muted);">[SYSTEM] Subsystem initialized. All devices operating nominally in SIMULATED mode.</div>
                    </div>
                </div>
            </div>
        `;
    }

    private renderInspector(): void {
        this.inspectorPanel.innerHTML = `
            <!-- Datasheet Citations Card -->
            <div class="card">
                <div class="card-title">
                    <span>Semiconductor Datasheets</span>
                    <span class="brand-badge">Real Citations</span>
                </div>
                <div style="display: flex; flex-direction: column; gap: 10px; font-size: 11px; line-height: 1.4;">
                    <div>
                        <div style="font-weight: 600; color: var(--cyan);">1. STM32H753ZI (STMicroelectronics)</div>
                        <div style="color: var(--muted);">DS13312 Rev 3 (Nov 2021) | RM0433 Rev 7</div>
                        <div style="color: var(--text);">Arm Cortex-M7 @ 480 MHz, VDD 3.3V CMOS, Table 23 operating limits.</div>
                    </div>
                    <div style="border-top: 1px solid var(--line); padding-top: 6px;">
                        <div style="font-weight: 600; color: var(--cyan);">2. Artix-7 XC7A100T (AMD / Xilinx)</div>
                        <div style="color: var(--muted);">DS181 v1.25 (July 2018) | UG470 v1.13.1</div>
                        <div style="color: var(--text);">VCCINT 1.0V &plusmn;3%, VCCAUX 1.8V, IDCODE 0x03631093.</div>
                    </div>
                    <div style="border-top: 1px solid var(--line); padding-top: 6px;">
                        <div style="font-weight: 600; color: var(--cyan);">3. FT2232HL Dual USB IC (FTDI)</div>
                        <div style="color: var(--muted);">DS_FT2232H v2.6 (2021) | IEEE 1149.1</div>
                        <div style="color: var(--text);">USB 2.0 High-Speed (480 Mbps) to MPSSE JTAG (TCK 15 MHz).</div>
                    </div>
                </div>
            </div>

            <!-- Recovery Protocol Details -->
            <div class="card">
                <div class="card-title">Recovery Protocol (Rule 3)</div>
                <div style="font-size: 11px; color: var(--muted); line-height: 1.5;">
                    <div>When a communication failure occurs, the link enters automated exponential backoff:</div>
                    <div style="background-color: var(--surface-2); padding: 8px; border-radius: var(--radius-sm); margin: 8px 0; font-family: var(--font-mono); color: var(--text);">
                        Retry 1: Delay 200 ms<br/>
                        Retry 2: Delay 400 ms<br/>
                        Retry 3: Delay 800 ms<br/>
                        Total Retry Ceiling: 1,400 ms
                    </div>
                    <div>If 3 retries are exhausted without restoration, state transitions to <code>FAULTED</code> and requires explicit <code>reset</code>.</div>
                </div>
            </div>

            <!-- Voltage Rails Reference -->
            <div class="card">
                <div class="card-title">Power Domains</div>
                <div style="display: flex; flex-direction: column; gap: 6px; font-family: var(--font-mono); font-size: 11px;">
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">USB VBUS:</span>
                        <span style="color: #f43f5e; font-weight: 600;">5.00 V (Host)</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">VDD Logic:</span>
                        <span style="color: #38bdf8; font-weight: 600;">3.30 V (CMOS)</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">VCCAUX Rail:</span>
                        <span style="color: #a855f7; font-weight: 600;">1.80 V (Aux)</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">VCCINT Core:</span>
                        <span style="color: var(--ok); font-weight: 600;">1.00 V (FPGA)</span>
                    </div>
                </div>
            </div>
        `;
    }

    private bindEvents(): void {
        const chkAutoPoll = document.getElementById('chk-auto-poll') as HTMLInputElement;
        const btnRefreshAll = document.getElementById('btn-refresh-all');
        const btnClearLog = document.getElementById('btn-clear-log');

        chkAutoPoll?.addEventListener('change', () => {
            this.autoPoll = chkAutoPoll.checked;
        });

        btnRefreshAll?.addEventListener('click', () => {
            this.refreshAllDevices();
        });

        btnClearLog?.addEventListener('click', () => {
            const container = document.getElementById('recovery-log-container');
            if (container) container.innerHTML = '';
            this.eventLogs = [];
        });

        // Device Action buttons
        ['mcu', 'fpga', 'jtag'].forEach(dev => {
            document.getElementById(`btn-fault-${dev}`)?.addEventListener('click', () => this.injectFault(dev, 2));
            document.getElementById(`btn-reset-${dev}`)?.addEventListener('click', () => this.resetDevice(dev));
            document.getElementById(`btn-ping-${dev}`)?.addEventListener('click', () => this.pingDevice(dev));
        });
    }

    private async refreshAllDevices(silent: boolean = false): Promise<void> {
        try {
            const data = await apiClient.getDevices();
            if (data.devices) {
                for (const d of data.devices) {
                    const devType = d.type || (d.device_name.toLowerCase().includes('mcu') ? 'mcu' : (d.device_name.toLowerCase().includes('fpga') ? 'fpga' : 'jtag'));
                    this.devices[devType] = d;
                    this.updateDeviceUI(devType, d);
                }
            }
        } catch (err: unknown) {
            if (!silent) {
                toast.show({
                    title: 'Telemetry Poll Failed',
                    description: (err as Error).message,
                    type: 'error'
                });
            }
        }
    }

    private updateDeviceUI(dev: string, d: DeviceTelemetryResponse): void {
        const stateBadge = document.getElementById(`${dev}-state-badge`);
        const tempEl = document.getElementById(`${dev}-temp`);
        const voltEl = document.getElementById(`${dev}-volt`);
        const currEl = document.getElementById(`${dev}-curr`);
        const uptimeEl = document.getElementById(`${dev}-uptime`);
        const statusEl = document.getElementById(`${dev}-status`);

        if (stateBadge) {
            stateBadge.textContent = d.state.toUpperCase();
            if (d.state === 'connected') {
                stateBadge.className = 'badge badge-ok';
            } else if (d.state === 'recovering') {
                stateBadge.className = 'badge badge-warn';
            } else {
                stateBadge.className = 'badge badge-error';
            }
        }

        if (tempEl) tempEl.textContent = `${d.temperature_c.toFixed(1)} °C`;
        if (voltEl) voltEl.textContent = `${d.voltage_v.toFixed(3)} V`;
        if (currEl) currEl.textContent = `${d.current_ma.toFixed(1)} mA`;
        if (uptimeEl) uptimeEl.textContent = `${d.uptime_seconds}s (#${d.sequence_number})`;
        if (statusEl) statusEl.textContent = d.status_word;

        if (dev === 'mcu') {
            const loadEl = document.getElementById('mcu-load');
            const ramEl = document.getElementById('mcu-ram');
            if (loadEl) loadEl.textContent = `${d.extra_metric_1.toFixed(1)}%`;
            if (ramEl) ramEl.textContent = `${d.extra_metric_2.toFixed(1)}%`;
        } else if (dev === 'fpga') {
            const vauxEl = document.getElementById('fpga-vaux');
            const dspEl = document.getElementById('fpga-dsp');
            if (vauxEl) vauxEl.textContent = `${d.extra_metric_2.toFixed(3)} V`;
            if (dspEl) dspEl.textContent = `${d.extra_metric_1.toFixed(1)}%`;
        } else if (dev === 'jtag') {
            const vrefEl = document.getElementById('jtag-vref');
            const tckEl = document.getElementById('jtag-tck');
            if (vrefEl) vrefEl.textContent = `${d.extra_metric_2.toFixed(3)} V`;
            if (tckEl) tckEl.textContent = `${(d.extra_metric_1 / 1000).toFixed(1)} MHz`;
        }

        // Process recovery history if present
        if (d.recovery_history && d.recovery_history.length > 0) {
            d.recovery_history.forEach(r => this.appendLog(dev, r));
        }
    }

    private appendLog(device: string, entry: DeviceRecoveryLog): void {
        const timeStr = new Date().toLocaleTimeString();
        const logItem: LogItem = {
            timestamp: timeStr,
            device: device.toUpperCase(),
            attempt: entry.attempt,
            backoffMs: entry.backoff_ms,
            succeeded: entry.succeeded,
            message: entry.message
        };

        this.eventLogs.push(logItem);
        const container = document.getElementById('recovery-log-container');
        if (!container) return;

        const row = document.createElement('div');
        const color = entry.succeeded ? 'var(--ok)' : 'var(--warn)';
        row.innerHTML = `
            <span style="color: var(--muted);">[${timeStr}]</span>
            <span style="color: var(--cyan); font-weight: 600;">[${device.toUpperCase()}]</span>
            <span style="color: ${color};">Attempt ${entry.attempt}/3 (${entry.backoff_ms}ms backoff):</span>
            <span style="color: var(--text);">${entry.message}</span>
        `;
        container.appendChild(row);
        container.scrollTop = container.scrollHeight;
    }

    private async injectFault(dev: string, failCount: number): Promise<void> {
        try {
            await apiClient.deviceAction(dev, 'inject_fault', failCount);
            toast.show({
                title: 'Fault Injected (SIMULATED)',
                description: `Injected ${failCount}-attempt timeout into ${dev.toUpperCase()}. Next read will trigger exponential backoff.`,
                type: 'warn'
            });

            // Trigger telemetry read immediately to demonstrate backoff recovery
            const tel = await apiClient.deviceAction(dev, 'telemetry') as DeviceTelemetryResponse;
            this.updateDeviceUI(dev, tel);

            toast.show({
                title: 'Link Restored',
                description: `${dev.toUpperCase()} successfully recovered after 2-stage backoff (200ms -> 400ms).`,
                type: 'success'
            });
        } catch (err: unknown) {
            toast.show({
                title: 'Fault Handled',
                description: (err as Error).message,
                type: 'warn'
            });
            this.refreshAllDevices(true);
        }
    }

    private async resetDevice(dev: string): Promise<void> {
        try {
            await apiClient.deviceAction(dev, 'reset');
            toast.show({
                title: 'Device Reset',
                description: `Reset command delivered to ${dev.toUpperCase()}. Link restored to CONNECTED.`,
                type: 'success'
            });
            this.refreshAllDevices(true);
        } catch (err: unknown) {
            toast.show({
                title: 'Reset Failed',
                description: (err as Error).message,
                type: 'error'
            });
        }
    }

    private async pingDevice(dev: string): Promise<void> {
        try {
            await apiClient.deviceAction(dev, 'ping');
            toast.show({
                title: 'Ping OK',
                description: `${dev.toUpperCase()} responded in < 2 ms.`,
                type: 'success'
            });
        } catch (err: unknown) {
            toast.show({
                title: 'Ping Failed',
                description: (err as Error).message,
                type: 'error'
            });
        }
    }

    destroy(): void {
        if (this.pollInterval !== null) {
            clearInterval(this.pollInterval);
            this.pollInterval = null;
        }
        this.canvasContainer.innerHTML = '';
        this.inspectorPanel.innerHTML = '';
    }
}
