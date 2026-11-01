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
}

export const apiClient = new ApiClient();
