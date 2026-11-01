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
