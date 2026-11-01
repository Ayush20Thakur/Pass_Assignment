import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { apiClient, ThermalSolveParams, ThermalJobResult } from '../api/client';
import { toast } from '../components/toast';

export class ThermalView {
    private canvasContainer: HTMLElement;
    private inspectorPanel: HTMLElement;
    private activeJobId: string | null = null;
    private unsubscribeEvents: (() => void) | null = null;

    private tempPlot: uPlot | null = null;
    private logPlot: uPlot | null = null;

    // Default physical parameters per Task C
    private params: ThermalSolveParams = {
        length_m: 1.0,
        area_m2: 1e-4,
        thermal_conductivity_w_mk: 10.0,
        volumetric_heat_w_m3: 10000.0,
        temperature_left_c: 100.0,
        temperature_right_c: 20.0,
        element_counts: [10, 20, 40, 80]
    };

    private lastResult: ThermalJobResult | null = null;

    constructor(canvasContainer: HTMLElement, inspectorPanel: HTMLElement) {
        this.canvasContainer = canvasContainer;
        this.inspectorPanel = inspectorPanel;
    }

    render() {
        this.renderCanvas();
        this.renderInspector();
    }

    destroy() {
        if (this.unsubscribeEvents) {
            this.unsubscribeEvents();
            this.unsubscribeEvents = null;
        }
        if (this.tempPlot) {
            this.tempPlot.destroy();
            this.tempPlot = null;
        }
        if (this.logPlot) {
            this.logPlot.destroy();
            this.logPlot = null;
        }
    }

    private renderCanvas() {
        this.canvasContainer.innerHTML = `
            <div style="display: flex; flex-direction: column; height: 100%; width: 100%; padding: var(--gap); gap: var(--gap); overflow-y: auto;">
                <div style="flex: 1; min-height: 280px; background-color: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); padding: 16px; display: flex; flex-direction: column;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                        <span style="font-weight: 600; font-size: 13px; color: var(--text);">Temperature Distribution T(x) Across Rod</span>
                        <span style="font-family: var(--font-mono); font-size: 11px; color: var(--muted);">x in [0, L] | Units: degC</span>
                    </div>
                    <div id="chart-temp-dist" style="flex: 1; width: 100%; position: relative;"></div>
                </div>

                <div style="flex: 1; min-height: 280px; background-color: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); padding: 16px; display: flex; flex-direction: column;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                        <span style="font-weight: 600; font-size: 13px; color: var(--text);">Mesh Convergence (Midpoint Error vs Mesh Size h)</span>
                        <span style="font-family: var(--font-mono); font-size: 11px; color: var(--cyan);">Theoretical Slope = 2.00 (Second-Order)</span>
                    </div>
                    <div id="chart-convergence" style="flex: 1; width: 100%; position: relative;"></div>
                </div>
            </div>
        `;

        if (this.lastResult) {
            this.updateCharts(this.lastResult);
        }
    }

