# Release Evidence

- Generated: 2026-08-10 19:12:15 +08:00 (UTC: 2026-08-10T19:12:15.7745089+08:00)
- OS: Microsoft Windows 11 專業版 build 26200
- CPU: Intel64 Family 6 Model 151 Stepping 2, GenuineIntel
- Debugger attached: False
- Git commit: dcd0a6b06b0a15bae910f6f854f125843f39d47a
- CTest count: Total Tests: 31

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
-- Configuring done (0.6s)
-- Generating done (0.1s)
-- Build files have been written to: D:/Documents/GitHub/NimbleRun/build
exit code: 0
```
```text
# cmake build
ninja: no work to do.
exit code: 0
```
```text
# ctest full suite
Test project D:/Documents/GitHub/NimbleRun/build
      Start  1: nimblerun_search_test
 1/31 Test  #1: nimblerun_search_test ....................   Passed    0.03 sec
      Start  2: nimblerun_hotkey_test
 2/31 Test  #2: nimblerun_hotkey_test ....................   Passed    0.05 sec
      Start  3: nimblerun_start_menu_catalog_test
 3/31 Test  #3: nimblerun_start_menu_catalog_test ........   Passed    6.21 sec
      Start  4: nimblerun_settings_test
 4/31 Test  #4: nimblerun_settings_test ..................   Passed    0.23 sec
      Start  5: nimblerun_appsfolder_catalog_test
 5/31 Test  #5: nimblerun_appsfolder_catalog_test ........   Passed    1.28 sec
      Start  6: nimblerun_app_filter_test
 6/31 Test  #6: nimblerun_app_filter_test ................   Passed    0.03 sec
      Start  7: nimblerun_user_folder_catalog_test
 7/31 Test  #7: nimblerun_user_folder_catalog_test .......   Passed    0.11 sec
      Start  8: nimblerun_identity_dedup_test
 8/31 Test  #8: nimblerun_identity_dedup_test ............   Passed    0.04 sec
      Start  9: nimblerun_shell_launch_test
 9/31 Test  #9: nimblerun_shell_launch_test ..............   Passed    0.95 sec
      Start 10: nimblerun_recent_usage_test
10/31 Test #10: nimblerun_recent_usage_test ..............   Passed    0.20 sec
      Start 11: nimblerun_list_vertical_slice_test
11/31 Test #11: nimblerun_list_vertical_slice_test .......   Passed    0.03 sec
      Start 12: nimblerun_icons_cache_test
12/31 Test #12: nimblerun_icons_cache_test ...............   Passed    0.05 sec
      Start 13: nimblerun_icon_pack_format_test
13/31 Test #13: nimblerun_icon_pack_format_test ..........   Passed    0.10 sec
      Start 14: nimblerun_icon_store_test
14/31 Test #14: nimblerun_icon_store_test ................   Passed    7.17 sec
      Start 15: nimblerun_png_codec_test
15/31 Test #15: nimblerun_png_codec_test .................   Passed    0.07 sec
      Start 16: nimblerun_icon_worker_test
16/31 Test #16: nimblerun_icon_worker_test ...............   Passed    0.83 sec
      Start 17: nimblerun_handoff_registry_test
17/31 Test #17: nimblerun_handoff_registry_test ..........   Passed    0.03 sec
      Start 18: nimblerun_icon_request_session_test
18/31 Test #18: nimblerun_icon_request_session_test ......   Passed    0.03 sec
      Start 19: nimblerun_dpi_theme_accessibility_test
19/31 Test #19: nimblerun_dpi_theme_accessibility_test ...   Passed    0.08 sec
      Start 20: nimblerun_settings_ui_test
20/31 Test #20: nimblerun_settings_ui_test ...............   Passed    0.13 sec
      Start 21: nimblerun_startup_option_test
21/31 Test #21: nimblerun_startup_option_test ............   Passed    0.07 sec
      Start 22: nimblerun_catalog_refresh_test
22/31 Test #22: nimblerun_catalog_refresh_test ...........   Passed    0.70 sec
      Start 23: nimblerun_directory_walker_test
