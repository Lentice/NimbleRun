# Release evidence runner (NR-017 / design-spec §12.4, §15 Phase 5).
#
# Produces a repeatable evidence report with tool versions, environment
# conditions, the exact commands run and their exit codes, plus an explicit
# row for every NFR-001 blocking metric. Runs build + the full CTest suite, a
# process launch/terminate smoke, and a short soak. Unknown blocking metrics
# fail closed as INCOMPLETE (exit 2); measured threshold failures and build /
# test failures exit 1. Process totals and estimates are context only.
#
# Usage:
#   pwsh -NoProfile -File tests/release/release_evidence.ps1 [-OutPath <file.md>]
#
# Requires the same toolchain as AGENTS.md on PATH (cmake, ninja, clang, ctest).

param(
    [string]$OutPath = ""
)

$ErrorActionPreference = 'Stop'

# NR-056: the parameter was named after PowerShell's automatic arguments
# variable, so `& $name @Arguments` splatted the caller's own (empty) argument
# array and --version never reached the tool. Every recorded tool version in
# docs/release-evidence.md was therefore an error message.
function Get-CmdVersion([string]$name, [string[]]$Arguments) {
    try {
        $raw = & $name @Arguments 2>&1 | Out-String
        return $raw.Trim().Split("`n")[0].Trim()
    } catch {
        return "(not found)"
    }
}

function Invoke-Capture([string]$label, [scriptblock]$body) {
    $lines = @('```text', "# $label")
    $exitCode = 0
    try {
        $output = & $body 2>&1
        foreach ($line in $output) { $lines += [string]$line }
        $exitCode = $LASTEXITCODE
    } catch {
        $exitCode = 1
        $lines += "EXCEPTION: $($_.Exception.Message)"
    }
    $lines += "exit code: $exitCode"
    $lines += '```'
    return @{ Lines = $lines; Exit = $exitCode }
}

$workspace = (Resolve-Path "$PSScriptRoot\..\..").Path
$buildDir = "$workspace\build"
$exe = "$buildDir\NimbleRun.exe"

if ($OutPath -eq "") {
    $OutPath = "$workspace\docs\release-evidence.md"
}

# NR-104: the registered CTest count (from `ctest -N`) is the single count
# authority; the header prints it live and the count-consistency check below
# compares it against what the suite actually ran.
$ctestCountLine = (ctest --test-dir $buildDir -N 2>$null | Select-String 'Total Tests:' | ForEach-Object { $_.Line.Trim() })
$registeredTests = 0
if ($ctestCountLine -match 'Total Tests:\s*(\d+)') { $registeredTests = [int]$matches[1] }

# ---- Header: tool versions and conditions (reproducibility) ----------------
$header = @()
$header += "# Release Evidence"
$header += ""
$header += "- Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz') (UTC: $(Get-Date -Format 'o'))"
$header += "- OS: $((Get-CimInstance Win32_OperatingSystem).Caption) build $((Get-CimInstance Win32_OperatingSystem).BuildNumber)"
$header += "- CPU: $env:PROCESSOR_IDENTIFIER"
$header += "- Debugger attached: $([System.Diagnostics.Debugger]::IsAttached)"
$header += "- Git commit: $((git -C $workspace rev-parse HEAD 2>$null))"
$header += "- CTest count: $ctestCountLine"
$header += ""
$header += "## Tool versions"
$header += ""
$header += "| Tool | Version |"
$header += "|---|---|"
$header += "| cmake | $(Get-CmdVersion cmake '--version') |"
$header += "| ninja | $(Get-CmdVersion ninja '--version') |"
$header += "| clang | $(Get-CmdVersion clang '--version') |"
$header += "| clang++ | $(Get-CmdVersion clang++ '--version') |"
$header += "| ctest | $(Get-CmdVersion ctest '--version') |"
$header += ""
$header += "## Conditions"
$header += ""
$header += '- Build dir: `$buildDir`'
$header += "- Build type: Release"
$header += '- Toolchain: `cmake/llvm-mingw.cmake` (LLVM-MinGW, target x86_64-w64-windows-gnu)'
$header += '- The blocking thresholds are the `> value` columns of `docs/performance-baseline.md`.'
$header += ""

# ---- Build --------------------------------------------------------------
$configure = Invoke-Capture 'cmake configure' {
    & cmake -S $workspace -B $buildDir -G Ninja '-DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake' -DCMAKE_BUILD_TYPE=Release
}
$build = Invoke-Capture 'cmake build' {
    & cmake --build $buildDir
}
$ctest = Invoke-Capture 'ctest full suite' {
    & ctest --test-dir $buildDir --output-on-failure
}

