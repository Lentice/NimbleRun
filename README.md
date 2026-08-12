# NimbleRun

NimbleRun is a lightweight Windows app drawer: a grid of frequently used app icons with instant app-name search.

The MVP is intentionally narrow:

- C++20 and native Win32.
- Direct2D/DirectWrite for the launcher surface.
- Windows Shell for app discovery, icons, and launching.
- Local-only data; no network, telemetry, or third-party runtime dependency.
- Portable ZIP distribution.

## Status

The repository is mid-MVP (Phase 5 release gate, see `docs/roadmap.md`). The current executable is a real launcher: multi-source catalog with watcher-driven refresh, lazy icon store, search with usage ranking, pinning, settings, tray menu, and native cell tooltips. It is not yet a released product — release evidence (`docs/release-evidence.md`) is INCOMPLETE until the NFR-001 resource gates are measured.

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
