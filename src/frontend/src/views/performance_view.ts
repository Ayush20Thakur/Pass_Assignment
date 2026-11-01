import { apiClient, BenchmarkRunParams, ThreadBenchmarkResult } from '../api/client';
import { toast } from '../components/toast';

export class PerformanceView {
    private canvasContainer: HTMLElement;
    private inspectorPanel: HTMLElement;

    private activeJobId: string | null = null;
    private unsubscribeEvents: (() => void) | null = null;

    private params: BenchmarkRunParams = {
        batch_size: 1500,
        element_count: 50,
        seed: 42,
        threads: [1, 2, 4]
    };

    private results: ThreadBenchmarkResult[] = [];
    private logicalCpus: number = 4;

    constructor(canvasContainer: HTMLElement, inspectorPanel: HTMLElement) {
        this.canvasContainer = canvasContainer;
        this.inspectorPanel = inspectorPanel;
    }

    render(): void {
        this.renderCanvas();
        this.renderInspector();
        this.bindEvents();
        this.detectSystemCores();
        this.loadExistingResults();
    }

    private renderCanvas(): void {
        this.canvasContainer.innerHTML = `
            <div style="display: flex; flex-direction: column; height: 100%; width: 100%; padding: var(--gap); gap: var(--gap); overflow-y: auto;">
                <!-- Top Chart: Speedup vs Thread Count -->
                <div style="flex: 1; min-height: 320px; background-color: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); padding: 16px; display: flex; flex-direction: column;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
                        <div>
                            <span style="font-weight: 600; font-size: 14px; color: var(--text);">Parallel Speedup S_p = T_1 / T_p</span>
                            <span style="font-family: var(--font-mono); font-size: 11px; color: var(--muted); margin-left: 8px;">
                                Actual vs. Ideal Linear Scaling (y = x)
                            </span>
                        </div>
                        <div style="display: flex; gap: 8px; align-items: center;">
                            <span class="brand-badge" id="badge-cpus">Logical CPUs: Detecting...</span>
                            <a id="btn-export-bench" class="btn-secondary" style="text-decoration: none; font-size: 11px; padding: 4px 10px;" href="/api/export/benchmark.csv" download="benchmark.csv">
                                ⬇ Export CSV
                            </a>
                        </div>
                    </div>

                    <!-- SVG Chart Container -->
                    <div id="chart-speedup-container" style="flex: 1; width: 100%; min-height: 240px; position: relative;">
                        <svg id="svg-speedup" width="100%" height="100%" style="overflow: visible;"></svg>
                    </div>
                </div>

                <!-- Bottom Table: Multi-Core Scaling & Efficiency Table -->
                <div style="min-height: 220px; background-color: var(--surface); border: 1px solid var(--line); border-radius: var(--radius); padding: 16px; display: flex; flex-direction: column;">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">
                        <span style="font-weight: 600; font-size: 13px; color: var(--text);">Benchmark Repeat Metrics & Parallel Efficiency</span>
                        <span style="font-family: var(--font-mono); font-size: 11px; color: var(--muted);">5 Measured Repeats | Median Selection | Efficiency = (T_1/T_p)/p</span>
                    </div>

                    <div style="overflow-x: auto;">
                        <table style="width: 100%; border-collapse: collapse; font-family: var(--font-mono); font-size: 12px; text-align: right;">
                            <thead>
                                <tr style="border-bottom: 1px solid var(--line); color: var(--muted); font-size: 11px; text-transform: uppercase;">
                                    <th style="padding: 8px; text-align: left;">Threads (p)</th>
                                    <th style="padding: 8px;">Warm-Up (ms)</th>
                                    <th style="padding: 8px;">Repeats 1..5 (ms)</th>
                                    <th style="padding: 8px;">Median T_p (ms)</th>
                                    <th style="padding: 8px;">Speedup S_p</th>
                                    <th style="padding: 8px; text-align: center;">Efficiency E_p</th>
                                </tr>
                            </thead>
                            <tbody id="table-bench-body">
                                <tr>
                                    <td colspan="6" style="padding: 24px; text-align: center; color: var(--muted); font-family: var(--font-ui);">
                                        No benchmark data yet. Click <em>Run Multithreaded Benchmark</em> to evaluate scaling.
                                    </td>
                                </tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>
        `;
    }

