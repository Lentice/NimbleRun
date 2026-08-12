# NimbleRun

NimbleRun is a lightweight Windows app drawer: an empty-state icon grid plus instant app search. This glossary pins the interaction vocabulary shared across the design spec, the UI, and the work items.

## Interaction terms

**cell tooltip**:
The floating popup shown when the pointer rests on a grid cell. It appears only when that cell's name is truncated, shows the full display name, and follows mouse hover only — never keyboard selection.
_Avoid_: tooltip (unqualified), popup, hint

**truncated display name**:
A grid cell's name that exceeds the cell's name width and is therefore drawn with a trailing ellipsis. Truncation is determined by text layout measurement; the cell tooltip exists solely to reveal it.
_Avoid_: short name, ellipsized name

**hover**:
The state where the pointer rests on a grid cell with no mouse button pressed. Hover changes only that cell's light fill and the footer path bar content, never the keyboard selection.
_Avoid_: mouse-over, active