# NR-104: count-consistency sanity check. Parse the executed total from the
# suite output ("out of <N> tests", e.g. "100% tests passed out of 25") and
# mark the report STALE when it does not match the registered count -- so a
# future registration change cannot silently produce a stale report again.
$executedTests = $null
foreach ($line in $ctest.Lines) {
    if ($line -match 'out of\s+(\d+)(?:\s*tests?)?') { $executedTests = [int]$matches[1] }
}
$stale = ($null -eq $executedTests) -or ($executedTests -ne $registeredTests)
$executedDisplay = if ($null -eq $executedTests) { 'unparsed' } else { $executedTests }

# ---- Process smoke + soak + measurements ---------------------------------
# A resident instance's idle thread count and working set, measured while the
# process is hidden at rest (NFR-001 idle budget). Launch, wait for it to settle,
# sample, terminate. A short soak repeats launch/terminate to catch startup
# leaks without a 72-hour run (documented as a short soak).
$process = @()
$process += "## Process smoke, idle measurement, short soak"
$process += ""
$smokeFailures = 0

function Start-SettledProcess {
    $p = Start-Process -FilePath $exe -PassThru
    # Let the startup background catalog build finish and the process settle
    # (design-spec NFR-001 measures "after first indexing completes and 60s
    # idle"; a shorter settle is a conservative proxy here).
    Start-Sleep -Seconds 3
    return $p
}

$idle = @()
try {
    $p = Start-SettledProcess
    if ($p.HasExited) {
        $idle += "FAILED: first instance exited immediately (code $($p.ExitCode))"
        $smokeFailures++
    } else {
        $p.Refresh()
        $idle += "idle thread count: $($p.Threads.Count)"
        $idle += "idle working set bytes: $($p.WorkingSet64)"
        $idle += "idle private bytes: $($p.PrivateMemorySize64)"
        $idle += "idle handle count: $($p.HandleCount)"
        $p.Kill()
        [void]$p.WaitForExit(2000)
    }
} catch {
    $idle += "EXCEPTION during idle measurement: $($_.Exception.Message)"
    $smokeFailures++
}

$soak = @()
$soakFailures = 0
for ($i = 0; $i -lt 3; $i++) {
    try {
        $p = Start-SettledProcess
        if ($p.HasExited) {
            $soak += "soak iteration ${i}: instance exited early (code $($p.ExitCode))"
            $soakFailures++
        } else {
            $p.Kill()
            [void]$p.WaitForExit(2000)
            $soak += "soak iteration ${i}: launched and terminated OK"
        }
    } catch {
        $soak += "soak iteration ${i}: EXCEPTION $($_.Exception.Message)"
        $soakFailures++
    }
}

$process += "### Idle measurement (hidden at rest, sampled once)"
$process += ""
$process += '```text'
$process += $idle
$process += '```'
$process += ""
$process += "### Short soak (3x launch/terminate)"
$process += ""
$process += '```text'
$process += $soak
$process += '```'
$process += ""