    private renderInspector(): void {
        this.inspectorPanel.innerHTML = `
            <!-- Benchmark Configuration Card -->
            <div class="card">
                <div class="card-title">
                    <span>Benchmark Parameters</span>
                    <span class="brand-badge">Batch FEM</span>
                </div>

                <div class="form-group">
                    <label class="form-label" for="inp-batch-size">
                        <span>Batch Size (M problems)</span>
                        <span id="txt-batch-size" style="font-family: var(--font-mono); color: var(--cyan);">1,500</span>
                    </label>
                    <input type="range" id="inp-batch-size" min="200" max="4000" step="100" value="1500" style="width: 100%; accent-color: var(--cyan);" />
                </div>

                <div class="form-group">
                    <label class="form-label" for="inp-element-count">
                        <span>Elements per Solve (N)</span>
                        <span id="txt-element-count" style="font-family: var(--font-mono); color: var(--cyan);">50</span>
                    </label>
                    <input type="range" id="inp-element-count" min="10" max="150" step="10" value="50" style="width: 100%; accent-color: var(--cyan);" />
                </div>

                <div class="form-group">
                    <label class="form-label" for="inp-seed">PRNG Seed</label>
                    <input type="number" id="inp-seed" class="form-input" value="42" />
                    <span style="font-size: 11px; color: var(--muted);">Formula: (mt19937_64(seed)() >> 11) * 2^-53</span>
                </div>

                <div class="form-group">
                    <label class="form-label">Thread Counts</label>
                    <div id="thread-checkboxes-container" style="display: flex; gap: 12px; align-items: center; margin-top: 4px;">
                        <!-- Generated dynamically based on detected cores -->
                    </div>
                </div>

                <!-- Run / Cancel Actions -->
                <div style="display: flex; gap: 8px; margin-top: 14px;">
                    <button id="btn-run-bench" class="btn-primary" style="flex: 1;">
                        ⚡ Run Benchmark
                    </button>
                    <button id="btn-cancel-bench" class="btn-secondary hidden" style="color: var(--err); border-color: rgba(248, 113, 113, 0.4);">
                        Cancel
                    </button>
                </div>

                <!-- Live Progress Bar -->
                <div id="bench-progress-wrapper" class="hidden" style="margin-top: 12px;">
                    <div style="display: flex; justify-content: space-between; font-size: 11px; margin-bottom: 4px;">
                        <span id="bench-progress-label" style="color: var(--muted);">Running benchmark...</span>
                        <span id="bench-progress-pct" style="font-family: var(--font-mono); color: var(--cyan);">0%</span>
                    </div>
                    <div style="width: 100%; height: 6px; background-color: var(--surface-2); border-radius: 3px; overflow: hidden;">
                        <div id="bench-progress-bar" style="width: 0%; height: 100%; background: var(--aurora); transition: width 150ms ease;"></div>
                    </div>
                </div>
            </div>

            <!-- Execution Model Card -->
            <div class="card">
                <div class="card-title">Concurrency Architecture</div>
                <div style="display: flex; flex-direction: column; gap: 8px; font-size: 12px;">
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Thread Pool:</span>
                        <span style="font-family: var(--font-mono); font-weight: 600;">Bounded std::thread</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Work Distribution:</span>
                        <span style="font-family: var(--font-mono); font-weight: 600; color: var(--cyan);">Atomic Chunk (16)</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Determinism:</span>
                        <span style="font-family: var(--font-mono); font-weight: 600; color: var(--ok);">Max diff &le; 1e-8 °C</span>
                    </div>
                    <div style="border-top: 1px solid var(--line); padding-top: 6px; margin-top: 4px; font-size: 11px; color: var(--muted); line-height: 1.4;">
                        Avoids thread explosion (no std::async spam). Each worker claims chunks via <code>fetch_add(16)</code> to ensure optimal dynamic load balancing with minimal lock contention.
                    </div>
                </div>
            </div>

            <!-- Amdahl's Law Scaling Reference -->
            <div class="card">
                <div class="card-title">Amdahl's Law & Efficiency</div>
                <div style="font-size: 11px; color: var(--muted); line-height: 1.5;">
                    Parallel speedup is bounded by serial fraction and memory bus bandwidth:
                    <div style="background-color: var(--surface-2); padding: 8px; border-radius: var(--radius-sm); margin: 6px 0; font-family: var(--font-mono); color: var(--text); text-align: center;">
                        S(p) = 1 / ((1 - P) + P / p)
                    </div>
                    Efficiency $E_p > 70\%$ represents near-linear scaling; sublinear drop-off at higher thread counts reflects memory bandwidth limits and L3 cache sharing.
                </div>
            </div>
        `;
    }

