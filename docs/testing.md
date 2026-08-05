# Testing Guide

## Automated checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The current unit test covers the pure search/ranking module. Keep catalog, storage, and scoring tests free of HWND and Shell COM dependencies wherever possible.

## Manual smoke test for the Phase 0 probe

1. Start `NimbleRun.exe` from the Release output directory.
2. Press `Alt+Space`.
3. Confirm an English popup appears near the cursor and renders the fake app grid.
4. Press `Esc` or click outside the window to hide it.
5. Start the executable a second time and confirm the existing instance is brought forward.
6. Leave the app hidden for 15 minutes and record CPU time and working-set behavior.

When testing a conflicting or Windows-reserved shortcut, confirm that NimbleRun rejects it, leaves the native shortcut unchanged, keeps any previous working shortcut, and shows only one non-blocking reminder with a settings entry.

## MVP acceptance checklist

- [ ] Hotkey focuses the search field and does not leak input to the previous foreground app.
- [ ] Conflicting or Windows-reserved shortcuts are rejected without intercepting native input.
- [ ] Empty search shows pins followed by usage-ranked apps without duplicates.
- [ ] Non-empty search filters only launchable apps in the same grid.
- [ ] Arrow keys move through the grid and `Enter` launches the selected app.
- [ ] Win32 Start Menu and AppsFolder packaged apps can be discovered and launched.
- [ ] Multiple configured local folders honor each folder's recursive setting and scan only the selected executable extension allowlist.
- [ ] Start Menu or configured-folder changes refresh the catalog after the debounce window without rescanning on every panel show.
- [ ] Startup can show a valid cached catalog while the first background rebuild is running; `Ctrl+R` forces a full rebuild.
- [ ] AppsFolder refreshes on panel show only when its last successful enumeration is older than 10 minutes.
- [ ] Corrupt shortcuts, icon failures, and corrupt caches do not crash the process.
- [ ] High contrast, light/dark mode, keyboard navigation, and 100/150/200% DPI work.
- [ ] Core flows work without network access or administrator rights.
- [ ] Release measurements pass the blocking thresholds in `docs/performance-baseline.md`.

## Required test environments

Use at least one Windows 11 x64 development machine and one lower-end or virtualized Windows 11 x64 environment. Include synthetic catalogs of 100, 500, and 2,000 entries and test at 100%, 150%, and 200% DPI.
