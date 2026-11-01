import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { apiClient, StlMeshResponse, NodalCsvResponse, ApiError } from '../api/client';
import { toast } from '../components/toast';

export type ColormapType = 'viridis' | 'inferno' | 'coolwarm';

interface ColormapStop {
    t: number;
    r: number;
    g: number;
    b: number;
}

const COLORMAPS: Record<ColormapType, ColormapStop[]> = {
    viridis: [
        { t: 0.0, r: 0.267, g: 0.004, b: 0.329 },
        { t: 0.25, r: 0.231, g: 0.322, b: 0.545 },
        { t: 0.5, r: 0.129, g: 0.569, b: 0.549 },
        { t: 0.75, r: 0.369, g: 0.788, b: 0.384 },
        { t: 1.0, r: 0.992, g: 0.906, b: 0.145 }
    ],
    inferno: [
        { t: 0.0, r: 0.0, g: 0.0, b: 0.016 },
        { t: 0.25, r: 0.341, g: 0.063, b: 0.431 },
        { t: 0.5, r: 0.733, g: 0.216, b: 0.329 },
        { t: 0.75, r: 0.976, g: 0.557, b: 0.035 },
        { t: 1.0, r: 0.988, g: 1.0, b: 0.643 }
    ],
    coolwarm: [
        { t: 0.0, r: 0.231, g: 0.298, b: 0.753 },
        { t: 0.5, r: 0.867, g: 0.867, b: 0.867 },
        { t: 1.0, r: 0.706, g: 0.016, b: 0.150 }
    ]
};

function interpolateColormap(t: number, stops: ColormapStop[]): [number, number, number] {
    const clamped = Math.max(0, Math.min(1, t));
    for (let i = 0; i < stops.length - 1; i++) {
        if (clamped >= stops[i].t && clamped <= stops[i + 1].t) {
            const span = stops[i + 1].t - stops[i].t;
            const frac = span > 0 ? (clamped - stops[i].t) / span : 0;
            return [
                stops[i].r + frac * (stops[i + 1].r - stops[i].r),
                stops[i].g + frac * (stops[i + 1].g - stops[i].g),
                stops[i].b + frac * (stops[i + 1].b - stops[i].b)
            ];
        }
    }
    const last = stops[stops.length - 1];
    return [last.r, last.g, last.b];
}

export class GeometryView {
    private canvasContainer: HTMLElement;
    private inspectorPanel: HTMLElement;

    private scene: THREE.Scene | null = null;
    private camera: THREE.PerspectiveCamera | null = null;
    private renderer: THREE.WebGLRenderer | null = null;
    private controls: OrbitControls | null = null;
    private resizeObserver: ResizeObserver | null = null;
    private animFrameId: number | null = null;

    private stlMesh: THREE.Mesh | null = null;
    private stlWireframe: THREE.LineSegments | null = null;
    private pointsMesh: THREE.Points | null = null;
    private highlightMarker: THREE.Mesh | null = null;

    private stlData: StlMeshResponse | null = null;
    private csvData: NodalCsvResponse | null = null;

    private currentColormap: ColormapType = 'viridis';
    private pointSize: number = 3.5;
    private showMesh: boolean = true;
    private showWireframe: boolean = false;
    private showPoints: boolean = true;

    private raycaster = new THREE.Raycaster();
    private pointer = new THREE.Vector2();

    constructor(canvasContainer: HTMLElement, inspectorPanel: HTMLElement) {
        this.canvasContainer = canvasContainer;
        this.inspectorPanel = inspectorPanel;
    }

    render(): void {
        this.renderCanvas();
        this.renderInspector();
        this.initThree();
        this.bindEvents();
    }

