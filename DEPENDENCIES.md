# Dependencies

This document records every third-party dependency, pinned version, license, source URL, and rationale used in the Forge engineering workbench.

## C++ Core and Server Dependencies

| Name | Pinned Version | License | Source / Upstream | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **doctest** | `v2.4.11` | MIT | [github.com/doctest/doctest](https://github.com/doctest/doctest) | Fast, modern C++17 unit-testing framework with zero overhead. |
| **nlohmann/json** | `v3.11.3` | MIT | [github.com/nlohmann/json](https://github.com/nlohmann/json) | Header-only JSON serialization for server routes and job telemetry. (Used strictly in `server/`, never in `core/`). |
| **cpp-httplib** | `v0.18.3` | MIT | [github.com/yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) | Lightweight C++ HTTP/HTTPS server library supporting SSE. (Used strictly in `server/`, never in `core/`). |

## Desktop Launcher Dependencies

| Name | Pinned Version | License | Source / Upstream | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **Microsoft WebView2** | Windows 11 System Evergreen | Proprietary / Redistributable | Microsoft Windows OS | Embedded modern web renderer without bundled browser binaries. |

## Frontend Dependencies (Bundled Offline)

| Name | Pinned Version | License | Source / Upstream | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **Three.js** | `0.170.0` | MIT | [threejs.org](https://threejs.org/) | WebGL 3D rendering for STL geometry and CSV temperature point clouds. |
| **uPlot** | `1.6.31` | MIT | [github.com/leeoniya/uPlot](https://github.com/leeoniya/uPlot) | High-performance 2D charting for thermal profiles, convergence log-log plots, and benchmark results. |
| **Inter** | `v4.1` | SIL Open Font License 1.1 | [rsms.me/inter](https://rsms.me/inter/) | Bundled UI font (no runtime network/CDN requests). |
| **JetBrains Mono** | `v2.304` | SIL Open Font License 1.1 | [jetbrains.com/lp/mono](https://www.jetbrains.com/lp/mono/) | Bundled monospace font for code, telemetry, and numerical data. |
| **Vite** | `5.4.10` | MIT | [vitejs.dev](https://vitejs.dev/) | Frontend asset bundler compiling TypeScript and static assets into offline bundle `dist/`. |
| **TypeScript** | `5.6.3` | Apache-2.0 | [typescriptlang.org](https://www.typescriptlang.org/) | Static type system for UI views and API clients. |

## Compliance Rules
- Every third-party library is under a permissive open-source license (MIT, OFL, Apache 2.0).
- No runtime calls to external CDNs or cloud endpoints; all assets and fonts are bundled locally.
- `core/` contains pure C++17 standard library code with zero transport, JSON, or UI dependencies.
