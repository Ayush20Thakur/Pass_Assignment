import './styles/theme.css';
import { apiClient } from './api/client';
import { ThermalView } from './views/thermal_view';
import { GeometryView } from './views/geometry_view';
import { PerformanceView } from './views/performance_view';
import { PlaceholderView } from './views/placeholder_view';
import { toast } from './components/toast';

type ViewMode = 'geometry' | 'thermal' | 'performance' | 'hardware';

class ForgeApp {
    private currentMode: ViewMode = 'thermal';
    private currentView: { render: () => void; destroy: () => void } | null = null;

    private canvasEl!: HTMLElement;
    private inspectorEl!: HTMLElement;
    private statusIndicator!: HTMLElement;
    private statusText!: HTMLElement;

    init() {
        this.renderShell();
        this.bindEvents();
        this.switchView('thermal');
        this.checkServerHealth();
    }

    private renderShell() {
        const app = document.getElementById('app')!;
        app.innerHTML = `
            <header class="app-header">
                <div class="brand-container">
                    <span class="brand-title">FORGE</span>
                    <span class="brand-badge">v1.0.0</span>
                    <span class="brand-badge" style="color: var(--cyan); border-color: rgba(6, 182, 212, 0.4);">Windows 11 x64</span>
                </div>

                <div style="display: flex; align-items: center; gap: 12px;">
                    <div id="conn-pill" class="status-pill">
                        <span id="conn-indicator" class="status-indicator disconnected"></span>
                        <span id="conn-text" style="font-family: var(--font-mono); font-size: 11px;">Checking...</span>
                    </div>
                </div>
            </header>

            <div class="workspace-body">
                <nav class="nav-rail">
                    <button id="tab-geometry" class="nav-tab-button" data-view="geometry" title="Geometry (STL/CSV 3D View)">
                        <span style="font-size: 16px;">📐</span>
                        <span>Geometry</span>
                    </button>
                    <button id="tab-thermal" class="nav-tab-button active" data-view="thermal" title="Thermal Analysis (1D FEA)">
                        <span style="font-size: 16px;">🔥</span>
                        <span>Thermal</span>
                    </button>
                    <button id="tab-performance" class="nav-tab-button" data-view="performance" title="Performance & Multithreaded Benchmarks">
                        <span style="font-size: 16px;">⚡</span>
                        <span>Perf</span>
                    </button>
                    <button id="tab-hardware" class="nav-tab-button" data-view="hardware" title="Embedded Hardware Devices">
                        <span style="font-size: 16px;">🔌</span>
                        <span>Hardware</span>
                    </button>
                </nav>

                <main id="canvas-container" class="canvas-container"></main>
                <aside id="inspector-panel" class="inspector-panel"></aside>
            </div>
        `;

        this.canvasEl = document.getElementById('canvas-container')!;
        this.inspectorEl = document.getElementById('inspector-panel')!;
        this.statusIndicator = document.getElementById('conn-indicator')!;
        this.statusText = document.getElementById('conn-text')!;
    }

    private bindEvents() {
        const navButtons = document.querySelectorAll<HTMLButtonElement>('.nav-tab-button');
        navButtons.forEach(btn => {
            btn.addEventListener('click', () => {
                const view = btn.dataset.view as ViewMode;
                this.switchView(view);
            });
        });
    }

    getCurrentMode(): ViewMode {
        return this.currentMode;
    }

    switchView(mode: ViewMode) {
        if (this.currentView) {
            this.currentView.destroy();
        }

        this.currentMode = mode;

        document.querySelectorAll('.nav-tab-button').forEach(btn => {
            const b = btn as HTMLButtonElement;
            b.classList.toggle('active', b.dataset.view === mode);
        });

        switch (mode) {
            case 'thermal':
                this.currentView = new ThermalView(this.canvasEl, this.inspectorEl);
                break;
            case 'geometry':
                this.currentView = new GeometryView(this.canvasEl, this.inspectorEl);
                break;
            case 'performance':
                this.currentView = new PerformanceView(this.canvasEl, this.inspectorEl);
                break;
            case 'hardware':
                this.currentView = new PlaceholderView(
                    this.canvasEl, this.inspectorEl,
                    'Embedded Hardware Telemetry',
                    'Simulated hardware controllers with timeout injection and automated recovery.',
                    'Phase 7'
                );
                break;
        }

        this.currentView.render();
    }

    private async checkServerHealth() {
        try {
            const health = await apiClient.getHealth();
            this.statusIndicator.className = 'status-indicator connected';
            this.statusText.textContent = `Backend Ready (v${health.version})`;
        } catch (err) {
            this.statusIndicator.className = 'status-indicator disconnected';
            this.statusText.textContent = 'Backend Offline';
            toast.show({
                title: 'Backend Offline',
                description: 'Unable to connect to Forge local C++ backend on 127.0.0.1.',
                fix: 'Ensure forge_server is running on local port',
                type: 'warn'
            });
        }
    }
}

document.addEventListener('DOMContentLoaded', () => {
    const app = new ForgeApp();
    app.init();
});