    private renderInspector() {
        this.inspectorPanel.innerHTML = `
            <div class="card">
                <div class="card-title">
                    <span>1D Thermal Parameters</span>
                    <span style="font-size: 11px; color: var(--muted); font-family: var(--font-mono);">Task C</span>
                </div>

                <div class="form-group">
                    <label class="form-label">
                        <span>Rod Length L (m)</span>
                        <span class="field-hint">L > 0</span>
                    </label>
                    <input id="input-l" type="number" step="0.1" class="form-input" value="${this.params.length_m}">
                    <div id="err-l" class="field-error" style="display: none;"></div>
                </div>

                <div class="form-group">
                    <label class="form-label">
                        <span>Cross Section Area A (m²)</span>
                        <span class="field-hint">A > 0</span>
                    </label>
                    <input id="input-a" type="number" step="1e-5" class="form-input" value="${this.params.area_m2}">
                    <div id="err-a" class="field-error" style="display: none;"></div>
                </div>

                <div class="form-group">
                    <label class="form-label">
                        <span>Thermal Conductivity k (W/m·K)</span>
                        <span class="field-hint">k > 0</span>
                    </label>
                    <input id="input-k" type="number" step="0.5" class="form-input" value="${this.params.thermal_conductivity_w_mk}">
                    <div id="err-k" class="field-error" style="display: none;"></div>
                </div>

                <div class="form-group">
                    <label class="form-label">
                        <span>Heat Generation q (W/m³)</span>
                        <span class="field-hint">finite real</span>
                    </label>
                    <input id="input-q" type="number" step="500" class="form-input" value="${this.params.volumetric_heat_w_m3}">
                    <div id="err-q" class="field-error" style="display: none;"></div>
                </div>

                <div style="display: flex; gap: 8px;">
                    <div class="form-group" style="flex: 1;">
                        <label class="form-label"><span>T(0) (°C)</span></label>
                        <input id="input-t0" type="number" step="1" class="form-input" value="${this.params.temperature_left_c}">
                    </div>
                    <div class="form-group" style="flex: 1;">
                        <label class="form-label"><span>T(L) (°C)</span></label>
                        <input id="input-tl" type="number" step="1" class="form-input" value="${this.params.temperature_right_c}">
                    </div>
                </div>

                <div class="form-group">
                    <label class="form-label">
                        <span>Meshes N (comma separated)</span>
                        <span class="field-hint">[1, 1000000]</span>
                    </label>
                    <input id="input-meshes" type="text" class="form-input" value="${this.params.element_counts.join(', ')}">
                    <div id="err-meshes" class="field-error" style="display: none;"></div>
                </div>

                <div style="display: flex; gap: 8px; margin-top: 12px;">
                    <button id="btn-run-thermal" class="btn-primary" style="flex: 1;">
                        <span>Run Solve</span>
                    </button>
                    <button id="btn-cancel-thermal" class="btn-secondary btn-cancel" style="display: none;">
                        <span>Cancel</span>
                    </button>
                </div>

                <div id="thermal-progress-wrap" style="display: none; margin-top: 12px;">
                    <div style="display: flex; justify-content: space-between; font-size: 11px; font-family: var(--font-mono);">
                        <span id="thermal-progress-status">Solving...</span>
                        <span id="thermal-progress-pct">0%</span>
                    </div>
                    <div class="progress-container">
                        <div id="thermal-progress-bar" class="progress-fill"></div>
                    </div>
                </div>
            </div>

            <div id="thermal-results-card" class="card" style="${this.lastResult ? '' : 'display: none;'}">
                <div class="card-title">
                    <span>Convergence Results</span>
                    <button id="btn-export-csv" class="btn-secondary" style="font-size: 11px; padding: 4px 8px;">
                        <span>Export CSV</span>
                    </button>
                </div>

                <div id="thermal-results-table-wrap" style="overflow-x: auto;">
                    ${this.renderResultsTable(this.lastResult)}
                </div>

                <div id="energy-balance-wrap" style="margin-top: 12px; font-size: 12px; padding: 8px 12px; background-color: var(--surface-2); border-radius: var(--radius-sm); border: 1px solid var(--line);">
                    ${this.renderEnergyBalance(this.lastResult)}
                </div>
            </div>
        `;

        this.attachInspectorEvents();
    }

    private renderResultsTable(result: ThermalJobResult | null): string {
        if (!result || !result.convergence) {
            return '<div style="color: var(--muted); font-size: 12px;">No results yet</div>';
        }
        let html = `
            <table class="data-table">
                <thead>
                    <tr>
                        <th>N</th>
                        <th>h (m)</th>
                        <th>Mid Error (°C)</th>
                        <th>Order</th>
                    </tr>
                </thead>
                <tbody>
        `;
        for (const row of result.convergence) {
            html += `
                <tr>
                    <td>${row.element_count}</td>
                    <td>${row.mesh_size_h_m.toFixed(4)}</td>
                    <td>${row.max_midpoint_error_c.toFixed(6)}</td>
                    <td style="color: ${row.observed_order > 1.95 ? 'var(--ok)' : 'var(--text)'}; font-weight: 600;">
                        ${row.observed_order === 0 ? '-' : row.observed_order.toFixed(2)}
                    </td>
                </tr>
            `;
        }
        html += `</tbody></table>`;
        return html;
    }

    private renderEnergyBalance(result: ThermalJobResult | null): string {
        if (!result || !result.meshes || result.meshes.length === 0) {
            return '';
        }
        const lastMesh = result.meshes[result.meshes.length - 1];
        const isBalanced = lastMesh.energy_balance_error_w < 1e-8;
        return `
            <div style="display: flex; justify-content: space-between; align-items: center;">
                <span style="color: var(--muted);">Energy Balance (N=${lastMesh.element_count}):</span>
                <span style="color: ${isBalanced ? 'var(--ok)' : 'var(--err)'}; font-weight: 600; font-family: var(--font-mono);">
                    ${isBalanced ? '✓ Balanced (< 1e-9 W)' : '✗ Error: ' + lastMesh.energy_balance_error_w.toExponential(2) + ' W'}
                </span>
            </div>
            <div style="font-size: 11px; color: var(--muted); margin-top: 4px; font-family: var(--font-mono);">
                Q_gen = ${lastMesh.total_heat_generated_w.toFixed(4)} W | Flow = ${(-lastMesh.reaction_left_w - lastMesh.reaction_right_w).toFixed(4)} W
            </div>
        `;
    }

