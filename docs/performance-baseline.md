# Performance Baseline

These are Release x64 measurements, not Debug estimates. Record the OS build, CPU, memory, display scale, catalog size, build commit, and whether a debugger was attached with every run.

| Metric | Target | Blocking threshold | Result | Environment / notes |
| --- | ---: | ---: | --- | --- |
| Idle CPU, 15-minute average | ≤ 0.1% logical CPU equivalent | > 0.5% | Not measured | |
| Idle working set | ≤ 20 MiB | > 35 MiB | Not measured | |
| Idle private bytes | ≤ 15 MiB | > 30 MiB | Not measured | |
| Visible panel with 20 icons | ≤ 35 MiB | > 55 MiB | Not measured | |
| Cold start to hotkey-ready | ≤ 500 ms | > 1,000 ms | Not measured | |
| Warm hotkey to input-ready, p95 | ≤ 80 ms | > 150 ms | Not measured | |
| Filter 500 apps, p95 | ≤ 8 ms | > 16 ms | Not measured | |
| Idle thread count | ≤ 4 | > 8 | Not measured | |

## Measurement rules

- Use a Release x64 build without an attached debugger.
- Record working set, private working set, private bytes, CPU time, context switches, thread count, handle count, GDI objects, and USER objects.
- Measure cold start, warm show/hide, 100/500/2,000-item filtering, 20/40 visible icons, and 1,000 show/hide cycles.
- Do not replace a failed measurement with a process-size estimate or executable file size.

The first gate is the Phase 0 empty-window probe. If its idle or wake-up cost is already over a blocking threshold, fix the host architecture before adding catalog features.
