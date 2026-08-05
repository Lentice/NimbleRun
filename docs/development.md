# Development Guide

## Product boundary

NimbleRun searches launchable apps only. It is not a general file searcher, web searcher, command runner, plugin host, AI assistant, or package manager. User-selected local folders are scanned only for the supported executable extension allowlist.

The priority order is:

1. Idle resource usage and wake-up latency.
2. Correct app discovery and launching.
3. Keyboard and mouse efficiency.
4. Visual polish.

## Architecture rules

The intended module boundaries are:

| Module | Owns | Must not own |
| --- | --- | --- |
| `app_host` | WinMain, COM initialization, message loop, single instance, lifecycle | Search ranking |
| `ui` | HWND, focus, input, DPI, rendering | Folder scanning |
| `catalog` | Sources, merge, deduplication, immutable snapshots | Direct drawing |
| `user_folder_source` | User-selected local roots, recursive enumeration, extension allowlist | General file search, network paths |
| `search` | Normalization, matching, stable ranking | Shell calls |
| `shell` | Known Folders, Shell namespace, icon and launch APIs | Usage statistics |
| `storage` | Settings, pins, usage, atomic writes | Catalog enumeration |
| `diagnostics` | Bounded local logs and performance markers | Search text |

Core value types should remain copyable and testable without HWND or Shell COM ownership. Background work should publish a complete snapshot only after it succeeds; the UI must never display a half-built catalog.

## UI language

All user-visible NimbleRun UI text is English. Keep product copy short and action-oriented, for example:

- `Search apps`
- `No apps found`
- `Refresh apps`
- `Pin` / `Unpin`
- `Open file location`
- `Settings`
- `Quit`

Documentation can remain in Traditional Chinese when it is more useful to the project owner.

## Build configuration

Release is the performance and packaging configuration. The project does not require Visual Studio:

- x64.
- C++20.
- LLVM-MinGW with link-time optimization where supported.
- Unicode, Control Flow Guard, ASLR, DEP, and high-entropy VA.

Debug is for development only and must not be used for release resource gates.

## Change workflow

1. Read the relevant design-spec section.
2. Identify the narrowest module boundary that owns the behavior.
3. Reuse existing types and helpers before adding code.
4. Add or update one focused test for non-trivial logic.
5. Run configure, build, tests, and the applicable manual checks.
6. Update the relevant docs when behavior or a boundary changes.

Do not add a dependency, background loop, framework, or abstraction without a measured need.