    private bindEvents(): void {
        const inpBatchSize = document.getElementById('inp-batch-size') as HTMLInputElement;
        const txtBatchSize = document.getElementById('txt-batch-size');
        const inpElementCount = document.getElementById('inp-element-count') as HTMLInputElement;
        const txtElementCount = document.getElementById('txt-element-count');
        const inpSeed = document.getElementById('inp-seed') as HTMLInputElement;

        const btnRun = document.getElementById('btn-run-bench');
        const btnCancel = document.getElementById('btn-cancel-bench');

        inpBatchSize?.addEventListener('input', () => {
            this.params.batch_size = parseInt(inpBatchSize.value, 10);
            if (txtBatchSize) txtBatchSize.textContent = this.params.batch_size.toLocaleString();
        });

        inpElementCount?.addEventListener('input', () => {
            this.params.element_count = parseInt(inpElementCount.value, 10);
            if (txtElementCount) txtElementCount.textContent = this.params.element_count.toString();
        });

        inpSeed?.addEventListener('change', () => {
            this.params.seed = parseInt(inpSeed.value, 10) || 42;
        });

        btnRun?.addEventListener('click', () => this.startBenchmark());
        btnCancel?.addEventListener('click', () => this.cancelBenchmark());

        // Re-draw chart on window resize
        window.addEventListener('resize', () => {
            if (this.results.length > 0) {
                this.renderSpeedupChart();
            }
        });
    }

    private detectSystemCores(): void {
        const hwConcurrency = navigator.hardwareConcurrency || 4;
        this.logicalCpus = hwConcurrency;

        const badgeCpus = document.getElementById('badge-cpus');
        if (badgeCpus) badgeCpus.textContent = `Logical CPUs: ${this.logicalCpus}`;

        // Populate thread checkboxes
        const container = document.getElementById('thread-checkboxes-container');
        if (!container) return;

        const threadOptions = [1];
        if (this.logicalCpus >= 2) threadOptions.push(2);
        if (this.logicalCpus >= 4) threadOptions.push(4);
        if (this.logicalCpus >= 8) threadOptions.push(8);
        if (this.logicalCpus >= 16) threadOptions.push(16);

        this.params.threads = threadOptions.slice(0, 3); // default [1, 2, 4]

        container.innerHTML = threadOptions.map(t => `
            <label style="display: flex; align-items: center; gap: 4px; font-family: var(--font-mono); font-size: 12px; cursor: pointer;">
                <input type="checkbox" class="chk-thread-option" value="${t}" ${this.params.threads.includes(t) ? 'checked' : ''} />
                <span>${t}T</span>
            </label>
        `).join('');

        container.querySelectorAll('.chk-thread-option').forEach(el => {
            el.addEventListener('change', () => {
                const checkedThreads: number[] = [1]; // always include 1
                container.querySelectorAll<HTMLInputElement>('.chk-thread-option:checked').forEach(c => {
                    const val = parseInt(c.value, 10);
                    if (!checkedThreads.includes(val)) checkedThreads.push(val);
                });
                checkedThreads.sort((a, b) => a - b);
                this.params.threads = checkedThreads;
            });
        });
    }

