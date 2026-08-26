# Release Evidence

- Generated: 2026-08-25 17:40:50 +08:00 (UTC: 2026-08-25T17:40:50.4615917+08:00)
- OS: Microsoft Windows 11 專業版 build 26200
- CPU: Intel64 Family 6 Model 151 Stepping 2, GenuineIntel
- Debugger attached: False
- Git commit: e98134b119f63203decd2d5bbac03d54e2c49511
- CTest count: Total Tests: 33

## Tool versions

| Tool | Version |
|---|---|
| cmake | cmake version 4.4.2 |
| ninja | 1.13.2 |
| clang | clang version 22.1.8 (https://github.com/llvm/llvm-project.git ca7933e47d3a3451d81e72ac174dcb5aa28b59d1) |
| clang++ | clang version 22.1.8 (https://github.com/llvm/llvm-project.git ca7933e47d3a3451d81e72ac174dcb5aa28b59d1) |
| ctest | ctest version 4.4.2 |

## Conditions

- Build dir: `$buildDir`
- Build type: Release
- Toolchain: `cmake/llvm-mingw.cmake` (LLVM-MinGW, target x86_64-w64-windows-gnu)
- The blocking thresholds are the `> value` columns of `docs/performance-baseline.md`.

```text
# cmake configure
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: E:/GitHub/NimbleRun/build
exit code: 0
```
```text
# cmake build
[1/1] Linking CXX executable NimbleRun.exe
exit code: 0
```
```text
# ctest full suite
Test project E:/GitHub/NimbleRun/build
      Start  1: nimblerun_search_test
 1/33 Test  #1: nimblerun_search_test ....................   Passed    0.02 sec
      Start  2: nimblerun_hotkey_test
 2/33 Test  #2: nimblerun_hotkey_test ....................   Passed    0.02 sec
      Start  3: nimblerun_start_menu_catalog_test
 3/33 Test  #3: nimblerun_start_menu_catalog_test ........   Passed    6.75 sec
      Start  4: nimblerun_settings_test
 4/33 Test  #4: nimblerun_settings_test ..................   Passed    0.49 sec
      Start  5: nimblerun_appsfolder_catalog_test
 5/33 Test  #5: nimblerun_appsfolder_catalog_test ........   Passed    1.33 sec
      Start  6: nimblerun_app_filter_test
 6/33 Test  #6: nimblerun_app_filter_test ................   Passed    0.02 sec
      Start  7: nimblerun_user_folder_catalog_test
 7/33 Test  #7: nimblerun_user_folder_catalog_test .......   Passed    0.05 sec
      Start  8: nimblerun_identity_dedup_test
 8/33 Test  #8: nimblerun_identity_dedup_test ............   Passed    0.07 sec
      Start  9: nimblerun_shell_launch_test
 9/33 Test  #9: nimblerun_shell_launch_test ..............   Passed    0.79 sec
      Start 10: nimblerun_recent_usage_test
10/33 Test #10: nimblerun_recent_usage_test ..............   Passed    0.17 sec
      Start 11: nimblerun_list_vertical_slice_test
11/33 Test #11: nimblerun_list_vertical_slice_test .......   Passed    0.02 sec
      Start 12: nimblerun_icons_cache_test
12/33 Test #12: nimblerun_icons_cache_test ...............   Passed    0.02 sec
      Start 13: nimblerun_icon_pack_format_test
13/33 Test #13: nimblerun_icon_pack_format_test ..........   Passed    0.08 sec
      Start 14: nimblerun_icon_store_test
14/33 Test #14: nimblerun_icon_store_test ................   Passed    6.94 sec
      Start 15: nimblerun_png_codec_test
15/33 Test #15: nimblerun_png_codec_test .................   Passed    0.06 sec
      Start 16: nimblerun_icon_worker_test
16/33 Test #16: nimblerun_icon_worker_test ...............   Passed    1.30 sec
      Start 17: nimblerun_handoff_registry_test
17/33 Test #17: nimblerun_handoff_registry_test ..........   Passed    0.02 sec
      Start 18: nimblerun_icon_request_session_test
18/33 Test #18: nimblerun_icon_request_session_test ......   Passed    0.02 sec
      Start 19: nimblerun_dpi_theme_accessibility_test
19/33 Test #19: nimblerun_dpi_theme_accessibility_test ...   Passed    0.05 sec
      Start 20: nimblerun_settings_ui_test
20/33 Test #20: nimblerun_settings_ui_test ...............   Passed    0.06 sec
      Start 21: nimblerun_startup_option_test
21/33 Test #21: nimblerun_startup_option_test ............   Passed    0.04 sec
      Start 22: nimblerun_catalog_refresh_test
22/33 Test #22: nimblerun_catalog_refresh_test ...........   Passed    0.15 sec
      Start 23: nimblerun_directory_walker_test
23/33 Test #23: nimblerun_directory_walker_test ..........   Passed    0.03 sec
      Start 24: nimblerun_pinning_test
24/33 Test #24: nimblerun_pinning_test ...................   Passed    1.56 sec
      Start 25: nimblerun_diagnostic_log_test
25/33 Test #25: nimblerun_diagnostic_log_test ............   Passed    2.70 sec
      Start 26: nimblerun_hotkey_capture_test
26/33 Test #26: nimblerun_hotkey_capture_test ............   Passed    0.03 sec
      Start 27: nimblerun_snapshot_assembler_test
27/33 Test #27: nimblerun_snapshot_assembler_test ........   Passed    0.08 sec
      Start 28: nimblerun_pin_drag_state_test
28/33 Test #28: nimblerun_pin_drag_state_test ............   Passed    0.02 sec
      Start 29: nimblerun_input_mode_test
29/33 Test #29: nimblerun_input_mode_test ................   Passed    0.03 sec
      Start 30: nimblerun_lifecycle_check
30/33 Test #30: nimblerun_lifecycle_check ................   Passed    2.71 sec
      Start 31: nimblerun_catalog_watcher_test
31/33 Test #31: nimblerun_catalog_watcher_test ...........   Passed    2.09 sec
      Start 32: nimblerun_rebuild_pipeline_test
32/33 Test #32: nimblerun_rebuild_pipeline_test ..........   Passed   11.68 sec
      Start 33: nimblerun_cell_tooltip_test
33/33 Test #33: nimblerun_cell_tooltip_test ..............   Passed    0.07 sec

100% tests passed out of 33

Total Test time (real) =  39.46 sec
exit code: 0
```
## Process smoke, idle measurement, short soak

### Idle measurement (hidden at rest, sampled once)

```text
idle thread count: 16
idle working set bytes: 40087552
idle private bytes: 8200192
idle handle count: 404
```

### Short soak (3x launch/terminate)

```text
soak iteration 0: launched and terminated OK
soak iteration 1: launched and terminated OK
soak iteration 2: launched and terminated OK
```

### NFR-001 formal probe

`tests/release/nfr001_probe.ps1` ran against the same Release x64 executable with
no debugger. It waited 60 seconds after `StartupReady`, then sampled idle CPU and
memory for 900 seconds. The startup log contained completed Start Menu, AppsFolder,
and UserFolder rebuild entries before the sample was recorded.

```text
cold window-ready: 214.89 ms (not the hotkey-ready gate)
cold hotkey-ready: 220.41 ms
idle CPU, 15-minute average: 0.0013% logical CPU equivalent
idle working set peak: 39.45 MiB
idle private bytes peak: 7.84 MiB
visible panel: 20-row grid ready; working set peak 80.37 MiB
warm show -> input-ready p95: 41.54 ms (20 samples)
filter 5000 entries p95: 683 us (100 samples; 500-entry gate uses this larger profile)
app-owned thread census: 5/5 (main, icon worker, 3 watchers), verdict PASS
icons.cache: 1,376,256 bytes after the 20-row VisibleReady cycle and hide flush
final full CTest after measurement hooks: 33/33 passed, 56.11 s
```

The cold/warm/filter probe and the `HotkeyReady`/`InputReady` rendezvous were run
against the current working tree after the recorded base commit; the source changes
are not represented by the historical commit hash in the generated header.

## Blocking-threshold gate

| Metric | Blocking threshold | Measurement source | Measured | Value | Verdict |
|---|---|---|---|---|---|
| Idle CPU, 15-minute average | > 0.5% logical CPU equivalent | `nfr001_probe.ps1`, 60s settle + 900s sample | measured | 0.0013% | PASS |
| Idle working set | > 80 MiB | `nfr001_probe.ps1`, 60s settle + 900s sample | measured | 39.45 MiB peak | PASS |
| Idle private bytes | > 70 MiB | `nfr001_probe.ps1`, 60s settle + 900s sample | measured | 7.84 MiB peak | PASS |
| Visible panel with 20 icons working set | > 100 MiB | `nfr001_probe.ps1` + `VisibleReady`, 20-row cycle | measured | 80.37 MiB peak | PASS |
| Cold start to hotkey-ready | > 1,000 ms | `nfr001_probe.ps1` + `HotkeyReady` | measured | 220.41 ms | PASS |
| Warm hotkey to input-ready, p95 | p95 > 150 ms | `nfr001_probe.ps1` + `InputReady`, 20 samples | measured | 41.54 ms | PASS |
| Filter 500 apps, p95 | p95 > 16 ms | `search_engine_test`, 5,000 entries x 100 samples | measured | 683 us | PASS |
| Idle app-owned thread count | 超出 2 + watcher root 數 | `nfr001_probe.ps1`, `GetThreadDescription` census (`NimbleRun.*`) | measured | 5 app-owned / budget 5 | PASS |
| icons.cache file size | > 48 MiB | `nfr001_probe.ps1`, post-VisibleReady cycle + hide flush | measured | 1,376,256 bytes (1.31 MiB) | PASS |

### CTest gate

CTest is a separate release gate; its registration count comes from live ctest -N output.

| Metric | Threshold | Measurement source | Measured | Value | Verdict |
|---|---|---|---|---|---|
| CTest registration vs executed | registered == executed | live ctest -N vs full-suite output | measured | registered 33 vs executed 33 | PASS |
| CTest skipped tests | 0 | full-suite output | measured | none | PASS |

### Non-blocking process context

- Idle process thread count: 10 (this run's process total; varies with OS-injected worker threads). This is context only and never substitutes for the app-owned thread census.
- Idle working set/private bytes and the short soak are smoke context only; they do not satisfy the NFR-001 60-second/profile requirements.

### Thread-count attribution

The idle thread-count budget applies to app-owned threads only: 1 UI thread,
1 resident icon worker, and one directory watcher per watcher root (two
Programs known folders plus each configured custom folder), each blocking on
`ReadDirectoryChangesW` per design-spec §9.2. Catalog rebuild workers are
per-source and reclaimed on completion, so they are not part of the idle
figure.

The process total above is larger and is recorded as context, not gated: it
also counts threads Windows injects (IME `IMM32.dll`, `ntdll.dll` /
`ucrtbase.dll` threadpool and worker threads from Direct2D/DirectWrite/Shell
COM, plus display-driver device threads), whose number varies with OS build,
display driver and installed Shell extensions.

Attribution no longer uses a start-address heuristic: on this UCRT toolchain,
`std::thread` is spawned via `_beginthreadex`, which reports its own
`ucrtbase.dll` trampoline as the Win32 start address for every such thread --
identical to genuine OS-injected CRT/COM worker threads, so the address alone
cannot tell them apart (a 2026-08-26 re-measurement classified only 1 of 5
expected app threads this way). The app instead names its own threads via
`SetThreadDescription` (`NimbleRun.Main`, `NimbleRun.IconWorker`,
`NimbleRun.Watcher`); `nfr001_probe.ps1` reads the description back with
`GetThreadDescription` and counts threads whose name starts with
`NimbleRun.`. The 2026-08-26 formal run classified exactly 5 app-owned
threads (main + icon worker + 3 watchers: 2 known Programs folders + 1
configured custom root), matching the budget formula.

## Result

- **PASS (all 9 blocking NFR-001 metrics measured and within threshold)**

