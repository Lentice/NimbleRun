# Release Evidence

- Generated: 2026-08-05 03:04:10 +08:00 (UTC: 2026-08-05T03:04:10.1623606+08:00)
- OS: Microsoft Windows 11 專業版 build 26200
- CPU: Intel64 Family 6 Model 151 Stepping 2, GenuineIntel
- Debugger attached: False
- Git commit: d1e92c758eb27d7a762d07f7ffb32493f01151c1
- CTest count: Total Tests: 18

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
-- Configuring done (0.2s)
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
 1/18 Test  #1: nimblerun_search_test ....................   Passed    0.05 sec
      Start  2: nimblerun_hotkey_test
 2/18 Test  #2: nimblerun_hotkey_test ....................   Passed    0.05 sec
      Start  3: nimblerun_start_menu_catalog_test
 3/18 Test  #3: nimblerun_start_menu_catalog_test ........   Passed    0.53 sec
      Start  4: nimblerun_settings_test
 4/18 Test  #4: nimblerun_settings_test ..................   Passed    0.06 sec
      Start  5: nimblerun_appsfolder_catalog_test
 5/18 Test  #5: nimblerun_appsfolder_catalog_test ........   Passed    1.18 sec
      Start  6: nimblerun_user_folder_catalog_test
 6/18 Test  #6: nimblerun_user_folder_catalog_test .......   Passed    0.08 sec
      Start  7: nimblerun_identity_dedup_test
 7/18 Test  #7: nimblerun_identity_dedup_test ............   Passed    0.06 sec
      Start  8: nimblerun_shell_launch_test
 8/18 Test  #8: nimblerun_shell_launch_test ..............   Passed    0.35 sec
      Start  9: nimblerun_recent_usage_test
 9/18 Test  #9: nimblerun_recent_usage_test ..............   Passed    0.11 sec
      Start 10: nimblerun_list_vertical_slice_test
10/18 Test #10: nimblerun_list_vertical_slice_test .......   Passed    0.05 sec
      Start 11: nimblerun_icons_cache_test
11/18 Test #11: nimblerun_icons_cache_test ...............   Passed    0.05 sec
      Start 12: nimblerun_dpi_theme_accessibility_test
12/18 Test #12: nimblerun_dpi_theme_accessibility_test ...   Passed    0.04 sec
      Start 13: nimblerun_settings_ui_test
13/18 Test #13: nimblerun_settings_ui_test ...............   Passed    0.08 sec
      Start 14: nimblerun_startup_option_test
14/18 Test #14: nimblerun_startup_option_test ............   Passed    0.07 sec
      Start 15: nimblerun_catalog_refresh_test
15/18 Test #15: nimblerun_catalog_refresh_test ...........   Passed    1.63 sec
      Start 16: nimblerun_pinning_test
16/18 Test #16: nimblerun_pinning_test ...................   Passed    0.07 sec
      Start 17: nimblerun_diagnostic_log_test
17/18 Test #17: nimblerun_diagnostic_log_test ............   Passed    0.08 sec
      Start 18: nimblerun_lifecycle_check
18/18 Test #18: nimblerun_lifecycle_check ................   Passed    1.50 sec

100% tests passed out of 18

Total Test time (real) =   6.08 sec
exit code: 0
```
## Process smoke, idle measurement, short soak

### Idle measurement (hidden at rest, sampled once)

```text
idle thread count: 14
idle working set bytes: 36659200
idle private bytes: 7180288
idle handle count: 377
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
| Idle thread count | > 8 | 14 | FAIL |

### Known issues (below target, not blocking)

- Idle CPU 15-min average, working set/private bytes budget, cold start, warm hotkey p95, and filter p95 are recorded in docs/performance-baseline.md and require the full measurement harness (multi-machine, 100/500/2000-entry catalogs). Not gated here.

### Thread-count attribution (2026-08-05)

The measured idle thread count exceeds the `> 8` blocking threshold. A
thread start-address census attributes the threads as: 1 main thread, 2
`std::thread` catalog watchers (one per Programs known folder, blocking on
`ReadDirectoryChangesW` per design-spec §9.2), plus OS-owned infrastructure
(IME `IMM32.dll`, `ntdll.dll`/`ucrtbase.dll` threadpool/worker threads from
Direct2D/DirectWrite/Shell COM). App-owned threads (3) are within the `<= 4`
target; the over-budget count is dominated by OS infrastructure. Tracked as
a known issue for the release gate.

## Result

- **FAILED** (build/test/process/threshold gate failed)

