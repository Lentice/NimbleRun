# NimbleRun Agent Instructions

## Project intent

NimbleRun is a lightweight Windows app drawer. The MVP targets Windows 10 22H2 and Windows 11 x64, using C++20, native Win32, Direct2D, DirectWrite, and Windows Shell APIs.

The design specification in `docs/design-spec.md` is the product source of truth. Do not add features that are listed as out of scope there.

## Language rules

- Conversation with the user: Traditional Chinese.
- Project documentation may use Traditional Chinese.
- NimbleRun application UI text must be English.
- Code, identifiers, test names, and diagnostic event names use English.

## Engineering rules

- Read the relevant design-spec section and trace existing callers before changing shared code.
- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Use the C++ standard library or Win32 native APIs before adding dependencies.
- Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.
- Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- Launch apps through Windows Shell APIs. Never build an arbitrary command line from search input.
- Keep all user data under `%LOCALAPPDATA%\\NimbleRun`; do not write beside the executable.
- Do not add network access, telemetry, third-party runtime dependencies, services, drivers, or administrator requirements.
- Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.
- UI strings are English and should be centralized when more than one screen needs them.
- New non-trivial logic needs one focused runnable test or self-check.

## Current baseline

The repository currently contains the Phase 0 foundation:

- CMake project for a Windows GUI executable.
- Native popup window with a global `Alt+Space` hotkey.
- Direct2D/DirectWrite rendering probe with an English fake app grid.
- Pure catalog search/ranking module with a small unit test.
- Project-local development, testing, performance, and roadmap documents.

Do not treat the probe UI as the finished product. Implement the vertical slices in the roadmap incrementally.

## Validation

From a shell with LLVM-MinGW, CMake, and Ninja on `PATH`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Before release, validate the acceptance criteria and resource budgets in `docs/testing.md` and `docs/performance-baseline.md` with a Release x64 build.

## Safety boundaries

- Do not push branches, publish releases, or modify production systems without explicit approval.
- Do not add schema migrations or destructive data cleanup as part of unrelated changes.
- Keep changes scoped to the requested task and update the relevant documentation when behavior changes.
