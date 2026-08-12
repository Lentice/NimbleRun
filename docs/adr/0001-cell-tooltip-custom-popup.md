# Cell tooltip is a custom Direct2D popup, not the native tooltip control

The empty-state grid truncates app names with a trailing ellipsis, and the user asked (2026-08-11) for a hover tooltip that reveals the full name with a Bootstrap-style look: dark, rounded, white text, and an arrow pointing at the cell. We render it as a small custom Direct2D popup window instead of the native `TOOLTIPS_CLASS` because the native control cannot produce that look on the Windows 10 22H2 target (classic square/balloon shape, no controllable arrow), while the custom window reuses the panel's existing Direct2D infrastructure and costs nothing at idle: the window and its render target exist only while the tooltip is visible, so the NFR-001 budgets stay untouched (measured idle working set 37.2 MiB vs 60 MiB target).

## Considered options

- **Native TOOLTIPS_CLASS**: zero application code, built-in delay (`TTM_SETDELAYTIME`), free accessibility. Rejected because the look is the entire point of the feature: on Win10 it renders the classic tooltip shape and the arrow is not controllable, so the Bootstrap style cannot be met.
- **Drawing the tooltip inside the panel window**: no extra HWND, but the tooltip is clipped to the panel bounds and cannot float above the top grid row, making the placement/flip rule impossible.
- **Custom Direct2D popup (chosen)**: full look control on both targets; create on show, release on hide keeps the idle footprint at zero; the one-shot 150 ms hover timer is event-driven, so NFR-002 is unaffected.