    private async loadExistingResults(): Promise<void> {
        try {
            const data = await apiClient.getBenchmarkResults();
            if (data.results && data.results.length > 0) {
                this.results = data.results;
                this.logicalCpus = data.logical_cpus || this.logicalCpus;
                const badgeCpus = document.getElementById('badge-cpus');
                if (badgeCpus) badgeCpus.textContent = `Logical CPUs: ${this.logicalCpus}`;
                this.renderResultsTable();
                this.renderSpeedupChart();
            }
        } catch {
            // No prior benchmark run on disk, silently ignore
        }
    }

    private async startBenchmark(): Promise<void> {
        const btnRun = document.getElementById('btn-run-bench') as HTMLButtonElement;
        const btnCancel = document.getElementById('btn-cancel-bench') as HTMLButtonElement;
        const progressWrapper = document.getElementById('bench-progress-wrapper');
        const progressBar = document.getElementById('bench-progress-bar');
        const progressPct = document.getElementById('bench-progress-pct');
        const progressLabel = document.getElementById('bench-progress-label');

        btnRun.disabled = true;
        btnCancel?.classList.remove('hidden');
        progressWrapper?.classList.remove('hidden');

        if (progressBar) progressBar.style.width = '0%';
        if (progressPct) progressPct.textContent = '0%';
        if (progressLabel) progressLabel.textContent = 'Spawning thread pool & warming up...';

        try {
            const resp = await apiClient.runBenchmark(this.params);
            this.activeJobId = resp.job_id;

            this.unsubscribeEvents = apiClient.subscribeJobEvents(
                this.activeJobId,
                (evt) => {
                    const pct = Math.round(evt.progress * 100);
                    if (progressBar) progressBar.style.width = `${pct}%`;
                    if (progressPct) progressPct.textContent = `${pct}%`;

                    if (progressLabel) {
                        if (pct < 10) progressLabel.textContent = 'Executing 1T baseline...';
                        else if (pct < 50) progressLabel.textContent = 'Measuring parallel repeats...';
                        else if (pct < 95) progressLabel.textContent = 'Computing medians & speedups...';
                        else progressLabel.textContent = 'Writing benchmark.csv...';
                    }

                    if (evt.state === 'done') {
                        this.finishBenchmark(true);
                    } else if (evt.state === 'failed') {
                        this.finishBenchmark(false, 'Benchmark job failed');
                    } else if (evt.state === 'cancelled') {
                        this.finishBenchmark(false, 'Benchmark cancelled');
                    }
                },
                (err) => {
                    this.finishBenchmark(false, String(err));
                }
            );
        } catch (err: unknown) {
            this.finishBenchmark(false, (err as Error).message || 'Failed to start benchmark');
        }
    }

    private async cancelBenchmark(): Promise<void> {
        if (!this.activeJobId) return;
        try {
            await apiClient.cancelJob(this.activeJobId);
            toast.show({
                title: 'Benchmark Cancelled',
                description: 'Worker threads signalled stop token.',
                type: 'warn'
            });
        } catch {
            // Already cancelled or done
        }
    }