23/31 Test #23: nimblerun_directory_walker_test ..........   Passed    0.05 sec
      Start 24: nimblerun_pinning_test
24/31 Test #24: nimblerun_pinning_test ...................   Passed    1.64 sec
      Start 25: nimblerun_diagnostic_log_test
25/31 Test #25: nimblerun_diagnostic_log_test ............   Passed    2.26 sec
      Start 26: nimblerun_hotkey_capture_test
26/31 Test #26: nimblerun_hotkey_capture_test ............   Passed    0.06 sec
      Start 27: nimblerun_snapshot_assembler_test
27/31 Test #27: nimblerun_snapshot_assembler_test ........   Passed    0.13 sec
      Start 28: nimblerun_pin_drag_state_test
28/31 Test #28: nimblerun_pin_drag_state_test ............   Passed    0.04 sec
      Start 29: nimblerun_lifecycle_check
29/31 Test #29: nimblerun_lifecycle_check ................   Passed    3.25 sec
      Start 30: nimblerun_catalog_watcher_test
30/31 Test #30: nimblerun_catalog_watcher_test ...........   Passed    2.10 sec
      Start 31: nimblerun_rebuild_pipeline_test
31/31 Test #31: nimblerun_rebuild_pipeline_test ..........   Passed    0.25 sec

100% tests passed out of 31

Total Test time (real) =  28.27 sec
exit code: 0
```
## Process smoke, idle measurement, short soak

### Idle measurement (hidden at rest, sampled once)

```text
idle thread count: 17
idle working set bytes: 39583744
idle private bytes: 8908800
idle handle count: 403
```

### Short soak (3x launch/terminate)

```text
soak iteration 0: launched and terminated OK
soak iteration 1: launched and terminated OK
soak iteration 2: launched and terminated OK
```

## Blocking-threshold gate

| Metric | Blocking threshold | Measurement source | Measured | Value | Verdict |
|---|---|---|---|---|---|
| Idle CPU, 15-minute average | > 0.5% logical CPU equivalent | No 15-minute idle CPU sample in this runner | not measured | not measured | INCOMPLETE |
| Idle working set | > 80 MiB | The 3-second smoke sample is context only; no compliant 60-second idle profile | not measured | not measured | INCOMPLETE |
| Idle private bytes | > 70 MiB | The 3-second smoke sample is context only; no compliant 60-second idle profile | not measured | not measured | INCOMPLETE |
| Visible panel with 20 icons working set | > 100 MiB | No visible-panel 20-icon census | not measured | not measured | INCOMPLETE |
| Cold start to hotkey-ready | > 1,000 ms | No startup-to-hotkey-ready timestamp | not measured | not measured | INCOMPLETE |
| Warm hotkey to input-ready, p95 | p95 > 150 ms | No warm hotkey/input-ready latency profile | not measured | not measured | INCOMPLETE |
| Filter 500 apps, p95 | p95 > 16 ms | No 500-entry p95 profile run by this release invocation | not measured | not measured | INCOMPLETE |
| Idle app-owned thread count | 超出 2 + watcher root 數 | No start-address census; process total is context only | not measured | not measured | INCOMPLETE |
| icons.cache file size | > 48 MiB | No complete icon build followed by an actual file-size sample | not measured | not measured | INCOMPLETE |

### CTest gate

CTest is a separate release gate; its registration count comes from live ctest -N output.

| Metric | Threshold | Measurement source | Measured | Value | Verdict |
|---|---|---|---|---|---|
| CTest registration vs executed | registered == executed | live ctest -N vs full-suite output | measured | registered 31 vs executed 31 | PASS |

### Non-blocking process context

- Idle process thread count: 17. This is context only and never substitutes for the app-owned start-address census.
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
display driver and installed Shell extensions. A 2026-08-05 start-address
census confirmed the attribution: 3 app-owned threads out of 14, matching the
formula for that configuration (no icon worker, no custom root yet).

## Result

- **INCOMPLETE (one or more blocking NFR-001 metrics are not measured)**