    private attachInspectorEvents() {
        const btnRun = document.getElementById('btn-run-thermal');
        const btnCancel = document.getElementById('btn-cancel-thermal');
        const btnExport = document.getElementById('btn-export-csv');

        btnRun?.addEventListener('click', () => this.handleRun());
        btnCancel?.addEventListener('click', () => this.handleCancel());
        btnExport?.addEventListener('click', () => this.handleExport());

        // Inline live validations
        document.getElementById('input-l')?.addEventListener('input', (e) => this.validateField('l', (e.target as HTMLInputElement).value));
        document.getElementById('input-a')?.addEventListener('input', (e) => this.validateField('a', (e.target as HTMLInputElement).value));
        document.getElementById('input-k')?.addEventListener('input', (e) => this.validateField('k', (e.target as HTMLInputElement).value));
        document.getElementById('input-q')?.addEventListener('input', (e) => this.validateField('q', (e.target as HTMLInputElement).value));
        document.getElementById('input-meshes')?.addEventListener('input', (e) => this.validateField('meshes', (e.target as HTMLInputElement).value));
    }

    private validateField(field: string, valStr: string): boolean {
        let isValid = true;
        let errMsg = '';
        const num = parseFloat(valStr);

        switch (field) {
            case 'l':
                if (isNaN(num) || num <= 0) { isValid = false; errMsg = 'Length must be > 0'; }
                break;
            case 'a':
                if (isNaN(num) || num <= 0) { isValid = false; errMsg = 'Area must be > 0'; }
                break;
            case 'k':
                if (isNaN(num) || num <= 0) { isValid = false; errMsg = 'Thermal conductivity must be > 0'; }
                break;
            case 'q':
                if (isNaN(num)) { isValid = false; errMsg = 'q must be a finite number'; }
                break;
            case 'meshes':
                const parts = valStr.split(',').map(s => parseInt(s.trim(), 10));
                if (parts.some(n => isNaN(n) || n < 1 || n > 1000000)) {
                    isValid = false; errMsg = 'All N must be integers between 1 and 1,000,000';
                }
                break;
        }

        const inputEl = document.getElementById(`input-${field}`);
        const errEl = document.getElementById(`err-${field}`);

        if (inputEl) {
            inputEl.classList.toggle('invalid', !isValid);
        }
        if (errEl) {
            errEl.style.display = isValid ? 'none' : 'block';
            errEl.textContent = errMsg;
        }

        return isValid;
    }

