export interface HealthResponse {
    status: string;
    version: string;
}

export interface ApiError {
    field?: string;
    line_number?: number;
    message: string;
    hint?: string;
}

export interface BoundingBox {
    min_x: number;
    max_x: number;
    min_y: number;
    max_y: number;
    min_z: number;
    max_z: number;
    diagonal: number;
}

export interface StlMeshResponse {
    solid_name: string;
    triangle_count: number;
    degenerate_count: number;
    warnings: string[];
    bounding_box: BoundingBox;
    vertices: number[];
    normals: number[];
}

export interface NodalCsvResponse {
    node_count: number;
    min_temperature_c: number;
    max_temperature_c: number;
    is_synthetic: boolean;
    bounding_box: BoundingBox;
    node_ids: number[];
    positions: number[];
    temperatures: number[];
}

export interface ThermalSolveParams {
    length_m: number;
    area_m2: number;
    thermal_conductivity_w_mk: number;
    volumetric_heat_w_m3: number;
    temperature_left_c: number;
    temperature_right_c: number;
    element_counts: number[];
}

export interface MeshResult {
    element_count: number;
    mesh_size_h_m: number;
    node_x_m: number[];
    temperature_c: number[];
    exact_temperature_c: number[];
    max_midpoint_error_c: number;
    max_nodal_error_c: number;
    reaction_left_w: number;
    reaction_right_w: number;
    total_heat_generated_w: number;
    energy_balance_error_w: number;
}

export interface ConvergenceRow {
    element_count: number;
    mesh_size_h_m: number;
    max_midpoint_error_c: number;
    observed_order: number;
}

export interface ThreadBenchmarkResult {
    thread_count: number;
    warmup_ms: number;
    repeat_times_ms: number[];
    median_ms: number;
    speedup: number;
    efficiency: number;
}

export interface BenchmarkJobResult {
    batch_size: number;
    element_count: number;
    seed: number;
    logical_cpus: number;
    results: ThreadBenchmarkResult[];
}

export interface BenchmarkRunParams {
    batch_size: number;
    element_count: number;
    seed: number;
    threads: number[];
}

export interface DeviceRecoveryLog {
    attempt: number;
    backoff_ms: number;
    succeeded: boolean;
    message: string;
}

export interface DeviceTelemetryResponse {
    device?: string;
    type?: string;
    device_name: string;
    part_number: string;
    state: 'connected' | 'degraded' | 'faulted' | 'recovering';
    simulated: boolean;
    uptime_seconds: number;
    sequence_number: number;
    temperature_c: number;
    voltage_v: number;
    current_ma: number;
    extra_metric_1: number;
    extra_metric_2: number;
    status_word: string;
    recovery_history: DeviceRecoveryLog[];
}

export interface ThermalJobResult {
    meshes: MeshResult[];
    convergence: ConvergenceRow[];
}

export interface JobStatusResponse {
    job_id: string;
    state: 'queued' | 'running' | 'done' | 'failed' | 'cancelled';
    progress: number;
    result?: ThermalJobResult;
    error?: { message: string };
}

export class ApiClient {
    private baseUrl: string;

    constructor(baseUrl: string = '') {
        this.baseUrl = baseUrl;
    }

    async getHealth(): Promise<HealthResponse> {
        const res = await fetch(`${this.baseUrl}/api/health`);
        if (!res.ok) {
            throw new Error(`Health check failed with HTTP ${res.status}`);
        }
        return res.json();
    }

    async solveThermal(params: ThermalSolveParams): Promise<{ job_id: string; state: string }> {
        const res = await fetch(`${this.baseUrl}/api/thermal/solve`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(params)
        });