    private renderCanvas(): void {
        this.canvasContainer.innerHTML = `
            <div id="geometry-canvas-wrapper" style="position: relative; width: 100%; height: 100%; background: #070b19; overflow: hidden;">
                <!-- 3D Canvas element -->
                <div id="three-viewport" style="width: 100%; height: 100%;"></div>

                <!-- Top Floating Controls Toolbar -->
                <div style="position: absolute; top: 16px; left: 16px; display: flex; gap: 8px; align-items: center; background: rgba(18, 26, 51, 0.85); backdrop-filter: blur(8px); padding: 6px 12px; border-radius: var(--radius-sm); border: 1px solid var(--line); z-index: 10;">
                    <button id="btn-fit-view" class="btn-secondary" style="padding: 4px 10px; font-size: 12px;" title="Frame and center geometry">
                        🔍 Fit View
                    </button>
                    <button id="btn-reset-view" class="btn-secondary" style="padding: 4px 10px; font-size: 12px;" title="Reset isometric camera">
                        ↺ Reset
                    </button>
                    <div style="width: 1px; height: 16px; background: var(--line);"></div>
                    <label style="display: flex; align-items: center; gap: 6px; font-size: 11px; cursor: pointer; color: var(--text);">
                        <input type="checkbox" id="chk-tb-mesh" checked />
                        <span>Mesh</span>
                    </label>
                    <label style="display: flex; align-items: center; gap: 6px; font-size: 11px; cursor: pointer; color: var(--text);">
                        <input type="checkbox" id="chk-tb-wireframe" />
                        <span>Wireframe</span>
                    </label>
                    <label style="display: flex; align-items: center; gap: 6px; font-size: 11px; cursor: pointer; color: var(--text);">
                        <input type="checkbox" id="chk-tb-points" checked />
                        <span>Points</span>
                    </label>
                </div>

                <!-- Drag & Drop Overlay Indicator -->
                <div id="drop-target-overlay" style="display: none; position: absolute; inset: 0; background: rgba(6, 182, 212, 0.15); border: 2px dashed var(--cyan); pointer-events: none; align-items: center; justify-content: center; font-weight: 600; font-size: 16px; color: var(--cyan); z-index: 20;">
                    Drop ASCII STL or Nodal CSV file here
                </div>

                <!-- Colormap Legend Overlay (Bottom Right) -->
                <div id="colormap-legend" class="hidden" style="position: absolute; bottom: 20px; right: 20px; background: rgba(18, 26, 51, 0.9); backdrop-filter: blur(8px); border: 1px solid var(--line); border-radius: var(--radius-sm); padding: 10px 14px; min-width: 220px; z-index: 10;">
                    <div style="font-size: 11px; text-transform: uppercase; color: var(--muted); margin-bottom: 6px; font-weight: 600; letter-spacing: 0.05em; display: flex; justify-content: space-between;">
                        <span>Nodal Temperature</span>
                        <span id="legend-field-label">°C</span>
                    </div>
                    <div id="legend-gradient-bar" style="width: 100%; height: 12px; border-radius: 2px; border: 1px solid rgba(255,255,255,0.2); margin-bottom: 4px;"></div>
                    <div style="display: flex; justify-content: space-between; font-family: var(--font-mono); font-size: 11px; color: var(--text);">
                        <span id="legend-min-temp">0.0 °C</span>
                        <span id="legend-mid-temp">50.0 °C</span>
                        <span id="legend-max-temp">100.0 °C</span>
                    </div>
                </div>

                <!-- Viewport Navigation HUD (Bottom Left) -->
                <div style="position: absolute; bottom: 16px; left: 16px; pointer-events: none; font-size: 11px; color: rgba(233, 236, 255, 0.4); font-family: var(--font-mono); z-index: 10;">
                    <div>Left Drag: Orbit | Right Drag: Pan | Wheel: Zoom</div>
                </div>
            </div>
        `;
    }

