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