    private async finishBenchmark(success: boolean, errorMsg?: string): Promise<void> {
        const btnRun = document.getElementById('btn-run-bench') as HTMLButtonElement;
        const btnCancel = document.getElementById('btn-cancel-bench') as HTMLButtonElement;
        const progressWrapper = document.getElementById('bench-progress-wrapper');

        btnRun.disabled = false;
        btnCancel?.classList.add('hidden');
        progressWrapper?.classList.add('hidden');

        if (this.unsubscribeEvents) {
            this.unsubscribeEvents();
            this.unsubscribeEvents = null;
        }

        if (success && this.activeJobId) {
            const jobStatus = await apiClient.getJob(this.activeJobId);
            if (jobStatus.result && (jobStatus.result as unknown as { results: ThreadBenchmarkResult[] }).results) {
                const suite = jobStatus.result as unknown as { results: ThreadBenchmarkResult[]; logical_cpus: number };
                this.results = suite.results;
                this.logicalCpus = suite.logical_cpus || this.logicalCpus;
                this.renderResultsTable();
                this.renderSpeedupChart();

                toast.show({
                    title: 'Benchmark Completed',
                    description: `Evaluated ${this.results.length} thread configurations for ${this.params.batch_size} problems.`,
                    type: 'success'
                });
            }
        } else if (errorMsg) {
            toast.show({
                title: 'Benchmark Failed',
                description: errorMsg,
                type: 'error'
            });
        }
        this.activeJobId = null;
    }

    private renderResultsTable(): void {
        const tbody = document.getElementById('table-bench-body');
        if (!tbody) return;

        if (this.results.length === 0) {
            tbody.innerHTML = `
                <tr>
                    <td colspan="6" style="padding: 24px; text-align: center; color: var(--muted); font-family: var(--font-ui);">
                        No benchmark data available.
                    </td>
                </tr>
            `;
            return;
        }

        tbody.innerHTML = this.results.map(m => {
            const effPct = m.efficiency * 100;
            let effColor = 'var(--ok)';
            if (effPct < 50) effColor = 'var(--err)';
            else if (effPct < 70) effColor = 'var(--warn)';

            const repeatsStr = m.repeat_times_ms.map(r => r.toFixed(1)).join(', ');

            return `
                <tr style="border-bottom: 1px solid var(--line); height: 38px;">
                    <td style="padding: 8px; text-align: left; font-weight: 600; color: var(--text);">
                        <span class="badge ${m.thread_count === 1 ? 'badge-neutral' : 'badge-accent'}">
                            ${m.thread_count} ${m.thread_count === 1 ? 'Thread (Baseline)' : 'Threads'}
                        </span>
                    </td>
                    <td style="padding: 8px; color: var(--muted);">${m.warmup_ms.toFixed(2)}</td>
                    <td style="padding: 8px; font-size: 11px; color: var(--muted);" title="${repeatsStr}">
                        [${repeatsStr}]
                    </td>
                    <td style="padding: 8px; font-weight: 600; color: var(--cyan);">${m.median_ms.toFixed(2)} ms</td>
                    <td style="padding: 8px; font-weight: 700; color: ${m.speedup >= 1.0 ? 'var(--ok)' : 'var(--warn)'};">
                        ${m.speedup.toFixed(2)}x
                    </td>
                    <td style="padding: 8px; text-align: center;">
                        <span style="display: inline-block; padding: 2px 8px; border-radius: var(--radius-sm); font-weight: 600; font-size: 11px; background: rgba(255,255,255,0.05); color: ${effColor}; border: 1px solid ${effColor};">
                            ${effPct.toFixed(1)}%
                        </span>
                    </td>
                </tr>
            `;
        }).join('');
    }