# ---- Threshold comparison ------------------------------------------------
# NFR-001 has nine blocking rows. Keep this list explicit: a new baseline row
# must not disappear from release evidence. A row is release-valid only when
# this runner has a measurement source that satisfies the NFR-001 conditions
# (Release x64, no debugger, initial indexing complete, 60s idle, and the
# required catalog/DPI profile). Existing smoke samples are context, not
# substitutes for those measurements.
$blockingMetrics = @(
    [pscustomobject]@{ Metric = 'Idle CPU, 15-minute average'; Threshold = '> 0.5% logical CPU equivalent'; Source = 'No 15-minute idle CPU sample in this runner'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
    [pscustomobject]@{ Metric = 'Idle working set'; Threshold = '> 80 MiB'; Source = 'The 3-second smoke sample is context only; no compliant 60-second idle profile'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
    [pscustomobject]@{ Metric = 'Idle private bytes'; Threshold = '> 70 MiB'; Source = 'The 3-second smoke sample is context only; no compliant 60-second idle profile'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
    [pscustomobject]@{ Metric = 'Visible panel with 20 icons working set'; Threshold = '> 100 MiB'; Source = 'No visible-panel 20-icon census'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
    [pscustomobject]@{ Metric = 'Cold start to hotkey-ready'; Threshold = '> 1,000 ms'; Source = 'No startup-to-hotkey-ready timestamp'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
    [pscustomobject]@{ Metric = 'Warm hotkey to input-ready, p95'; Threshold = 'p95 > 150 ms'; Source = 'No warm hotkey/input-ready latency profile'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
    [pscustomobject]@{ Metric = 'Filter 500 apps, p95'; Threshold = 'p95 > 16 ms'; Source = 'No 500-entry p95 profile run by this release invocation'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
    [pscustomobject]@{ Metric = 'Idle app-owned thread count'; Threshold = '超出 2 + watcher root 數'; Source = 'No start-address census; process total is context only'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
    [pscustomobject]@{ Metric = 'icons.cache file size'; Threshold = '> 48 MiB'; Source = 'No complete icon build followed by an actual file-size sample'; Measured = 'not measured'; Value = 'not measured'; Verdict = 'INCOMPLETE' }
)
if ($blockingMetrics.Count -ne 9) {
    throw "NFR-001 blocking metric list must contain exactly 9 rows"
}
$metricFailures = @($blockingMetrics | Where-Object { $_.Measured -eq 'measured' -and $_.Verdict -eq 'FAIL' })
$metricIncomplete = @($blockingMetrics | Where-Object { $_.Measured -ne 'measured' -or $_.Verdict -eq 'INCOMPLETE' })

$threadCount = $null
foreach ($line in $idle) {
    if ($line -match '^idle thread count: (\d+)$') {
        $threadCount = [int]$matches[1]
    }
}

$gate = @()
$gate += "## Blocking-threshold gate"
$gate += ""
$gate += "| Metric | Blocking threshold | Measurement source | Measured | Value | Verdict |"
$gate += "|---|---|---|---|---|---|"
foreach ($metric in $blockingMetrics) {
    $gate += "| $($metric.Metric) | $($metric.Threshold) | $($metric.Source) | $($metric.Measured) | $($metric.Value) | $($metric.Verdict) |"
}
$gate += ""
$gate += "### CTest gate"
$gate += ""
$gate += "CTest is a separate release gate; its registration count comes from live `ctest -N` output."
$gate += ""
$gate += "| Metric | Threshold | Measurement source | Measured | Value | Verdict |"
$gate += "|---|---|---|---|---|---|"
$gate += "| CTest registration vs executed | registered == executed | live ctest -N vs full-suite output | measured | registered $registeredTests vs executed $executedDisplay | $(if ($stale) { 'STALE' } else { 'PASS' }) |"

# Sub-threshold items that are not hard gates are recorded as known issues.
$gate += ""
$gate += "### Non-blocking process context"
$gate += ""
$gate += "- Idle process thread count: $(if ($null -eq $threadCount) { 'not measured' } else { $threadCount }). This is context only and never substitutes for the app-owned start-address census."
$gate += "- Idle working set/private bytes and the short soak are smoke context only; they do not satisfy the NFR-001 60-second/profile requirements."
$gate += ""
$gate += '### Thread-count attribution'
$gate += ""
$gate += 'The idle thread-count budget applies to app-owned threads only: 1 UI thread,'
$gate += '1 resident icon worker, and one directory watcher per watcher root (two'
$gate += 'Programs known folders plus each configured custom folder), each blocking on'
$gate += '`ReadDirectoryChangesW` per design-spec §9.2. Catalog rebuild workers are'
$gate += 'per-source and reclaimed on completion, so they are not part of the idle'
$gate += 'figure.'
$gate += ""
$gate += 'The process total above is larger and is recorded as context, not gated: it'
$gate += 'also counts threads Windows injects (IME `IMM32.dll`, `ntdll.dll` /'
$gate += '`ucrtbase.dll` threadpool and worker threads from Direct2D/DirectWrite/Shell'
$gate += 'COM, plus display-driver device threads), whose number varies with OS build,'
$gate += 'display driver and installed Shell extensions. A 2026-08-05 start-address'
$gate += 'census confirmed the attribution: 3 app-owned threads out of 14, matching the'
$gate += 'formula for that configuration (no icon worker, no custom root yet).'
$gate += ""

# ---- Assemble and write ---------------------------------------------------
$sections = @($header)
foreach ($item in @($configure, $build, $ctest)) {
    $sections += $item.Lines
}
$sections += $process
$sections += $gate

$gateFailure = $stale -or ($metricFailures.Count -gt 0)
$incomplete = $metricIncomplete.Count -gt 0
$exitFailures = $configure.Exit -or $build.Exit -or $ctest.Exit -or ($smokeFailures -gt 0) -or ($soakFailures -gt 0) -or $gateFailure

$sections += "## Result"
$sections += ""
$result = if ($incomplete) {
    if ($exitFailures) { 'INCOMPLETE (blocking NFR-001 metrics are not measured; another gate also failed)' } else { 'INCOMPLETE (one or more blocking NFR-001 metrics are not measured)' }
} elseif ($exitFailures) {
    'FAILED (build/test/process/threshold gate failed)'
} else {
    'PASSED'
}
$sections += "- **$result**"
$sections += ""

$report = $sections -join "`n"
Set-Content -LiteralPath $OutPath -Value $report -Encoding utf8
Write-Output "Evidence written to $OutPath"
if ($exitFailures) {
    if ($stale) {
        Write-Output "STALE: registered CTest count ($registeredTests) differs from the executed count ($executedDisplay). Regenerate after a clean build + ctest run."
    }
    if ($incomplete) {
        Write-Output 'Evidence is incomplete: one or more blocking NFR-001 metrics are not measured.'
    }
    Write-Output 'One or more gates failed.'
    exit 1
}
if ($incomplete) {
    Write-Output 'Evidence is incomplete: one or more blocking NFR-001 metrics are not measured.'
    exit 2
}
Write-Output 'All gates passed.'
exit 0
