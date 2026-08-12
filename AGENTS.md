# NimbleRun Agent Instructions

## Project intent

NimbleRun is a lightweight Windows app drawer. The MVP targets Windows 10 22H2 and Windows 11 x64, using C++20, native Win32, Direct2D, DirectWrite, and Windows Shell APIs.

The design specification in `docs/design-spec.md` is the product source of truth. Do not add features that are listed as out of scope there.

## Language rules

- Conversation with the user: Traditional Chinese.
- Project documentation may use Traditional Chinese.
- NimbleRun application UI text must be English.
- Code, identifiers, test names, and diagnostic event names use English.

## Engineering rules

- Read the relevant design-spec section and trace existing callers before changing shared code.
- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Use the C++ standard library or Win32 native APIs before adding dependencies.
- Keep search, ranking, scoring, persistence formats, and other core logic independent of HWND and Shell COM objects where practical.
- Keep App Catalog data as ordinary copyable values. UI code must not own Shell COM pointers.
- Keep the idle path event-driven: no busy loops and no high-frequency timers.
- Launch apps through Windows Shell APIs. Never build an arbitrary command line from search input.
- Keep all user data under `%LOCALAPPDATA%\\NimbleRun`; do not write beside the executable.
- Do not add network access, telemetry, third-party runtime dependencies, services, drivers, or administrator requirements.
- Do not overwrite user data in place. Use temporary files and atomic replacement for persistent writes.
- UI strings are English and should be centralized when more than one screen needs them.
- New non-trivial logic needs one focused runnable test or self-check.

## Work item authoring rules

Work items live in `docs/work-items/` and are tracked in `docs/work-items.md`.

- Split work agile-style: one item delivers one outcome, sized half a day to two days. If it grows past that, split it before writing it.
- Write each item to be self-contained, so a low-capability agent can pick it up and finish it without prior context or further questions.
- Quote the binding constraints into the item itself: the relevant `docs/design-spec.md` clauses, the `docs/development.md` rules, and the applicable rules from this file. Do not rely on the agent finding them.
- List the exact files to read and trace, the concrete scope (signatures, constants, call sites), the non-goals, the acceptance criteria, and the runnable Agent checks.
- When an item overrides an earlier decision, state the override inside the new item. Never edit a completed item's document — that rule protects its scope, decisions and 交接區, which are the historical record. It does not protect tracker metadata that has since become false.
- Status and dependencies live only in the Item 總覽 table of `docs/work-items.md`. An item document must not declare its own status: two copies drift, and in this repository twelve of them had already drifted to a state that told a cold reader the work was still waiting to be done.
- Before writing a new item, read the "已否決的方向" section of `docs/work-items.md`. Reopening a rejected direction is allowed, but the new item must state the override and the new evidence in its own text.
- Anything a later session needs must live in the repository, not in a scratchpad handoff. Candidate items and rejected directions go in `docs/work-items.md`; measured numbers go in `docs/performance-baseline.md` or the item's 交接區. A scratchpad is session-scoped and unversioned, so a fact left only there has to be re-derived — which is how the same conclusion ended up recorded in five consecutive handoffs.
- Do not reserve item numbers in advance. Take the highest number in the Item 總覽 table and add one at the moment you write the file, and confirm `docs/work-items/` has no file with that number: another agent may be authoring items in this repository at the same time.

## Current baseline

The repository is mid-MVP, in Phase 5 (release gate) — `docs/roadmap.md` is the authoritative phase status. The executable is a real launcher:

- Multi-source catalog: Start Menu, AppsFolder, and user folders, with directory-watcher refresh, immutable snapshots, and refresh generations.
- Lazy Shell icon store with bounded LRU cache and `icons.cache` persistence.
- Search, usage scoring, pinning, settings dialog, tray menu, and native cell tooltips.
- Atomic persistence under `%LOCALAPPDATA%\NimbleRun`; startup can show a cached catalog while the first rebuild runs.
- 32 CTest-registered checks plus a release-evidence runner (`docs/testing.md`). Pre-release: NFR-001 resource gates are not yet measured.

## Validation

From a shell with LLVM-MinGW, CMake, and Ninja on `PATH`:

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Before release, validate the acceptance criteria and resource budgets in `docs/testing.md` and `docs/performance-baseline.md` with a Release x64 build.

## Safety boundaries

- Do not push branches, publish releases, or modify production systems without explicit approval.
- Do not add schema migrations or destructive data cleanup as part of unrelated changes.
- Keep changes scoped to the requested task and update the relevant documentation when behavior changes.