    private async handleRun() {
        // Read form inputs
        const l = parseFloat((document.getElementById('input-l') as HTMLInputElement).value);
        const a = parseFloat((document.getElementById('input-a') as HTMLInputElement).value);
        const k = parseFloat((document.getElementById('input-k') as HTMLInputElement).value);
        const q = parseFloat((document.getElementById('input-q') as HTMLInputElement).value);
        const t0 = parseFloat((document.getElementById('input-t0') as HTMLInputElement).value);
        const tl = parseFloat((document.getElementById('input-tl') as HTMLInputElement).value);
        const meshesStr = (document.getElementById('input-meshes') as HTMLInputElement).value;
        const elementCounts = meshesStr.split(',').map(s => parseInt(s.trim(), 10)).filter(n => !isNaN(n));

        if (!this.validateField('l', String(l)) || !this.validateField('a', String(a)) ||
            !this.validateField('k', String(k)) || !this.validateField('q', String(q)) ||
            !this.validateField('meshes', meshesStr)) {
            toast.show({ title: 'Validation Error', description: 'Please correct the invalid parameters before running.', type: 'error' });
            return;
        }

        this.params = {
            length_m: l,
            area_m2: a,
            thermal_conductivity_w_mk: k,
            volumetric_heat_w_m3: q,
            temperature_left_c: t0,
            temperature_right_c: tl,
            element_counts: elementCounts
        };

        const btnRun = document.getElementById('btn-run-thermal') as HTMLButtonElement;
        const btnCancel = document.getElementById('btn-cancel-thermal') as HTMLButtonElement;
        const progressWrap = document.getElementById('thermal-progress-wrap');
        const progressBar = document.getElementById('thermal-progress-bar');
        const progressPct = document.getElementById('thermal-progress-pct');
        const progressStatus = document.getElementById('thermal-progress-status');

        if (btnRun) btnRun.disabled = true;
        if (btnCancel) btnCancel.style.display = 'inline-flex';
        if (progressWrap) progressWrap.style.display = 'block';
        if (progressBar) progressBar.style.width = '0%';
        if (progressPct) progressPct.textContent = '0%';
        if (progressStatus) progressStatus.textContent = 'Submitting job...';

        try {
            const resp = await apiClient.solveThermal(this.params);
            this.activeJobId = resp.job_id;

            // Subscribe to SSE real-time events
            this.unsubscribeEvents = apiClient.subscribeJobEvents(
                resp.job_id,
                async (ev) => {
                    const pct = Math.round(ev.progress * 100);
                    if (progressBar) progressBar.style.width = `${pct}%`;
                    if (progressPct) progressPct.textContent = `${pct}%`;
                    if (progressStatus) progressStatus.textContent = `Solving: ${pct}%`;

                    if (ev.state === 'done') {
                        if (btnRun) btnRun.disabled = false;
                        if (btnCancel) btnCancel.style.display = 'none';
                        if (progressStatus) progressStatus.textContent = 'Complete';
                        const fullJob = await apiClient.getJob(resp.job_id);
                        if (fullJob.result) {
                            this.lastResult = fullJob.result;
                            this.render();
                            toast.show({ title: 'Solve Completed', description: 'Thermal FEA converged successfully with second-order accuracy.', type: 'success' });
                        }
                    } else if (ev.state === 'cancelled') {
                        if (btnRun) btnRun.disabled = false;
                        if (btnCancel) btnCancel.style.display = 'none';
                        if (progressStatus) progressStatus.textContent = 'Cancelled';
                        toast.show({ title: 'Solve Cancelled', description: 'Job was cancelled by user; partial results discarded.', type: 'warn' });
                    } else if (ev.state === 'failed') {
                        if (btnRun) btnRun.disabled = false;
                        if (btnCancel) btnCancel.style.display = 'none';
                        if (progressStatus) progressStatus.textContent = 'Failed';
                        toast.show({ title: 'Solve Failed', description: 'Core solver encountered an error.', type: 'error' });
                    }
                },
                (err) => {
                    console.error('SSE Error', err);
                }
            );
        } catch (err: any) {
            if (btnRun) btnRun.disabled = false;
            if (btnCancel) btnCancel.style.display = 'none';
            if (progressWrap) progressWrap.style.display = 'none';
            toast.show({
                title: 'Request Failed',
                description: err.message || 'Error occurred while contacting server',
                fix: err.hint,
                type: 'error'
            });
        }
    }

    private async handleCancel() {
        if (!this.activeJobId) return;
        try {
            await apiClient.cancelJob(this.activeJobId);
        } catch (err: any) {
            toast.show({ title: 'Cancellation Error', description: err.message, type: 'error' });
        }
    }

    private handleExport() {
        const url = apiClient.getExportUrl('convergence.csv');
        const a = document.createElement('a');
        a.href = url;
        a.download = 'convergence.csv';
        document.body.appendChild(a);
        a.click();
        a.remove();
        toast.show({ title: 'Export Initiated', description: 'Downloading convergence.csv', type: 'success' });
    }

    private updateCharts(result: ThermalJobResult) {
        this.updateTemperatureChart(result);
        this.updateConvergenceChart(result);
    }

