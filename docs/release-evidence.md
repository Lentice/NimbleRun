# Release Evidence

- Generated: 2026-08-06 21:36:34 +08:00 (UTC: 2026-08-06T21:36:34.1585182+08:00)
- OS: Microsoft Windows 11 專業版 build 26200
- CPU: Intel64 Family 6 Model 151 Stepping 2, GenuineIntel
- Debugger attached: False
- Git commit: cd0f256634edcd2510ceb3f4b929dc4a41471e30
- CTest count: Total Tests: 23

## Tool versions

| Tool | Version |
|---|---|
| cmake | Usage |
| ninja | ninja: error: loading 'build.ninja': The system cannot find the file specified. |
| clang | clang-22: error: no input files |
| clang++ | clang-22: error: no input files |
| ctest | ********************************* |

## Conditions

- Build dir: `$buildDir`
- Build type: Release
- Toolchain: `cmake/llvm-mingw.cmake` (LLVM-MinGW, target x86_64-w64-windows-gnu)
- The blocking thresholds are the `> value` columns of `docs/performance-baseline.md`.

```text
# cmake configure
-- Configuring done (1.4s)
-- Generating done (0.6s)
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
 1/23 Test  #1: nimblerun_search_test ....................   Passed    0.10 sec
      Start  2: nimblerun_hotkey_test
 2/23 Test  #2: nimblerun_hotkey_test ....................   Passed    0.03 sec
      Start  3: nimblerun_start_menu_catalog_test
 3/23 Test  #3: nimblerun_start_menu_catalog_test ........   Passed    0.82 sec
      Start  4: nimblerun_settings_test
 4/23 Test  #4: nimblerun_settings_test ..................   Passed    0.10 sec
      Start  5: nimblerun_appsfolder_catalog_test
 5/23 Test  #5: nimblerun_appsfolder_catalog_test ........   Passed    1.19 sec
      Start  6: nimblerun_app_filter_test
 6/23 Test  #6: nimblerun_app_filter_test ................   Passed    0.04 sec
      Start  7: nimblerun_user_folder_catalog_test
 7/23 Test  #7: nimblerun_user_folder_catalog_test .......   Passed    0.14 sec
      Start  8: nimblerun_identity_dedup_test
 8/23 Test  #8: nimblerun_identity_dedup_test ............   Passed    0.04 sec
      Start  9: nimblerun_shell_launch_test
 9/23 Test  #9: nimblerun_shell_launch_test ..............   Passed    0.50 sec
      Start 10: nimblerun_recent_usage_test
10/23 Test #10: nimblerun_recent_usage_test ..............   Passed    0.10 sec
      Start 11: nimblerun_list_vertical_slice_test
11/23 Test #11: nimblerun_list_vertical_slice_test .......   Passed    0.04 sec
      Start 12: nimblerun_icons_cache_test
12/23 Test #12: nimblerun_icons_cache_test ...............   Passed    0.03 sec
      Start 13: nimblerun_icon_pack_format_test
13/23 Test #13: nimblerun_icon_pack_format_test ..........   Passed    0.08 sec
      Start 14: nimblerun_icon_store_test
14/23 Test #14: nimblerun_icon_store_test ................   Passed    7.89 sec
      Start 15: nimblerun_png_codec_test
15/23 Test #15: nimblerun_png_codec_test .................   Passed    0.11 sec
      Start 16: nimblerun_icon_worker_test
16/23 Test #16: nimblerun_icon_worker_test ...............   Passed    5.99 sec
      Start 17: nimblerun_dpi_theme_accessibility_test
17/23 Test #17: nimblerun_dpi_theme_accessibility_test ...   Passed    0.58 sec
      Start 18: nimblerun_settings_ui_test
18/23 Test #18: nimblerun_settings_ui_test ...............   Passed    0.57 sec
      Start 19: nimblerun_startup_option_test
19/23 Test #19: nimblerun_startup_option_test ............   Passed    0.39 sec
      Start 20: nimblerun_catalog_refresh_test
20/23 Test #20: nimblerun_catalog_refresh_test ...........   Passed    3.45 sec
      Start 21: nimblerun_pinning_test
21/23 Test #21: nimblerun_pinning_test ...................   Passed    0.05 sec
      Start 22: nimblerun_diagnostic_log_test
22/23 Test #22: nimblerun_diagnostic_log_test ............   Passed    0.15 sec
      Start 23: nimblerun_lifecycle_check
23/23 Test #23: nimblerun_lifecycle_check ................   Passed    3.48 sec

100% tests passed out of 23

Total Test time (real) =  26.01 sec
exit code: 0
```
## Process smoke, idle measurement, short soak

### Idle measurement (hidden at rest, sampled once)

```text
idle thread count: 16
idle working set bytes: 39075840
idle private bytes: 8323072
idle handle count: 391
```

### Short soak (3x launch/terminate)

```text
soak iteration 0: launched and terminated OK
soak iteration 1: launched and terminated OK
soak iteration 2: launched and terminated OK
```

## Blocking-threshold gate

| Metric | Blocking threshold | Measured | Verdict |
|---|---|---|---|
| Idle process thread count | not gated (context only) | 16 | recorded |

### Known issues (below target, not blocking)

- Idle CPU 15-min average, working set/private bytes budget, cold start, warm hotkey p95, and filter p95 are recorded in docs/performance-baseline.md and require the full measurement harness (multi-machine, 100/500/2000-entry catalogs). Not gated here.

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

- **PASSED**

