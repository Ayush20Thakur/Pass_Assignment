# Architecture & Technical Decisions Log

This document records architectural, numerical, and engineering decisions taken across the Forge project.

## Decision 001: Toolchain and Static Runtime
- **Context**: Windows 11 x64 target requires running `Launch.exe` with no prerequisite installation of VC++ redistributables or admin rights.
- **Decision**: Configure MSVC with `/MT` (Multi-Threaded static C runtime) for Release builds, avoiding dependency on external `vcruntime140.dll` installs.
- **Verification**: Executables built in Release mode inspectable with `dumpbin /dependents` show no dependency on non-system MSVC dynamic runtimes.

## Decision 002: Module Isolation Between Core and Server
- **Context**: Master prompt Hard Rule 5 states: `core/ never includes HTTP, JSON, or UI headers. Transport lives in server/.`
- **Decision**: `forge_core` is declared as a standalone static library linking only C++17 standard library components. All JSON serialization, HTTP routes, server state, and job tracking are quarantined inside `forge_server`.
- **Verification**: Build system forbids linking `httplib` or `nlohmann_json` to `forge_core`.

## Decision 003: Deterministic Numerical Tests First
- **Context**: Hard Rule 9 states: `Write the test first for numerical code. A phase is done only when its tests pass.`
- **Decision**: `forge_tests` uses `doctest` (pinned v2.4.11) with deterministic seed and strict numerical tolerances against closed-form analytical solutions.

## Decision 004: Dirichlet Boundary Conditions via Elimination (Not Penalty)
- **Context**: Section 3.1 mandates: `Dirichlet by elimination (not penalty)`. Penalty methods pollute system condition numbers and perturb nodal values by an $O(1/\alpha)$ numerical artifact.
- **Decision**: Formulate the reduced $(N-1) \times (N-1)$ tridiagonal system for internal nodes $T_1 \dots T_{N-1}$. The boundary contributions are eliminated directly onto the first ($d_0 += (kA/h) T_0$) and last ($d_{N-2} += (kA/h) T_L$) load vector entries.
- **Verification**: Tests assert nodal error $< 10^{-10}$ °C (measured $\sim 10^{-13}$ °C), verifying that linear elements on 1D Poisson problems are nodally exact.

## Decision 005: Thomas Algorithm (TDMA) for Tridiagonal SPD System
- **Context**: Assembling a full dense matrix or invoking iterative solvers introduces unnecessary $O(N^3)$ or $O(N^2)$ overhead.
- **Decision**: Implement a specialized $O(N)$ tridiagonal Thomas algorithm. Since the assembled 1D stiffness matrix is symmetric positive definite and strictly diagonally dominant, no pivoting is required.
- **Verification**: Verified numerically for $N \in [1, 1000000]$ with zero division errors and deterministic timing.

## Decision 006: Robust Binary STL Detection and Rejection
- **Context**: Section 3.2 requires binary STL files to be recognized and rejected with the exact message: `"binary STL not supported, export ASCII"`.
- **Decision**: Validate size criteria `file_size >= 84 && (file_size - 84) % 50 == 0`, check the 32-bit facet count header at byte 80, and scan for binary non-printable control characters. Return line 1 with actionable guidance to re-export as ASCII.
- **Verification**: Tested against 84-byte binary STL fixture; verified exact message and line number without parser crashes.

## Decision 007: Synthetic CSV Provenance and Schema Invariants
- **Context**: Hard Rule 4 prohibits calling synthetic test data an ANSYS export. Section 3.2 enforces strict 5-field schema: `node_id,x_m,y_m,z_m,temperature_C`.
- **Decision**: The CSV parser parses leading `#` comments, detects `# Synthetic`, and sets `is_synthetic = true`. Any row deviating from 5 finite numerical values or containing duplicate node IDs returns a structured `Error` specifying the offending line number.
- **Verification**: Tested against valid 3-node, synthetic 925-node, 25,000-node stress test, and 6 malformed fixtures (empty, header-only, wrong header, short row, duplicate ID, NaN).