    private renderInspector(): void {
        this.inspectorPanel.innerHTML = `
            <!-- Validation Error Card (Dismissible) -->
            <div id="geometry-error-card" class="card hidden" style="border-color: var(--err); background-color: rgba(248, 113, 113, 0.08); padding: 12px;">
                <div style="display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 4px;">
                    <div id="error-card-title" style="font-weight: 600; font-size: 12px; color: var(--err);">Validation Error</div>
                    <button id="btn-close-error" style="background: none; border: none; color: var(--muted); cursor: pointer; font-size: 14px;">✕</button>
                </div>
                <div id="error-card-body" style="font-family: var(--font-mono); font-size: 11px; color: var(--text); white-space: pre-wrap; line-height: 1.4;"></div>
            </div>

            <!-- Import Data & Sample Loaders -->
            <div class="card">
                <div class="card-title">
                    <span>Geometry & Results</span>
                    <span class="brand-badge">ASCII STL / CSV</span>
                </div>

                <div style="display: flex; flex-direction: column; gap: 8px;">
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px;">
                        <button id="btn-sample-stl" class="btn-secondary" style="font-size: 11px;" title="Load procedural L-bracket STL">
                            Load Bracket STL
                        </button>
                        <button id="btn-sample-csv" class="btn-secondary" style="font-size: 11px;" title="Load synthetic nodal CSV">
                            Load Nodal CSV
                        </button>
                    </div>

                    <input type="file" id="stl-file-input" accept=".stl" style="display: none;" />
                    <input type="file" id="csv-file-input" accept=".csv" style="display: none;" />

                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px;">
                        <button id="btn-upload-stl" class="btn-secondary" style="font-size: 11px;">
                            📁 Import STL...
                        </button>
                        <button id="btn-upload-csv" class="btn-secondary" style="font-size: 11px;">
                            📊 Import CSV...
                        </button>
                    </div>
                </div>
            </div>

            <!-- Visual Options & Colormap -->
            <div class="card">
                <div class="card-title">Visualization Style</div>
                
                <div style="display: flex; flex-direction: column; gap: 10px;">
                    <div class="form-group" style="margin-bottom: 0;">
                        <label class="form-label" for="sel-colormap">Perceptual Colormap</label>
                        <select id="sel-colormap" class="form-input">
                            <option value="viridis" selected>Viridis (Perceptually Uniform)</option>
                            <option value="inferno">Inferno (High-Contrast Thermal)</option>
                            <option value="coolwarm">Cool-Warm (Diverging Blue-Red)</option>
                        </select>
                    </div>

                    <div class="form-group" style="margin-bottom: 0;">
                        <div class="form-label">
                            <span>Point Size</span>
                            <span id="txt-point-size" style="font-family: var(--font-mono); color: var(--cyan);">3.5 mm</span>
                        </div>
                        <input type="range" id="range-point-size" min="1.0" max="8.0" step="0.5" value="3.5" style="width: 100%; accent-color: var(--cyan);" />
                    </div>
                </div>
            </div>

            <!-- STL Geometry Metrics Card -->
            <div class="card" id="stl-stats-card">
                <div class="card-title">STL Mesh Metrics</div>
                <div id="stl-stats-empty" style="color: var(--muted); font-size: 12px;">
                    No STL loaded. Click <em>Load Bracket STL</em> or import a file.
                </div>
                <div id="stl-stats-content" class="hidden" style="display: flex; flex-direction: column; gap: 6px; font-size: 12px;">
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Solid Name:</span>
                        <span id="stl-solid-name" style="font-family: var(--font-mono); font-weight: 600;">-</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Triangles:</span>
                        <span id="stl-triangle-count" style="font-family: var(--font-mono); font-weight: 600; color: var(--cyan);">-</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Degenerate Facets:</span>
                        <span id="stl-degenerate-count" style="font-family: var(--font-mono); font-weight: 600;">0</span>
                    </div>
                    <div style="border-top: 1px solid var(--line); padding-top: 6px; margin-top: 4px;">
                        <div style="color: var(--muted); font-size: 11px; text-transform: uppercase; margin-bottom: 2px;">Bounding Box (mm)</div>
                        <div id="stl-bbox-x" style="font-family: var(--font-mono); font-size: 11px;">X: -</div>
                        <div id="stl-bbox-y" style="font-family: var(--font-mono); font-size: 11px;">Y: -</div>
                        <div id="stl-bbox-z" style="font-family: var(--font-mono); font-size: 11px;">Z: -</div>
                        <div id="stl-bbox-diag" style="font-family: var(--font-mono); font-size: 11px; color: var(--cyan); margin-top: 2px;">Diagonal: -</div>
                    </div>
                </div>
            </div>

            <!-- Nodal Temperature Metrics Card -->
            <div class="card" id="csv-stats-card">
                <div class="card-title">Nodal Field Metrics</div>
                <div id="csv-stats-empty" style="color: var(--muted); font-size: 12px;">
                    No CSV loaded. Click <em>Load Nodal CSV</em> or import a file.
                </div>
                <div id="csv-stats-content" class="hidden" style="display: flex; flex-direction: column; gap: 6px; font-size: 12px;">
                    <!-- Rule 4 Warning Banner -->
                    <div id="csv-synthetic-badge" class="hidden" style="background: rgba(245, 158, 11, 0.15); border: 1px solid var(--warn); color: var(--warn); padding: 4px 8px; border-radius: var(--radius-sm); font-size: 11px; font-weight: 600; text-align: center; margin-bottom: 4px;">
                        ⚠️ SYNTHETIC DATA (Not an ANSYS export)
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Node Count:</span>
                        <span id="csv-node-count" style="font-family: var(--font-mono); font-weight: 600; color: var(--cyan);">-</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Min Temperature:</span>
                        <span id="csv-min-temp" style="font-family: var(--font-mono); font-weight: 600; color: #38bdf8;">-</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Max Temperature:</span>
                        <span id="csv-max-temp" style="font-family: var(--font-mono); font-weight: 600; color: #f43f5e;">-</span>
                    </div>
                </div>
            </div>

            <!-- Node Inspector Card -->
            <div class="card">
                <div class="card-title">Node Inspector</div>
                <div id="node-inspector-empty" style="color: var(--muted); font-size: 12px;">
                    Hover or click near any node point in the 3D viewport to inspect exact coordinates and temperature.
                </div>
                <div id="node-inspector-details" class="hidden" style="display: flex; flex-direction: column; gap: 4px; font-size: 12px;">
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Node ID:</span>
                        <span id="inspect-node-id" style="font-family: var(--font-mono); font-weight: 600; color: var(--cyan);">-</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Position (mm):</span>
                        <span id="inspect-node-pos" style="font-family: var(--font-mono);">-</span>
                    </div>
                    <div style="display: flex; justify-content: space-between;">
                        <span style="color: var(--muted);">Temperature:</span>
                        <span id="inspect-node-temp" style="font-family: var(--font-mono); font-weight: 700; color: #fbbf24;">-</span>
                    </div>
                </div>
            </div>
        `;
    }

    private initThree(): void {
        const viewportDiv = document.getElementById('three-viewport');
        if (!viewportDiv) return;

        const width = viewportDiv.clientWidth || 800;
        const height = viewportDiv.clientHeight || 600;

        // 1. Scene
        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x070b19);

