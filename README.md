# Forge: Unified Engineering Workbench

Forge is a high-performance Windows 11 x64 engineering workbench engineered for the Pass Science Works R&D selection assignment.

Forge integrates four core engineering views into a single unified workspace:
1. **Geometry View**: 3D visualization of ASCII STL models and ANSYS-style CSV nodal temperature point clouds with uniform perceptual false-color mapping.
2. **Thermal Analysis View**: Steady-state 1D finite-element method (FEM) thermal solver with exact analytical comparison, quadratic interpolation error verification, and mesh convergence analysis.
3. **Performance View**: Multi-threaded parameter sweep benchmarking comparing serial execution vs bounded worker thread pool, reporting speedup, efficiency, and system metrics.
4. **Hardware View**: Modular host-connected interface and telemetry viewer for embedded hardware targets (MCU, FPGA, JTAG).

---

## Architecture & System Design
- **Core Library (`src/core/`)**: Pure C++17 numerical computation, file parsers, and multithreaded workers. Free of all transport, HTTP, JSON, or GUI headers.
- **Server (`src/server/`)**: Local HTTP/SSE server powered by `cpp-httplib` and `nlohmann/json`, bound to `127.0.0.1` on an ephemeral free port.
- **Launcher (`src/launcher/`)**: Native Win32 desktop application that launches the backend server and hosts the user interface via WebView2 with default browser fallback.
- **Frontend (`src/frontend/`)**: Modern TypeScript / Vite interface using Three.js for WebGL 3D rendering and uPlot for high-frequency charts.

---

## Building from Source

### Prerequisites
- CMake >= 3.21
- MSVC 2022 (C++17 or later) on Windows, or GCC 11+ / Clang 14+ on Linux/macOS.
- Git (for FetchContent dependency retrieval).

### Configure, Build, and Run Tests
```powershell
# 1. Configure CMake
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 2. Build all targets (forge_core, forge_server, forge_tests, forge_launcher)
cmake --build build --config Release

# 3. Run test suite
ctest --test-dir build -C Release --output-on-failure
```

---

## One-Click Execution
After building or extracting the release archive, simply double-click `Launch.exe`.
No administrative rights, terminal commands, or internet connectivity are required.