        if (!res.ok) {
            const err: ApiError = await res.json().catch(() => ({ message: 'Solve request failed' }));
            throw err;
        }
        return res.json();
    }

    async runBenchmark(params: BenchmarkRunParams): Promise<{ job_id: string; state: string }> {
        const res = await fetch(`${this.baseUrl}/api/benchmark/run`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(params)
        });

        if (!res.ok) {
            const err: ApiError = await res.json().catch(() => ({ message: 'Benchmark request failed' }));
            throw err;
        }
        return res.json();
    }

    async getBenchmarkResults(): Promise<{ status: string; logical_cpus: number; results: ThreadBenchmarkResult[] }> {
        const res = await fetch(`${this.baseUrl}/api/benchmark/results`);
        if (!res.ok) {
            throw new Error(`Failed to fetch benchmark results (${res.status})`);
        }
        return res.json();
    }

    async getJob(jobId: string): Promise<JobStatusResponse> {
        const res = await fetch(`${this.baseUrl}/api/jobs/${jobId}`);
        if (!res.ok) {
            throw new Error(`Failed to fetch job ${jobId}`);
        }
        return res.json();
    }

    async cancelJob(jobId: string): Promise<void> {
        const res = await fetch(`${this.baseUrl}/api/jobs/${jobId}`, {
            method: 'DELETE'
        });
        if (!res.ok) {
            throw new Error(`Failed to cancel job ${jobId}`);
        }
    }

    subscribeJobEvents(
        jobId: string,
        onEvent: (event: { job_id: string; state: string; progress: number }) => void,
        onError?: (err: unknown) => void
    ): () => void {
        const eventSource = new EventSource(`${this.baseUrl}/api/jobs/${jobId}/events`);

        eventSource.onmessage = (msg) => {
            try {
                const data = JSON.parse(msg.data);
                onEvent(data);
                if (data.state === 'done' || data.state === 'failed' || data.state === 'cancelled') {
                    eventSource.close();
                }
            } catch (err) {
                if (onError) onError(err);
            }
        };

        eventSource.onerror = (err) => {
            if (onError) onError(err);
            eventSource.close();
        };

        return () => {
            eventSource.close();
        };
    }

    getExportUrl(filename: string): string {
        return `${this.baseUrl}/api/export/${encodeURIComponent(filename)}`;
    }

    async uploadStl(content: string): Promise<StlMeshResponse> {
        const res = await fetch(`${this.baseUrl}/api/geometry/stl`, {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: content
        });

        if (!res.ok) {
            const err: ApiError = await res.json().catch(() => ({ message: 'STL upload failed' }));
            throw err;
        }
        return res.json();
    }

    async loadSampleStl(): Promise<StlMeshResponse> {
        const res = await fetch(`${this.baseUrl}/api/geometry/stl`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ path: 'data/cube_or_bracket.stl' })
        });

        if (!res.ok) {
            const err: ApiError = await res.json().catch(() => ({ message: 'Failed to load sample STL' }));
            throw err;
        }
        return res.json();
    }

    async uploadCsv(content: string): Promise<NodalCsvResponse> {
        const res = await fetch(`${this.baseUrl}/api/results/csv`, {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: content
        });

        if (!res.ok) {
            const err: ApiError = await res.json().catch(() => ({ message: 'CSV upload failed' }));
            throw err;
        }
        return res.json();
    }

    async loadSampleCsv(): Promise<NodalCsvResponse> {
        const res = await fetch(`${this.baseUrl}/api/results/csv`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ path: 'data/nodal_temperature_synthetic.csv' })
        });

        if (!res.ok) {
            const err: ApiError = await res.json().catch(() => ({ message: 'Failed to load sample CSV' }));
            throw err;
        }
        return res.json();
    }

    async getDevices(): Promise<{ simulated: boolean; devices: DeviceTelemetryResponse[] }> {
        const res = await fetch(`${this.baseUrl}/api/devices`);
        if (!res.ok) {
            throw new Error(`Failed to fetch devices (${res.status})`);
        }
        return res.json();
    }

    async deviceAction(
        deviceType: string,
        action: 'telemetry' | 'ping' | 'reset' | 'inject_fault',
        failCount: number = 2
    ): Promise<DeviceTelemetryResponse | { simulated: boolean; status: string; fail_count?: number }> {
        const res = await fetch(`${this.baseUrl}/api/devices/${deviceType}/${action}`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ fail_count: failCount })
        });

        if (!res.ok) {
            const err: ApiError = await res.json().catch(() => ({ message: `Action ${action} failed` }));
            throw err;
        }
        return res.json();
    }
}

export const apiClient = new ApiClient();