    private renderSpeedupChart(): void {
        const svg = document.getElementById('svg-speedup') as unknown as SVGSVGElement;
        const container = document.getElementById('chart-speedup-container');
        if (!svg || !container || this.results.length === 0) return;

        const width = container.clientWidth || 700;
        const height = container.clientHeight || 260;

        const padLeft = 50;
        const padRight = 30;
        const padTop = 30;
        const padBottom = 40;

        const plotWidth = width - padLeft - padRight;
        const plotHeight = height - padTop - padBottom;

        const maxThreads = Math.max(...this.results.map(r => r.thread_count), 4);
        const maxSpeedup = Math.max(...this.results.map(r => r.speedup), maxThreads);

        const xScale = (t: number) => padLeft + ((t - 1) / Math.max(1, maxThreads - 1)) * plotWidth;
        const yScale = (s: number) => padTop + plotHeight - (s / maxSpeedup) * plotHeight;

        let svgHtml = '';

        // Grid lines (horizontal)
        const yTicks = 4;
        for (let i = 0; i <= yTicks; i++) {
            const sVal = (maxSpeedup / yTicks) * i;
            const y = yScale(sVal);
            svgHtml += `
                <line x1="${padLeft}" y1="${y}" x2="${width - padRight}" y2="${y}" stroke="var(--line)" stroke-dasharray="3,3" />
                <text x="${padLeft - 8}" y="${y + 4}" fill="var(--muted)" font-family="var(--font-mono)" font-size="10" text-anchor="end">${sVal.toFixed(1)}x</text>
            `;
        }

        // Ideal linear scaling line (y = x)
        const idealX1 = xScale(1);
        const idealY1 = yScale(1);
        const idealX2 = xScale(maxThreads);
        const idealY2 = yScale(maxThreads);

        svgHtml += `
            <line x1="${idealX1}" y1="${idealY1}" x2="${idealX2}" y2="${idealY2}" stroke="var(--muted)" stroke-width="1.5" stroke-dasharray="4,4" opacity="0.6" />
            <text x="${idealX2 - 4}" y="${idealY2 - 8}" fill="var(--muted)" font-family="var(--font-mono)" font-size="10" text-anchor="end">Ideal Linear (y = x)</text>
        `;

        // Actual speedup curve and points
        let pathD = '';
        this.results.forEach((r, idx) => {
            const cx = xScale(r.thread_count);
            const cy = yScale(r.speedup);
            if (idx === 0) pathD += `M ${cx} ${cy} `;
            else pathD += `L ${cx} ${cy} `;
        });

        // Speedup area under curve
        if (this.results.length > 1) {
            const firstX = xScale(this.results[0].thread_count);
            const lastX = xScale(this.results[this.results.length - 1].thread_count);
            const baselineY = yScale(0);
            const areaD = `${pathD} L ${lastX} ${baselineY} L ${firstX} ${baselineY} Z`;

            svgHtml += `
                <defs>
                    <linearGradient id="speedup-gradient" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="0%" stop-color="#06b6d4" stop-opacity="0.35"/>
                        <stop offset="100%" stop-color="#6366f1" stop-opacity="0.02"/>
                    </linearGradient>
                </defs>
                <path d="${areaD}" fill="url(#speedup-gradient)" />
            `;
        }

        svgHtml += `<path d="${pathD}" fill="none" stroke="var(--cyan)" stroke-width="2.5" />`;

        // Data points and labels
        this.results.forEach(r => {
            const cx = xScale(r.thread_count);
            const cy = yScale(r.speedup);

            svgHtml += `
                <circle cx="${cx}" cy="${cy}" r="5" fill="var(--surface)" stroke="var(--cyan)" stroke-width="2.5" />
                <text x="${cx}" y="${cy - 10}" fill="var(--text)" font-family="var(--font-mono)" font-size="11" font-weight="600" text-anchor="middle">
                    ${r.speedup.toFixed(2)}x
                </text>
                <text x="${cx}" y="${padTop + plotHeight + 18}" fill="var(--text)" font-family="var(--font-mono)" font-size="11" text-anchor="middle">
                    ${r.thread_count}T
                </text>
            `;
        });

        // X Axis line
        svgHtml += `
            <line x1="${padLeft}" y1="${padTop + plotHeight}" x2="${width - padRight}" y2="${padTop + plotHeight}" stroke="var(--line)" stroke-width="1.5" />
            <text x="${width / 2}" y="${height - 6}" fill="var(--muted)" font-family="var(--font-ui)" font-size="11" text-anchor="middle">Allocated Worker Threads</text>
        `;

        svg.innerHTML = svgHtml;
    }

    destroy(): void {
        if (this.unsubscribeEvents) {
            this.unsubscribeEvents();
            this.unsubscribeEvents = null;
        }
        this.canvasContainer.innerHTML = '';
        this.inspectorPanel.innerHTML = '';
    }
}
