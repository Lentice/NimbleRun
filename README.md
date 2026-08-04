# NimbleRun

NimbleRun is a lightweight Windows app drawer: a grid of frequently used app icons with instant app-name search.

The MVP is intentionally narrow:

- C++20 and native Win32.
- Direct2D/DirectWrite for the launcher surface.
- Windows Shell for app discovery, icons, and launching.
- Local-only data; no network, telemetry, or third-party runtime dependency.
- Portable ZIP distribution.

## Status

The repository is set up with the Phase 0 foundation described in `docs/roadmap.md`. The current executable is a rendering and hotkey probe, not the finished launcher.

## Requirements

- Windows 10 22H2 or Windows 11 x64.
- LLVM-MinGW x64 toolchain.
- CMake 3.25 or newer.
- Ninja.

## Build

Run these commands from a shell where LLVM-MinGW, CMake, and Ninja are on `PATH`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The executable is produced at `build/NimbleRun.exe`.

## Layout

```text
NimbleRun/
├── CMakeLists.txt
├── AGENTS.md
├── cmake/
│   └── llvm-mingw.cmake
├── docs/
│   ├── design-spec.md
│   ├── development.md
│   ├── performance-baseline.md
│   ├── roadmap.md
│   └── testing.md
├── src/
│   ├── app_host/
│   ├── catalog/
│   ├── resources/
│   └── search/
└── tests/unit/
```

Read `AGENTS.md` before changing the project and `docs/design-spec.md` before making product decisions.
