# Development Roadmap

## Phase 0 — Performance probe (current)

- Minimal Win32 popup and message loop.
- `Alt+Space` global hotkey.
- Validate hotkey conflict rejection and one-time notification.
- Direct2D/DirectWrite text and fake grid rendering.
- First idle and warm-show measurements.

## Phase 1 — Launchable vertical slice

- Single instance and tray menu.
- Hotkey conflict handling with tray settings entry.
- Start Menu `.lnk` enumeration.
- Fallback-icon grid.
- Search, keyboard selection, and Shell launch.

Done means a portable ZIP build can search and launch at least 20 real Win32 apps.

## Phase 2 — Complete catalog

- AppsFolder enumeration.
- User-selected local folder enumeration with extension allowlist.
- Stable IDs and deduplication.
- Programs and user-folder directory watcher with debounce.
- Immutable catalog snapshots and refresh generations.

## Phase 3 — Usable panel

- Lazy Shell icons and bounded LRU cache.
- DPI, light/dark mode, and high contrast.
- Grid keyboard navigation and outside-click hiding.

## Phase 4 — Personalization

- Pinning and ordering.
- Usage scoring.
- Settings and startup behavior.
- Atomic persistence and migration.

## Phase 5 — Release gate

- Performance harness.
- 72-hour soak test.
- Handle, GDI, USER, and memory leak checks.
- Portable ZIP, README, LICENSE, security flags, and Windows 10/11 validation.

Out-of-scope items in the design specification remain out of scope until the MVP passes its resource and stability gates.