## Decision 008: Ephemeral Random Port Binding with wait_until_ready()
- **Context**: Master Prompt Stack and Section 4 require the server to bind to a random free port on `127.0.0.1` to avoid port contention.
- **Decision**: Use `httplib::Server::bind_to_any_port("127.0.0.1")` which delegates to the operating system TCP stack to allocate an available ephemeral port. The server thread calls `http_server_->wait_until_ready()` to synchronize listener initialization before returning.
- **Verification**: Integration test suite boots `ForgeServer(0)`, retrieves `get_port() > 0`, and conducts concurrent HTTP transactions over localhost.

## Decision 009: Atomic Export Writes and Fast Stop Token Cancellation
- **Context**: Runtime model mandates atomic export file writes (temp file then rename) and fast cancellation via `DELETE /api/jobs/{id}` without leaving corrupted partial files.
- **Decision**: Worker jobs write to `results/<name>.tmp` and rename atomically to `results/<name>` via `std::filesystem::rename` upon successful completion. Background workers check `job->stop_token.load()` at each chunk iteration; on cancellation, the `.tmp` file is deleted via `std::filesystem::remove`.
- **Verification**: Integration test verifies cancellation returns in $< 500$ ms and asserts that `results/convergence.csv.tmp` does not exist on disk.

## Decision 010: Offline-First Bundling with Local WOFF2 Fonts and Relative Base
- **Context**: Hard Rule 6 dictates zero CDN dependencies and complete offline capability at runtime.
- **Decision**: Local WOFF2 binaries for Inter and JetBrains Mono are placed in `src/frontend/src/assets/fonts/` and bundled via Vite with `base: './'`. Vite generates an offline distribution in `dist/` where all scripts, styles, and font references use relative paths. `ForgeServer` mounts `dist/` to serve the UI directly over localhost.
- **Verification**: Built distribution inspected; verified that no CDN links exist in HTML/CSS and that the interface loads offline without active internet connections.

## Decision 011: WebGL 3D Geometry Rendering & Perceptual Colormapping
- **Context**: Phase 5 requires interactive 3D rendering of ASCII STL facets and ANSYS nodal CSV point clouds, with perceptual colormaps, raycast node inspection, and clear synthetic provenance.
- **Decision**: Use Three.js `BufferGeometry` with `Float32Array` attributes for positions, vertex normals, and vertex colors. Implemented smooth piecewise linear colormap interpolation for Viridis, Inferno, and Cool-Warm. Generated a procedural soft circular sprite texture to render circular nodes without sharp square artifacts. Node inspection uses `THREE.Raycaster` with dynamic threshold scaling with bounding box diagonal (`diag * 0.015`), placing a 3D reticle at the hovered/selected node. Synthetic datasets are marked with a prominent amber badge in accordance with Hard Rule 4.
- **Verification**: Tested with sample L-bracket mesh (20 triangles), synthetic CSV field (925 nodes), and malformed fixtures; verified instant raycast inspection and graceful error reporting.

## Decision 012: Bounded Thread Pool, Atomic Chunking, and Deterministic Batch Solves
- **Context**: Section 3.3 mandates a batch solver over $M$ thermal problems with a bounded thread pool (avoiding `std::async` explosion), dynamic chunk counter `fetch_add(16)`, portable PRNG, and deterministic outputs matching serial solve to $\le 10^{-8}$ °C.
- **Decision**: Implemented `ThreadPool` with RAII lifetime management and worker threads bounded by hardware concurrency. Work distribution uses `std::atomic<size_t> next_chunk{0}` with relaxed `fetch_add(16)`. Random generation utilizes `std::mt19937_64(seed)` with `(r >> 11) * 2^-53` to guarantee bitwise reproducibility across compilers and platforms. Benchmark harness records 1 warm-up run and 5 measured repeats, selecting the median time $T_p$ to calculate speedup $S_p = T_1 / T_p$ and parallel efficiency $E_p = S_p / p$ for $p \in \{1, 2, 4\}$.
- **Verification**: Verified in `tests/core/batch_test.cpp` and `threadpool_test.cpp` that parallel solve matches serial to $\le 10^{-8}$ °C across 200 problem instances; verified that `results/benchmark.csv` conforms to spec.