        // 2. Camera
        this.camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 5000);
        this.camera.position.set(120, 100, 140);

        // 3. Renderer
        this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
        this.renderer.setSize(width, height);
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
        this.renderer.toneMappingExposure = 1.0;
        viewportDiv.appendChild(this.renderer.domElement);

        // 4. Controls
        this.controls = new OrbitControls(this.camera, this.renderer.domElement);
        this.controls.enableDamping = true;
        this.controls.dampingFactor = 0.05;
        this.controls.target.set(0, 0, 0);

        // 5. Lighting
        const ambientLight = new THREE.AmbientLight(0xffffff, 0.6);
        this.scene.add(ambientLight);

        const dirLight1 = new THREE.DirectionalLight(0xffffff, 1.2);
        dirLight1.position.set(150, 200, 150);
        this.scene.add(dirLight1);

        const dirLight2 = new THREE.DirectionalLight(0x06b6d4, 0.4);
        dirLight2.position.set(-150, -100, -150);
        this.scene.add(dirLight2);

        // 6. Helpers
        const gridHelper = new THREE.GridHelper(200, 20, 0x27325f, 0x121a33);
        gridHelper.position.y = -0.1;
        this.scene.add(gridHelper);

        const axesHelper = new THREE.AxesHelper(30);
        this.scene.add(axesHelper);

        // 7. Reticle highlight marker
        const markerGeo = new THREE.SphereGeometry(1.2, 16, 16);
        const markerMat = new THREE.MeshBasicMaterial({ color: 0xfacc15, wireframe: true });
        this.highlightMarker = new THREE.Mesh(markerGeo, markerMat);
        this.highlightMarker.visible = false;
        this.scene.add(this.highlightMarker);

        // 8. Resize Observer
        this.resizeObserver = new ResizeObserver((entries) => {
            for (const entry of entries) {
                const w = entry.contentRect.width;
                const h = entry.contentRect.height;
                if (w > 0 && h > 0 && this.camera && this.renderer) {
                    this.camera.aspect = w / h;
                    this.camera.updateProjectionMatrix();
                    this.renderer.setSize(w, h);
                }
            }
        });
        this.resizeObserver.observe(viewportDiv);

        // 9. Animation loop
        const animate = () => {
            this.animFrameId = requestAnimationFrame(animate);
            if (this.controls) this.controls.update();
            if (this.renderer && this.scene && this.camera) {
                this.renderer.render(this.scene, this.camera);
            }
        };
        animate();
    }

    private bindEvents(): void {
        const btnSampleStl = document.getElementById('btn-sample-stl');
        const btnSampleCsv = document.getElementById('btn-sample-csv');
        const btnFitView = document.getElementById('btn-fit-view');
        const btnResetView = document.getElementById('btn-reset-view');

        const btnUploadStl = document.getElementById('btn-upload-stl');
        const fileInputStl = document.getElementById('stl-file-input') as HTMLInputElement;

        const btnUploadCsv = document.getElementById('btn-upload-csv');
        const fileInputCsv = document.getElementById('csv-file-input') as HTMLInputElement;

        const chkTbMesh = document.getElementById('chk-tb-mesh') as HTMLInputElement;
        const chkTbWireframe = document.getElementById('chk-tb-wireframe') as HTMLInputElement;
        const chkTbPoints = document.getElementById('chk-tb-points') as HTMLInputElement;

        const selColormap = document.getElementById('sel-colormap') as HTMLSelectElement;
        const rangePointSize = document.getElementById('range-point-size') as HTMLInputElement;
        const txtPointSize = document.getElementById('txt-point-size');

        const btnCloseError = document.getElementById('btn-close-error');

        btnSampleStl?.addEventListener('click', () => this.loadSampleStl());
        btnSampleCsv?.addEventListener('click', () => this.loadSampleCsv());
        btnFitView?.addEventListener('click', () => this.fitToView());
        btnResetView?.addEventListener('click', () => this.resetCamera());

        btnUploadStl?.addEventListener('click', () => fileInputStl?.click());
        fileInputStl?.addEventListener('change', (e) => this.handleStlFileSelect(e));

        btnUploadCsv?.addEventListener('click', () => fileInputCsv?.click());
        fileInputCsv?.addEventListener('change', (e) => this.handleCsvFileSelect(e));

        btnCloseError?.addEventListener('click', () => this.clearError());

        chkTbMesh?.addEventListener('change', () => {
            this.showMesh = chkTbMesh.checked;
            if (this.stlMesh) this.stlMesh.visible = this.showMesh;
        });

        chkTbWireframe?.addEventListener('change', () => {
            this.showWireframe = chkTbWireframe.checked;
            if (this.stlWireframe) this.stlWireframe.visible = this.showWireframe;
        });

        chkTbPoints?.addEventListener('change', () => {
            this.showPoints = chkTbPoints.checked;
            if (this.pointsMesh) this.pointsMesh.visible = this.showPoints;
        });

        selColormap?.addEventListener('change', () => {
            this.currentColormap = selColormap.value as ColormapType;
            this.updatePointColors();
            this.updateLegendUI();
        });

        rangePointSize?.addEventListener('input', () => {
            this.pointSize = parseFloat(rangePointSize.value);
            if (txtPointSize) txtPointSize.textContent = `${this.pointSize.toFixed(1)} mm`;
            if (this.pointsMesh && (this.pointsMesh.material as THREE.PointsMaterial)) {
                (this.pointsMesh.material as THREE.PointsMaterial).size = this.pointSize;
            }
        });

        // 3D Canvas mouse inspection
        const viewportDiv = document.getElementById('three-viewport');
        viewportDiv?.addEventListener('mousemove', (e) => this.handlePointerMove(e));
        viewportDiv?.addEventListener('click', (e) => this.handlePointerClick(e));

        // Drag & drop support
        const wrapper = document.getElementById('geometry-canvas-wrapper');
        const overlay = document.getElementById('drop-target-overlay');

        wrapper?.addEventListener('dragover', (e) => {
            e.preventDefault();
            if (overlay) overlay.style.display = 'flex';
        });

        wrapper?.addEventListener('dragleave', (e) => {
            e.preventDefault();
            if (overlay) overlay.style.display = 'none';
        });

        wrapper?.addEventListener('drop', (e) => {
            e.preventDefault();
            if (overlay) overlay.style.display = 'none';
            if (e.dataTransfer && e.dataTransfer.files.length > 0) {
                const file = e.dataTransfer.files[0];
                if (file.name.toLowerCase().endsWith('.stl')) {
                    this.readAndParseStlFile(file);
                } else if (file.name.toLowerCase().endsWith('.csv')) {
                    this.readAndParseCsvFile(file);
                } else {
                    toast.show({
                        title: 'Unsupported File',
                        description: `Received "${file.name}". Expected .stl or .csv.`,
                        fix: 'Select a valid ASCII STL or ANSYS-style CSV file',
                        type: 'warn'
                    });
                }
            }
        });
    }

    private clearError(): void {
        const card = document.getElementById('geometry-error-card');
        if (card) card.classList.add('hidden');
    }

    private showError(title: string, error: ApiError | Error): void {
        const card = document.getElementById('geometry-error-card');
        const titleEl = document.getElementById('error-card-title');
        const bodyEl = document.getElementById('error-card-body');

        if (card && titleEl && bodyEl) {
            card.classList.remove('hidden');
            const line = (error as ApiError).line_number;
            titleEl.textContent = line !== undefined ? `${title} (Line ${line})` : title;

            let msg = error.message;
            if ((error as ApiError).hint) {
                msg += `\nFix: ${(error as ApiError).hint}`;
            }
            bodyEl.textContent = msg;
        }

        toast.show({
            title,
            description: error.message,
            fix: (error as ApiError).hint,
            type: 'error'
        });
    }

    private async loadSampleStl(): Promise<void> {
        this.clearError();
        try {
            const data = await apiClient.loadSampleStl();
            this.applyStlMesh(data);
            toast.show({
                title: 'STL Loaded',
                description: `Successfully loaded sample bracket (${data.triangle_count} triangles).`,
                type: 'success'
            });
        } catch (err: unknown) {
            this.showError('STL Load Failed', err as ApiError);
        }
    }

    private async loadSampleCsv(): Promise<void> {
        this.clearError();
        try {
            const data = await apiClient.loadSampleCsv();
            this.applyNodalCsv(data);
            toast.show({
                title: 'CSV Loaded',
                description: `Loaded ${data.node_count} nodes with temperature range [${data.min_temperature_c.toFixed(1)}, ${data.max_temperature_c.toFixed(1)}] °C.`,
                type: 'success'
            });
        } catch (err: unknown) {
            this.showError('CSV Load Failed', err as ApiError);
        }
    }

    private handleStlFileSelect(e: Event): void {
        const input = e.target as HTMLInputElement;
        if (input.files && input.files.length > 0) {
            this.readAndParseStlFile(input.files[0]);
        }
        input.value = '';
    }

    private handleCsvFileSelect(e: Event): void {
        const input = e.target as HTMLInputElement;
        if (input.files && input.files.length > 0) {
            this.readAndParseCsvFile(input.files[0]);
        }
        input.value = '';
    }

    private async readAndParseStlFile(file: File): Promise<void> {
        this.clearError();
        try {
            const text = await file.text();
            const data = await apiClient.uploadStl(text);
            this.applyStlMesh(data);
            toast.show({
                title: 'STL Imported',
                description: `Imported "${file.name}" with ${data.triangle_count} facets.`,
                type: 'success'
            });
        } catch (err: unknown) {
            this.showError(`Error in ${file.name}`, err as ApiError);
        }
    }

    private async readAndParseCsvFile(file: File): Promise<void> {
        this.clearError();
        try {
            const text = await file.text();
            const data = await apiClient.uploadCsv(text);
            this.applyNodalCsv(data);
            toast.show({
                title: 'CSV Imported',
                description: `Imported "${file.name}" with ${data.node_count} nodes.`,
                type: 'success'
            });
        } catch (err: unknown) {
            this.showError(`Error in ${file.name}`, err as ApiError);
        }
    }

    private applyStlMesh(data: StlMeshResponse): void {
        if (!this.scene) return;
        this.stlData = data;

        if (this.stlMesh) {
            this.scene.remove(this.stlMesh);
            this.stlMesh.geometry.dispose();
            (this.stlMesh.material as THREE.Material).dispose();
            this.stlMesh = null;
        }
        if (this.stlWireframe) {
            this.scene.remove(this.stlWireframe);
            this.stlWireframe.geometry.dispose();
            (this.stlWireframe.material as THREE.Material).dispose();
            this.stlWireframe = null;
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(data.vertices), 3));

        if (data.normals && data.normals.length === data.vertices.length) {
            geometry.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(data.normals), 3));
        } else {
            geometry.computeVertexNormals();
        }

        const material = new THREE.MeshStandardMaterial({
            color: 0x38bdf8,
            roughness: 0.35,
            metalness: 0.25,
            side: THREE.DoubleSide
        });

        this.stlMesh = new THREE.Mesh(geometry, material);
        this.stlMesh.visible = this.showMesh;
        this.scene.add(this.stlMesh);

        const wireframeGeometry = new THREE.WireframeGeometry(geometry);
        const wireframeMaterial = new THREE.LineBasicMaterial({
            color: 0x0284c7,
            linewidth: 1,
            transparent: true,
            opacity: 0.4
        });
        this.stlWireframe = new THREE.LineSegments(wireframeGeometry, wireframeMaterial);
        this.stlWireframe.visible = this.showWireframe;
        this.scene.add(this.stlWireframe);

        this.updateStlUI(data);
        this.fitToView();
    }

    private updateStlUI(data: StlMeshResponse): void {
        const emptyEl = document.getElementById('stl-stats-empty');
        const contentEl = document.getElementById('stl-stats-content');
        const solidNameEl = document.getElementById('stl-solid-name');
        const triCountEl = document.getElementById('stl-triangle-count');
        const degenCountEl = document.getElementById('stl-degenerate-count');
        const bboxX = document.getElementById('stl-bbox-x');
        const bboxY = document.getElementById('stl-bbox-y');
        const bboxZ = document.getElementById('stl-bbox-z');
        const bboxDiag = document.getElementById('stl-bbox-diag');

        emptyEl?.classList.add('hidden');
        contentEl?.classList.remove('hidden');

        if (solidNameEl) solidNameEl.textContent = data.solid_name || '(unnamed)';
        if (triCountEl) triCountEl.textContent = data.triangle_count.toLocaleString();
        if (degenCountEl) {
            degenCountEl.textContent = data.degenerate_count.toString();
            degenCountEl.style.color = data.degenerate_count > 0 ? 'var(--warn)' : 'var(--text)';
        }

        const bb = data.bounding_box;
        if (bboxX) bboxX.textContent = `X: [${bb.min_x.toFixed(2)}, ${bb.max_x.toFixed(2)}]`;
        if (bboxY) bboxY.textContent = `Y: [${bb.min_y.toFixed(2)}, ${bb.max_y.toFixed(2)}]`;
        if (bboxZ) bboxZ.textContent = `Z: [${bb.min_z.toFixed(2)}, ${bb.max_z.toFixed(2)}]`;
        if (bboxDiag) bboxDiag.textContent = `Diagonal: ${bb.diagonal.toFixed(2)} mm`;
    }

    private createCircleTexture(): THREE.Texture {
        const canvas = document.createElement('canvas');
        canvas.width = 64;
        canvas.height = 64;
        const ctx = canvas.getContext('2d');
        if (ctx) {
            const grad = ctx.createRadialGradient(32, 32, 0, 32, 32, 32);
            grad.addColorStop(0, 'rgba(255,255,255,1.0)');
            grad.addColorStop(0.7, 'rgba(255,255,255,0.85)');
            grad.addColorStop(1, 'rgba(255,255,255,0.0)');
            ctx.fillStyle = grad;
            ctx.fillRect(0, 0, 64, 64);
        }
        return new THREE.CanvasTexture(canvas);
    }

    private applyNodalCsv(data: NodalCsvResponse): void {
        if (!this.scene) return;
        this.csvData = data;

        if (this.pointsMesh) {
            this.scene.remove(this.pointsMesh);
            this.pointsMesh.geometry.dispose();
            (this.pointsMesh.material as THREE.Material).dispose();
            this.pointsMesh = null;
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(data.positions), 3));

        const colors = new Float32Array(data.temperatures.length * 3);
        const stops = COLORMAPS[this.currentColormap];
        const tMin = data.min_temperature_c;
        const tRange = Math.max(1e-6, data.max_temperature_c - data.min_temperature_c);

        for (let i = 0; i < data.temperatures.length; i++) {
            const normalized = (data.temperatures[i] - tMin) / tRange;
            const [r, g, b] = interpolateColormap(normalized, stops);
            colors[i * 3 + 0] = r;
            colors[i * 3 + 1] = g;
            colors[i * 3 + 2] = b;
        }

        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));

        const material = new THREE.PointsMaterial({
            size: this.pointSize,
            vertexColors: true,
            map: this.createCircleTexture(),
            transparent: true,
            alphaTest: 0.05,
            sizeAttenuation: true
        });

        this.pointsMesh = new THREE.Points(geometry, material);
        this.pointsMesh.visible = this.showPoints;
        this.scene.add(this.pointsMesh);

        this.updateCsvUI(data);
        this.updateLegendUI();
        this.fitToView();
    }

    private updatePointColors(): void {
        if (!this.pointsMesh || !this.csvData) return;
        const geometry = this.pointsMesh.geometry;
        const colors = (geometry.getAttribute('color') as THREE.BufferAttribute).array as Float32Array;
        const stops = COLORMAPS[this.currentColormap];
        const tMin = this.csvData.min_temperature_c;
        const tRange = Math.max(1e-6, this.csvData.max_temperature_c - this.csvData.min_temperature_c);

        for (let i = 0; i < this.csvData.temperatures.length; i++) {
            const normalized = (this.csvData.temperatures[i] - tMin) / tRange;
            const [r, g, b] = interpolateColormap(normalized, stops);
            colors[i * 3 + 0] = r;
            colors[i * 3 + 1] = g;
            colors[i * 3 + 2] = b;
        }

        geometry.getAttribute('color').needsUpdate = true;
    }

    private updateCsvUI(data: NodalCsvResponse): void {
        const emptyEl = document.getElementById('csv-stats-empty');
        const contentEl = document.getElementById('csv-stats-content');
        const syntheticBadge = document.getElementById('csv-synthetic-badge');
        const nodeCountEl = document.getElementById('csv-node-count');
        const minTempEl = document.getElementById('csv-min-temp');
        const maxTempEl = document.getElementById('csv-max-temp');

        emptyEl?.classList.add('hidden');
        contentEl?.classList.remove('hidden');

        if (syntheticBadge) {
            // Hard Rule 4: Explicit synthetic marking
            if (data.is_synthetic) {
                syntheticBadge.classList.remove('hidden');
            } else {
                syntheticBadge.classList.add('hidden');
            }
        }

        if (nodeCountEl) nodeCountEl.textContent = data.node_count.toLocaleString();
        if (minTempEl) minTempEl.textContent = `${data.min_temperature_c.toFixed(2)} °C`;
        if (maxTempEl) maxTempEl.textContent = `${data.max_temperature_c.toFixed(2)} °C`;
    }

    private updateLegendUI(): void {
        const legend = document.getElementById('colormap-legend');
        const gradientBar = document.getElementById('legend-gradient-bar');
        const minTemp = document.getElementById('legend-min-temp');
        const midTemp = document.getElementById('legend-mid-temp');
        const maxTemp = document.getElementById('legend-max-temp');

        if (!this.csvData) {
            legend?.classList.add('hidden');
            return;
        }

        legend?.classList.remove('hidden');
        const stops = COLORMAPS[this.currentColormap];
        const cssGradient = stops
            .map(s => `rgb(${Math.round(s.r * 255)}, ${Math.round(s.g * 255)}, ${Math.round(s.b * 255)}) ${s.t * 100}%`)
            .join(', ');

        if (gradientBar) gradientBar.style.background = `linear-gradient(to right, ${cssGradient})`;

        const tMin = this.csvData.min_temperature_c;
        const tMax = this.csvData.max_temperature_c;
        const tMid = 0.5 * (tMin + tMax);

        if (minTemp) minTemp.textContent = `${tMin.toFixed(1)} °C`;
        if (midTemp) midTemp.textContent = `${tMid.toFixed(1)} °C`;
        if (maxTemp) maxTemp.textContent = `${tMax.toFixed(1)} °C`;
    }

    private handlePointerMove(e: MouseEvent): void {
        if (!this.pointsMesh || !this.csvData || !this.camera) return;
        const viewportDiv = document.getElementById('three-viewport');
        if (!viewportDiv) return;

        const rect = viewportDiv.getBoundingClientRect();
        this.pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
        this.pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

        this.raycaster.setFromCamera(this.pointer, this.camera);
        const diag = this.csvData.bounding_box.diagonal || 100;
        this.raycaster.params.Points.threshold = Math.max(1.0, diag * 0.015);

        const intersects = this.raycaster.intersectObject(this.pointsMesh);
        if (intersects.length > 0 && intersects[0].index !== undefined) {
            this.setHoveredNode(intersects[0].index);
        }
    }

    private handlePointerClick(e: MouseEvent): void {
        if (!this.pointsMesh || !this.csvData || !this.camera) return;
        const viewportDiv = document.getElementById('three-viewport');
        if (!viewportDiv) return;

        const rect = viewportDiv.getBoundingClientRect();
        this.pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
        this.pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

        this.raycaster.setFromCamera(this.pointer, this.camera);
        const diag = this.csvData.bounding_box.diagonal || 100;
        this.raycaster.params.Points.threshold = Math.max(1.0, diag * 0.02);

        const intersects = this.raycaster.intersectObject(this.pointsMesh);
        if (intersects.length > 0 && intersects[0].index !== undefined) {
            this.setHoveredNode(intersects[0].index);
        }
    }

    private setHoveredNode(index: number): void {
        if (!this.csvData) return;

        const emptyEl = document.getElementById('node-inspector-empty');
        const detailsEl = document.getElementById('node-inspector-details');
        const nodeIdEl = document.getElementById('inspect-node-id');
        const nodePosEl = document.getElementById('inspect-node-pos');
        const nodeTempEl = document.getElementById('inspect-node-temp');

        emptyEl?.classList.add('hidden');
        detailsEl?.classList.remove('hidden');

        const id = this.csvData.node_ids[index];
        const x = this.csvData.positions[index * 3 + 0];
        const y = this.csvData.positions[index * 3 + 1];
        const z = this.csvData.positions[index * 3 + 2];
        const temp = this.csvData.temperatures[index];

        if (nodeIdEl) nodeIdEl.textContent = `#${id}`;
        if (nodePosEl) nodePosEl.textContent = `(${x.toFixed(2)}, ${y.toFixed(2)}, ${z.toFixed(2)})`;
        if (nodeTempEl) nodeTempEl.textContent = `${temp.toFixed(2)} °C`;

        if (this.highlightMarker) {
            this.highlightMarker.position.set(x, y, z);
            const diag = this.csvData.bounding_box.diagonal || 100;
            const scale = Math.max(0.8, diag * 0.012);
            this.highlightMarker.scale.set(scale, scale, scale);
            this.highlightMarker.visible = true;
        }
    }

    private fitToView(): void {
        if (!this.camera || !this.controls) return;

        const box = new THREE.Box3();
        let hasGeometry = false;

        if (this.stlMesh) {
            this.stlMesh.geometry.computeBoundingBox();
            if (this.stlMesh.geometry.boundingBox) {
                box.union(this.stlMesh.geometry.boundingBox);
                hasGeometry = true;
            }
        }

        if (this.pointsMesh) {
            this.pointsMesh.geometry.computeBoundingBox();
            if (this.pointsMesh.geometry.boundingBox) {
                box.union(this.pointsMesh.geometry.boundingBox);
                hasGeometry = true;
            }
        }

        if (!hasGeometry) {
            box.set(new THREE.Vector3(-50, -50, -50), new THREE.Vector3(50, 50, 50));
        }

        const center = new THREE.Vector3();
        box.getCenter(center);
        const size = new THREE.Vector3();
        box.getSize(size);

        const maxDim = Math.max(size.x, size.y, size.z, 20);
        const fov = this.camera.fov * (Math.PI / 180);
        let cameraDistance = Math.abs(maxDim / 2 / Math.tan(fov / 2)) * 1.5;
        cameraDistance = Math.max(cameraDistance, 30);

        const offset = new THREE.Vector3(1, 0.8, 1.2).normalize().multiplyScalar(cameraDistance);
        this.camera.position.copy(center).add(offset);
        this.camera.near = cameraDistance / 100;
        this.camera.far = cameraDistance * 20;
        this.camera.updateProjectionMatrix();

        this.controls.target.copy(center);
        this.controls.update();
    }

    private resetCamera(): void {
        if (!this.camera || !this.controls) return;
        this.camera.position.set(120, 100, 140);
        this.controls.target.set(0, 0, 0);
        this.controls.update();
    }

    getStlData(): StlMeshResponse | null {
        return this.stlData;
    }

    getCsvData(): NodalCsvResponse | null {
        return this.csvData;
    }

    destroy(): void {
        if (this.animFrameId !== null) {
            cancelAnimationFrame(this.animFrameId);
            this.animFrameId = null;
        }

        if (this.resizeObserver) {
            this.resizeObserver.disconnect();
            this.resizeObserver = null;
        }

        if (this.stlMesh) {
            this.stlMesh.geometry.dispose();
            (this.stlMesh.material as THREE.Material).dispose();
            this.stlMesh = null;
        }

        if (this.stlWireframe) {
            this.stlWireframe.geometry.dispose();
            (this.stlWireframe.material as THREE.Material).dispose();
            this.stlWireframe = null;
        }

        if (this.pointsMesh) {
            this.pointsMesh.geometry.dispose();
            (this.pointsMesh.material as THREE.Material).dispose();
            this.pointsMesh = null;
        }

        if (this.highlightMarker) {
            this.highlightMarker.geometry.dispose();
            (this.highlightMarker.material as THREE.Material).dispose();
            this.highlightMarker = null;
        }

        if (this.renderer) {
            this.renderer.dispose();
            this.renderer = null;
        }

        this.canvasContainer.innerHTML = '';
        this.inspectorPanel.innerHTML = '';
    }
}