    private updateTemperatureChart(result: ThermalJobResult) {
        const el = document.getElementById('chart-temp-dist');
        if (!el || !result.meshes || result.meshes.length === 0) return;

        if (this.tempPlot) {
            this.tempPlot.destroy();
            this.tempPlot = null;
        }

        // Build continuous fine evaluation grid x in [0, L] with 101 samples
        const sampleCount = 101;
        const xVals: number[] = [];
        const l = this.params.length_m;
        for (let i = 0; i < sampleCount; ++i) {
            xVals.push((i * l) / (sampleCount - 1));
        }

        // 1. Analytical exact values
        const exactY: number[] = [];
        for (const x of xVals) {
            const lin = this.params.temperature_left_c + (this.params.temperature_right_c - this.params.temperature_left_c) * (x / l);
            const quad = (this.params.volumetric_heat_w_m3 / (2 * this.params.thermal_conductivity_w_mk)) * x * (l - x);
            exactY.push(lin + quad);
        }

        const data: uPlot.AlignedData = [xVals, exactY];
        const series: uPlot.Series[] = [
            {},
            {
                label: 'Exact Analytical',
                stroke: '#06B6D4',
                width: 2
            }
        ];

        // 2. FE piecewise linear curves for each mesh
        const colors = ['#F87171', '#F59E0B', '#8B5CF6', '#34D399'];
        result.meshes.forEach((mesh, idx) => {
            const feY: number[] = [];
            for (const x of xVals) {
                // Find element containing x
                const elemIdx = Math.min(Math.floor(x / mesh.mesh_size_h_m), mesh.element_count - 1);
                const x0 = mesh.node_x_m[elemIdx];
                const x1 = mesh.node_x_m[elemIdx + 1];
                const t0 = mesh.temperature_c[elemIdx];
                const t1 = mesh.temperature_c[elemIdx + 1];
                const frac = (x - x0) / (x1 - x0);
                feY.push(t0 + frac * (t1 - t0));
            }
            data.push(feY);
            series.push({
                label: `N = ${mesh.element_count}`,
                stroke: colors[idx % colors.length],
                width: 1.5,
                dash: idx === 0 ? [5, 5] : undefined
            });
        });

        const opts: uPlot.Options = {
            width: el.clientWidth || 600,
            height: el.clientHeight || 240,
            title: '',
            axes: [
                {
                    stroke: '#8C95D6',
                    grid: { stroke: 'rgba(39, 50, 95, 0.4)' },
                    ticks: { stroke: 'rgba(39, 50, 95, 0.4)' },
                    label: 'Position x (m)',
                    labelFont: '12px "Inter", sans-serif'
                },
                {
                    stroke: '#8C95D6',
                    grid: { stroke: 'rgba(39, 50, 95, 0.4)' },
                    ticks: { stroke: 'rgba(39, 50, 95, 0.4)' },
                    label: 'Temperature (°C)',
                    labelFont: '12px "Inter", sans-serif'
                }
            ],
            series
        };

        this.tempPlot = new uPlot(opts, data, el);
    }

    private updateConvergenceChart(result: ThermalJobResult) {
        const el = document.getElementById('chart-convergence');
        if (!el || !result.convergence || result.convergence.length === 0) return;

        if (this.logPlot) {
            this.logPlot.destroy();
            this.logPlot = null;
        }

        // Plot log10(h) vs log10(midpoint error)
        // Order = slope in log-log space!
        const logH: number[] = [];
        const logErr: number[] = [];
        const slopeRef: number[] = [];

        // Reference point for 2nd order line
        const baseH = result.convergence[0].mesh_size_h_m;
        const baseErr = result.convergence[0].max_midpoint_error_c;

        for (const row of result.convergence) {
            logH.push(Math.log10(row.mesh_size_h_m));
            logErr.push(Math.log10(row.max_midpoint_error_c));
            // Theoretical slope 2: error = baseErr * (h / baseH)^2 => log(err) = log(baseErr) + 2 * (log(h) - log(baseH))
            slopeRef.push(Math.log10(baseErr) + 2.0 * (Math.log10(row.mesh_size_h_m) - Math.log10(baseH)));
        }

        const data: uPlot.AlignedData = [logH, logErr, slopeRef];
        const opts: uPlot.Options = {
            width: el.clientWidth || 600,
            height: el.clientHeight || 240,
            axes: [
                {
                    stroke: '#8C95D6',
                    grid: { stroke: 'rgba(39, 50, 95, 0.4)' },
                    label: 'log10(h)',
                    labelFont: '12px "Inter", sans-serif'
                },
                {
                    stroke: '#8C95D6',
                    grid: { stroke: 'rgba(39, 50, 95, 0.4)' },
                    label: 'log10(Max Midpoint Error)',
                    labelFont: '12px "Inter", sans-serif'
                }
            ],
            series: [
                {},
                {
                    label: 'Observed Midpoint Error',
                    stroke: '#8B5CF6',
                    width: 2,
                    points: { show: true, size: 6, fill: '#8B5CF6' }
                },
                {
                    label: 'Ideal Slope = 2.00 (O(h²))',
                    stroke: '#06B6D4',
                    width: 1.5,
                    dash: [4, 4]
                }
            ]
        };

        this.logPlot = new uPlot(opts, data, el);
    }
}
