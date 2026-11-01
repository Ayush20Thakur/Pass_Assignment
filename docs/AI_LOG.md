# AI Assistance and Audit Log

This document tracks all AI interactions, prompts, validation results, corrections, and challenge tests in compliance with the evaluation rubric.

## Session Log Format
Each entry records:
- **Date & Tool/Model**: Date, tool (e.g., Antigravity IDE), model (e.g., Gemini 3.8 Flash).
- **Prompt**: User prompt or phase instruction given.
- **Output Summary**: Summary of code/architecture generated.
- **Edits & Validation**: Changes made, exact test commands executed, and output verified.
- **Wrong Suggestions / Challenge Test**: Specific incorrect or suboptimal suggestions caught and corrected.

---

### Phase 0: Repo Skeleton, CMake Targets, Pinned Dependencies
- **Date & Model**: 2026-11-02, Antigravity IDE (Gemini 3.8 Flash)
- **Prompt**: Phase 0 prompt (Repo skeleton, CMake targets, pinned deps via FetchContent, DEPENDENCIES.md, .clang-format, .gitignore, .vscode configs, smoke test).
- **Output Summary**: Created project structure, CMakeLists.txt with `forge_core`, `forge_server`, `forge_tests`, `forge_launcher`, pinned FetchContent dependencies (`doctest`, `nlohmann_json`, `cpp-httplib`), DEPENDENCIES.md, .clang-format, .gitignore, launch/tasks configuration, and initial smoke test.
- **Edits & Validation**: Verified clean configuration and build with MSVC 2022 and CMake 3.31.5; all targets built and smoke test passed via ctest.
- **Wrong Suggestions / Challenge Test**: Initial consideration was whether to bundle cpp-httplib headers directly in the tree or fetch via git tag; FetchContent with explicit pinned tags (`v0.18.3`, `v3.11.3`, `v2.4.11`) was enforced to adhere strictly to Rule 7 (no unpinned dependencies).

### Phase 1: FEM Core + Tests (Task C)
- **Date & Model**: 2026-11-02, Antigravity IDE (Gemini 3.8 Flash)
- **Prompt**: "Phase 1: FEM core + tests (Days 1–2)" per section 3.1.
- **Output Summary**: Implemented `src/core/fem1d.{hpp,cpp}` with exact analytical temperature, input validation, tridiagonal Thomas solver, Dirichlet by elimination, nodal and midpoint errors, reaction forces, energy balance check, convergence study, and CSV exporters. Implemented unit and numerical tests in `tests/core/fem1d_test.cpp`.
- **Edits & Validation**:
  - Test ran under CTest: initial failure occurred when `export_fem_results_csv` ran from `build/` directory because parent directory `results/` was relative to source root; fixed by ensuring parent directory creation via `std::filesystem::create_directories` with `std::error_code` (no exceptions) and setting `WORKING_DIRECTORY` in CMake test properties.
  - Test results: All 6 test cases (60 assertions) passed green. Midpoint errors for $N \in \{10, 20, 40, 80\}$ matched $q h^2 / (8k)$ to $< 10^{-12}$ °C. Observed order of accuracy = 2.000. Nodal error $< 10^{-12}$ °C. Energy balance generated $= qAL$ to $< 10^{-12}$ W. CSV files generated in `results/`.
- **Wrong Suggestions / Challenge Test**: Evaluated whether nodal error could exhibit $O(h^2)$ discretization error. Disproved and verified theoretical property: for 1D Poisson equations with constant coefficients and linear elements, the finite element solution is nodally exact (Green's function property), so nodal error is at floating-point epsilon ($\sim 10^{-13}$ °C), with all discretization error located exclusively between nodes.

### Phase 2: Parsers and Fixtures (Task B)
- **Date & Model**: 2026-11-02, Antigravity IDE (Gemini 3.8 Flash)
- **Prompt**: "Phase 2: Parsers and fixtures (Day 3)" per section 3.2.
- **Output Summary**: Implemented `src/core/stl.{hpp,cpp}`, `src/core/csv.{hpp,cpp}`, `src/core/geometry_types.hpp`, fixtures in `tests/fixtures/`, example STL in `data/cube_or_bracket.stl`, synthetic CSV in `data/nodal_temperature_synthetic.csv`, and documentation in `data/PROVENANCE.md`. Implemented test suites in `tests/core/stl_test.cpp` and `tests/core/csv_test.cpp`.
- **Edits & Validation**:
  - Validated STL parser against unit cube (12 facets, unit normals) and L-bracket (20 facets, bounding box exact). Verified exact detection of binary STL (84 bytes), empty STL, zero facets, missing endloop, and NaN vertex.
  - Validated CSV parser against 3-node reference, synthetic 925-node dataset, and 25,000-node stress test (< 250 ms parsing time). Verified rejection of empty, header-only, invalid header, short row, duplicate node ID, and NaN/Inf values with line numbers.
  - Total doctest test cases increased from 6 to 14 (201 assertions), all passing green in 0.38s.
- **Wrong Suggestions / Challenge Test**: Tested behavior when ASCII STL contains facet normals in the file versus computing facet normals from vertices. Implemented both parsing and vertex cross-product recomputation to ensure normals are guaranteed unit-length even if the file's header normals were malformed or zero, while flagging degenerate triangles (area < 1e-18).

### Phase 3: Server, Jobs, SSE (Days 3–4)
- **Date & Model**: 2026-11-02, Antigravity IDE (Gemini 3.8 Flash)
- **Prompt**: "Phase 3: Server, jobs, SSE (Days 3–4)" per section 4.
- **Output Summary**: Implemented `src/server/job_manager.{hpp,cpp}` (job state machine, atomic progress, stop token, atomic file writes), `src/server/server.{hpp,cpp}` (C++ httplib server, ephemeral free port binding on 127.0.0.1, REST routes, SSE events endpoint, atomic export downloader, graceful shutdown endpoint), updated `src/server/main.cpp`, and built integration test suite in `tests/server/server_test.cpp`.
- **Edits & Validation**:
  - Initial integration failure: `bind_to_port("127.0.0.1", 0)` in `cpp-httplib` returns a `bool` (evaluating to 1, causing connection attempts to port 1), whereas `bind_to_any_port("127.0.0.1")` returns the actual ephemeral `int` port. Corrected to `bind_to_any_port` and added `http_server_->wait_until_ready()`.
  - Empty file string trimming: Enhanced empty-input handling in STL and CSV string parsers to catch whitespace-only inputs.
  - Test results: All 17 test cases (243 assertions) passed green. Verified health endpoint, bad STL 400 error reporting with line number, thermal solve job lifecycle, atomic CSV export download, mid-run cancellation within 500 ms with zero residual `.tmp` files, and graceful shutdown.
- **Wrong Suggestions / Challenge Test**: Evaluated whether the server should perform thermal FEM solves synchronously within the HTTP handler. Rejected in compliance with Section 4 and the Runtime Model: the server returns HTTP 202 with `job_id`, updates atomic progress on background worker threads, provides real-time SSE updates, and handles cancellation cleanly without blocking the event loop.

