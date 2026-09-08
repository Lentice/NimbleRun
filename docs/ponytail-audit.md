# Ponytail audit — 2026-09-08

Scope: over-engineering only. Correctness / security / perf out of scope. Lists findings, applies nothing.

## Findings (biggest cut first)

- `shrink` Root CMake repeats `target_compile_definitions` + `target_compile_options` blocks ~14x. Replace with one `function(nimblerun_add_lib name)` / interface lib. [CMakeLists.txt]
- `shrink` Tests CMake repeats the same definitions/options per target plus HEAD/TAIL SUBLIST split to preserve ctest numbers and 4 verbatim exception blocks. Replace with one `function(nimblerun_add_test ...)`. [tests/CMakeLists.txt]
- `yagni` `nimblerun_icon_request_session` static lib exists for one 33-line .cpp (2x `std::set` wrapper, one global in main.cpp:281). Fold into `nimblerun_icons` or panel model, drop the lib + separate test link. [src/app_host/icon_request_session.h, src/app_host/icon_request_session.cpp, CMakeLists.txt]
- `yagni` `RebuildPipeline` ctor takes 8 args, 7 of them `std::function` injectors (PostToUi, EnumerateSource, SettingsSnapshot, Complete x2, ScheduleDebounce, ThreadFactory + OnException) for one production caller. Keep at most Enumerate + Post, call the rest directly. [src/app_host/rebuild_pipeline.h]
- `yagni` `IconStore` has 4 virtuals + virtual dtor with one prod impl (test seam only). Prefer non-virtual + link-time fake or function seam. [src/icons/icon_store.h]
- `shrink` `png_codec.cpp` spells `std::unique_ptr<X, ComRelease>` ~13x instead of using the `ComPtr<X>` alias defined in the same header it includes. Unify on the alias. [src/icons/png_codec.cpp, src/win/com.h]
- `stdlib` `HandleGuard` (27 lines, no move/release) used at 5 sites in main.cpp only. Replace with `std::unique_ptr<void, CloseHandleDeleter>` or WRL `unique_handle`. [src/win/handle_guard.h, src/app_host/main.cpp:3396-3473]
- `shrink` `storage/atomic_text_file.h` is 444 lines header-only, included by ~9 TUs (stores, catalog_cache, main, settings_dialog/editor, diagnostic_log, tests). Keep constants inline, move Read/Parse/Escape/Split/AtomicWrite bodies to a .cpp. No behavior change, cuts rebuild cost. [src/storage/atomic_text_file.h]
- `shrink` `HandoffRegistry<T>` token = `reinterpret_cast<uintptr_t>(ptr)` + mutex + map, instantiated 2x (IconResult global, RebuildResult member). Low priority; if touched, replace token with incrementing id. [src/win/handoff_registry.h, src/icons/icon_worker.h:56, src/app_host/rebuild_pipeline.h:125]

## Checked, kept (no cut)

- `stable_id.h` hand-rolled FNV + path normalize: `std::hash` is not stable across runs, keep.
- `icon_pack_format.h` / `icon_cache.h` / `panel_palette.{h,cpp}` / `quick_select.h` / `load_notice.h`: single-purpose, small, already minimal.
- `ComGuard` dedup into `win/com.h`: justified (was 2 verbatim copies), keep. Do NOT reopen the rejected "generic Store base class" direction (docs/work-items.md 已否決的方向) — per-store Load switch differences are product decisions, not duplication.
- `main.cpp` 3645 lines is a god-file problem (under-engineering), not over-engineering — out of scope for this pass.

```
net: -300 lines, -1 lib possible.
```
