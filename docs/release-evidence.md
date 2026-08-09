# Release Evidence

- Generated: 2026-08-09 14:24:49 +08:00 (UTC: 2026-08-09T14:24:49.3338285+08:00)
- OS: Microsoft Windows 11 專業版 build 26200
- CPU: Intel64 Family 6 Model 151 Stepping 2, GenuineIntel
- Debugger attached: False
- Git commit: 9117dd6c56e32dd50336f73b3a6e811cc5f13bc3
- CTest count: Total Tests: 25

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
-- Configuring done (0.3s)
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
 1/25 Test  #1: nimblerun_search_test ....................   Passed    0.03 sec
      Start  2: nimblerun_hotkey_test
 2/25 Test  #2: nimblerun_hotkey_test ....................   Passed    0.03 sec
      Start  3: nimblerun_start_menu_catalog_test
 3/25 Test  #3: nimblerun_start_menu_catalog_test ........   Passed    8.66 sec
      Start  4: nimblerun_settings_test
 4/25 Test  #4: nimblerun_settings_test ..................   Passed    0.24 sec
      Start  5: nimblerun_appsfolder_catalog_test
 5/25 Test  #5: nimblerun_appsfolder_catalog_test ........   Passed    1.41 sec
      Start  6: nimblerun_app_filter_test
 6/25 Test  #6: nimblerun_app_filter_test ................   Passed    0.03 sec
      Start  7: nimblerun_user_folder_catalog_test
 7/25 Test  #7: nimblerun_user_folder_catalog_test .......   Passed    0.09 sec
      Start  8: nimblerun_identity_dedup_test
 8/25 Test  #8: nimblerun_identity_dedup_test ............   Passed    0.04 sec
      Start  9: nimblerun_shell_launch_test
 9/25 Test  #9: nimblerun_shell_launch_test ..............   Passed    0.64 sec
      Start 10: nimblerun_recent_usage_test
10/25 Test #10: nimblerun_recent_usage_test ..............   Passed    0.22 sec
      Start 11: nimblerun_list_vertical_slice_test
11/25 Test #11: nimblerun_list_vertical_slice_test .......   Passed    0.05 sec
      Start 12: nimblerun_icons_cache_test
12/25 Test #12: nimblerun_icons_cache_test ...............   Passed    0.04 sec
      Start 13: nimblerun_icon_pack_format_test
13/25 Test #13: nimblerun_icon_pack_format_test ..........   Passed    0.11 sec
      Start 14: nimblerun_icon_store_test
14/25 Test #14: nimblerun_icon_store_test ................   Passed    8.41 sec
      Start 15: nimblerun_png_codec_test
15/25 Test #15: nimblerun_png_codec_test .................   Passed    0.07 sec
      Start 16: nimblerun_icon_worker_test
16/25 Test #16: nimblerun_icon_worker_test ...............   Passed    0.86 sec
      Start 17: nimblerun_dpi_theme_accessibility_test
17/25 Test #17: nimblerun_dpi_theme_accessibility_test ...   Passed    0.04 sec
      Start 18: nimblerun_settings_ui_test
18/25 Test #18: nimblerun_settings_ui_test ...............   Passed    0.12 sec
      Start 19: nimblerun_startup_option_test
19/25 Test #19: nimblerun_startup_option_test ............   Passed    0.06 sec
      Start 20: nimblerun_catalog_refresh_test
20/25 Test #20: nimblerun_catalog_refresh_test ...........   Passed    0.08 sec
      Start 21: nimblerun_pinning_test
21/25 Test #21: nimblerun_pinning_test ...................   Passed    0.19 sec
      Start 22: nimblerun_diagnostic_log_test
22/25 Test #22: nimblerun_diagnostic_log_test ............   Passed    3.43 sec
      Start 23: nimblerun_hotkey_capture_test
23/25 Test #23: nimblerun_hotkey_capture_test ............   Passed    0.05 sec
      Start 24: nimblerun_lifecycle_check
24/25 Test #24: nimblerun_lifecycle_check ................   Passed    2.21 sec
      Start 25: nimblerun_catalog_watcher_test
25/25 Test #25: nimblerun_catalog_watcher_test ...........   Passed    1.32 sec

100% tests passed out of 25

Total Test time (real) =  28.48 sec
exit code: 0
```
## Process smoke, idle measurement, short soak

### Idle measurement (hidden at rest, sampled once)

```text
idle thread count: 16
idle working set bytes: 41144320
idle private bytes: 8167424
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
| CTest registration vs executed | registered == executed | live ctest -N vs full-suite output | measured | registered 25 vs executed 25 | PASS |

### Non-blocking process context

- Idle process thread count: 16. This is context only and never substitutes for the app-owned start-address census.
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

