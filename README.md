# Forge: Unified Engineering Workbench

Forge is a high-performance Windows 11 x64 engineering workbench engineered for the Pass Science Works R&D selection assignment (100-Point Rubric).

Forge integrates four core engineering views into a single unified workspace:
1. **Geometry View**: 3D visualization of ASCII STL models and CSV nodal temperature point clouds with uniform perceptual colormaps (Viridis, Inferno, Cool-Warm), dynamic colorbar, raycast inspection, and Hard Rule 4 synthetic data notification.
2. **Thermal Analysis View**: Steady-state 1D finite-element method (FEM) thermal solver with exact analytical comparison, quadratic interpolation error verification ($qh^2/(8k)$), mesh convergence analysis ($O(h^2)$ slope = 2.000), and global energy balance conservation ($|-R_0 - R_N - qAL| < 10^{-12}$ W).
3. **Performance View**: Multi-threaded parameter sweep benchmarking comparing serial execution vs bounded worker thread pool, reporting speedup, parallel efficiency, and CSV data export.
4. **Hardware View**: Modular host-connected interface and telemetry viewer for simulated embedded hardware targets (STM32H753ZI MCU, Artix-7 FPGA, FT2232HL JTAG) with automated 3-stage exponential backoff link recovery (200 ms &rarr; 400 ms &rarr; 800 ms).

---

## Architecture & System Design
- **Core Library (`src/core/`)**: Pure C++17 numerical computation, file parsers, thread pool, backpropagation, and hardware models. **Hard Rule 5**: Free of all transport, HTTP, JSON, or GUI headers.
- **Server (`src/server/`)**: Local HTTP/SSE server powered by `cpp-httplib` and `nlohmann/json`, bound to `127.0.0.1` on an ephemeral free port with atomic file export renames.
- **Launcher (`src/launcher/`)**: Native Win32 desktop application (`Launch.exe`) that spawns the backend server with hidden console (`CREATE_NO_WINDOW`), polls `/api/health`, and hosts the UI via WebView2 or default system browser.
- **Frontend (`src/frontend/`)**: Modern TypeScript / Vite interface using Three.js for WebGL 3D rendering and uPlot for high-frequency charts. 100% offline with bundled typography (Inter & JetBrains Mono WOFF2) and zero CDN requests.

---

## Technical Documentation
All comprehensive documentation is located in `docs/`:
- **[`docs/REPORT.md`](file:///c:/Users/Vivtus/Downloads/Pass%20assignment/docs/REPORT.md)**: $\le 6$-page formal engineering report covering Tasks A–F, mathematical formulations, convergence tables, benchmark curves, and rubric audit.
- **[`docs/OWNERSHIP.md`](file:///c:/Users/Vivtus/Downloads/Pass%20assignment/docs/OWNERSHIP.md)**: Statement of authorship, provenance, AI collaboration log (prompts, suggestions challenged/rejected), and complete 100-point rubric audit.
- **[`docs/ARCHITECTURE.md`](file:///c:/Users/Vivtus/Downloads/Pass%20assignment/docs/ARCHITECTURE.md)**: Subsystem decomposition, Hard Rule 5 isolation boundaries, job lifecycle state machine, and memory ownership models.
- **[`docs/EMBEDDED.md`](file:///c:/Users/Vivtus/Downloads/Pass%20assignment/docs/EMBEDDED.md)**: Heterogeneous embedded architecture, SVG interconnect diagram, pinout/voltage tables, datasheet citations, and automated recovery protocol.
- **[`docs/DECISIONS.md`](file:///c:/Users/Vivtus/Downloads/Pass%20assignment/docs/DECISIONS.md)**: Formal architectural decision records (ADR 001–014).
- **[`docs/AI_LOG.md`](file:///c:/Users/Vivtus/Downloads/Pass%20assignment/docs/AI_LOG.md)**: Chronological prompt-by-prompt session log tracking validation, corrections, and challenge tests.
- **[`docs/backprop_handwritten.pdf`](file:///c:/Users/Vivtus/Downloads/Pass%20assignment/docs/backprop_handwritten.pdf)**: Handwritten-style mathematical derivation of multivariable chain rule, Taylor series truncation error analysis, and numerical verification table.

---

## Building from Source

### Prerequisites
- CMake >= 3.21
- MSVC 2022 Community (C++17 or later) on Windows, or GCC 11+ / Clang 14+ on Linux/macOS.
- Git (for FetchContent dependency retrieval).
- Node.js >= 18 (for compiling frontend bundle into `dist/`).

### Configure, Build, and Run Tests
```powershell
# 1. Compile frontend assets (pre-bundled in release archive)
npm --prefix src/frontend install
npm --prefix src/frontend run build

# 2. Configure CMake Release build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 3. Build all targets (forge_core, forge_server, forge_tests, forge_launcher)
cmake --build build --config Release

# 4. Run full test suite (31 test cases, 1891 assertions)
ctest --test-dir build -C Release --output-on-failure
```

---

## Double-Click Execution
After extracting `CandidateID_Workbench.zip` or compiling the repository:
1. Double-click `Launch.exe` in the root directory.
2. The launcher automatically assigns an ephemeral port, starts `forge_server.exe` in the background, waits for `/api/health`, and opens the user interface.
3. No administrative rights, installer wizards, or internet connection required. Server cleanly terminates when the launcher window is closed.
