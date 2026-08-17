# Testing Guide

## Automated checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest --test-dir build -N` lists the registered suite; the live count is the single source of truth (it is not hardcoded here because the number drifted twice before). They fall into three categories:

- **Unit tests** (`tests/unit/*.cpp`): the bulk of the suite, one executable per library, covering the pure search, ranking, catalog, storage, settings, icons, pins, usage, and diagnostics logic. The `nimblerun_catalog_watcher_test` is the exception: it compiles `src/app_host/catalog_watcher.cpp` directly because the watcher is part of the executable, not a library. Keep catalog, storage, and scoring tests free of HWND and Shell COM dependencies wherever possible.
- **Integration check** (`tests/integration/lifecycle_check.ps1`): registered as the `nimblerun_lifecycle_check` test; launches a real `NimbleRun.exe`, verifies the single-instance wake-up, and the tray Exit terminates cleanly. Also runs under `ctest`.
- **Release evidence** (`tests/release/release_evidence.ps1`): builds, runs the full suite, records process smoke context, and regenerates `docs/release-evidence.md`. The report has one row for every NFR-001 blocking metric; any row that is not measured with its required profile makes the result `INCOMPLETE` and the runner exits non-zero. Run manually before a release with `pwsh -NoProfile -File tests/release/release_evidence.ps1`; it is not registered as a `ctest` test.

## Manual smoke test

1. Press `Alt+Space`. Expected: the panel appears centered in the work area of the monitor under the cursor.
2. Leave the search box empty. Expected: the pinned / recent grid is shown, containing only pins and recents; if there are neither, the panel shows a one-line hint instead of filling the grid with other apps.
3. Type a query. Expected: the view switches to a search list; `Enter` launches the selected item and the panel hides.
4. Press `Esc`. Expected: the search box clears; a second `Esc` hides the panel.
5. Right-click an item and choose Pin. Expected: the item is pinned; a restart keeps it pinned, and Unpin works the same way.
6. Open the tray menu. Expected: all four entries (Open, Settings, About, Exit) work.
7. Open the panel at 200% DPI. Expected: the layout and the icons render correctly. First-show sizing and centering must match a later monitor move at the same DPI: press `Alt+Space` once (the first display) at 100%, 150%, and 200%, verify the panel is centered with the correct DIP layout, then move it to another monitor at the same DPI and confirm the size and centering are identical.

When testing a conflicting or Windows-reserved shortcut, confirm that NimbleRun rejects it, leaves the native shortcut unchanged, keeps any previous working shortcut, and shows only one non-blocking reminder with a settings entry.

## English input mode manual verification (NR-190)

The setting "Switch input to English on show" (`english_input_on_show`, default off) switches the **IME input mode** (composing/English) of NimbleRun's own search box — it does **not** change the Windows keyboard layout. Verify the distinction: pressing `Win+Space` or `Alt+Shift` changes the keyboard layout in every app and is outside NimbleRun's behavior; switching the IME input mode here must not move the layout for other apps.

1. **Setting off**: with the setting unchecked, warm-start with a Chinese IME active. Press `Alt+Space`; the search box stays in the IME's previous Chinese mode and typed Latin letters compose as usual.
2. **Setting on, hidden→visible**: check the setting, open Settings → OK (no restart needed). Switch the search IME to Chinese, hide the panel, then press `Alt+Space`. Expected: the box first gets focus, then switches to English/alphanumeric, and Latin letters enter directly.
3. **Repeated show while visible**: with the panel visible and English mode active, switch back to Chinese manually, then trigger another show request (hotkey while visible hides; use tray Open or a second instance). Expected: the visible panel is not switched back to English by the repeat show; only a real hide→show switches again.
4. **Entry points**: repeat step 2 with each of (a) the global hotkey, (b) tray left-click, (c) tray menu Open, (d) launching a second instance. All must switch once on the hide→show transition.
5. **Two IMEs**: run step 2 with Microsoft New Phonetic (Chinese) and at least one other locally installed window IME. An IME that ignores the public TSF/IMM32 mode switch must not crash or block the panel (safe no-op).
6. **No active IME context** (English-only system, or an IME-less session): enabling the setting must not change any keyboard layout, show no error dialog, and the panel must still appear normally.

## MVP acceptance checklist

- [ ] Hotkey focuses the search field and does not leak input to the previous foreground app.
- [ ] Conflicting or Windows-reserved shortcuts are rejected without intercepting native input.
- [ ] Empty search shows pins followed by usage-ranked apps without duplicates.
- [ ] Non-empty search filters only launchable apps and switches to the single-column search list.
- [ ] Arrow keys move through the empty-state grid; in the search list they move row by row, and `Enter` launches the selected app in both.
- [ ] Win32 Start Menu and AppsFolder packaged apps can be discovered and launched.
- [ ] Multiple configured local folders honor each folder's recursive setting and scan only the selected executable extension allowlist.
- [ ] Start Menu or configured-folder changes refresh the catalog after the debounce window without rescanning on every panel show.
- [ ] Startup can show a valid cached catalog while the first background rebuild is running; `Ctrl+R` forces a full rebuild.
- [ ] AppsFolder refreshes on panel show only when its last successful enumeration is older than 10 minutes.
- [ ] Corrupt shortcuts, icon failures, and corrupt caches do not crash the process.
- [ ] High contrast, light/dark mode, keyboard navigation, and 100/150/200% DPI work.
- [ ] Core flows work without network access or administrator rights.
- [ ] Release measurements pass every blocking threshold in `docs/performance-baseline.md`; an `INCOMPLETE` evidence report is not a release pass.

## Required test environments

Use at least one Windows 11 x64 development machine and one lower-end or virtualized Windows 11 x64 environment. Include synthetic catalogs of 100, 500, and 2,000 entries and test at 100%, 150%, and 200% DPI.
