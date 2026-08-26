# NFR-001 measurement probe. This is intentionally separate from the release
# evidence runner: it can take 15 minutes and only records measurements whose
# Win32 observation satisfies the documented profile.

param(
    [string]$ExePath = "",
    [int]$SettleSeconds = 60,
    [int]$IdleSeconds = 900,
    [int]$WarmSamples = 20
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot\..\..").Path
if ($ExePath -eq "") { $ExePath = "$workspace\build\NimbleRun.exe" }
$exe = (Resolve-Path $ExePath).Path

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NimNfr001 {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct GuiInfo {
        public int cbSize; public IntPtr hWndActive, hWndFocus, hWndCapture, hWndMenuOwner,
            hWndMoveSize, hWndCaret; public Rect rcCaret;
    }
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder b, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessageW(string n);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint id, ref GuiInfo info);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr OpenEventW(uint a, bool i, string n);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr CreateEventW(IntPtr a, bool m, bool s, string n);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint a, bool i, uint id);
    [DllImport("ntdll.dll")] public static extern int NtQueryInformationThread(IntPtr h, int c, out IntPtr v, int n, IntPtr r);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@

$windowClass = 'NimbleRun.Phase0Probe'
$showMessage = [NimNfr001]::RegisterWindowMessageW('NimbleRun.ShowPanel')
$startupEvent = [IntPtr]::Zero
$hotkeyEvent = [IntPtr]::Zero
$inputReadyName = "Local\NimbleRun.Nfr001.$([Guid]::NewGuid().ToString('N'))"
$inputReadyEvent = [NimNfr001]::CreateEventW([IntPtr]::Zero, $false, $false, $inputReadyName)
if ($inputReadyEvent -eq [IntPtr]::Zero) { throw 'Could not create input-ready event' }
$visibleReadyName = "Local\NimbleRun.Nfr001.$([Guid]::NewGuid().ToString('N'))"
$visibleReadyEvent = [NimNfr001]::CreateEventW([IntPtr]::Zero, $false, $false, $visibleReadyName)
if ($visibleReadyEvent -eq [IntPtr]::Zero) { throw 'Could not create visible-ready event' }
$previousInputEvent = $env:NIMBLERUN_TEST_INPUT_READY_EVENT
$previousVisibleEvent = $env:NIMBLERUN_TEST_VISIBLE_READY_EVENT
$env:NIMBLERUN_TEST_INPUT_READY_EVENT = $inputReadyName
$env:NIMBLERUN_TEST_VISIBLE_READY_EVENT = $visibleReadyName

function Find-NimbleWindow {
    $script:found = [IntPtr]::Zero
    $callback = [NimNfr001+EnumWindowsProc]{ param($h, $l)
        $name = New-Object System.Text.StringBuilder 128
        [void][NimNfr001]::GetClassNameW($h, $name, $name.Capacity)
        if ($name.ToString() -eq 'NimbleRun.Phase0Probe') { $script:found = $h; return $false }
        return $true
    }
    [void][NimNfr001]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:found
}

function Wait-Until([scriptblock]$Condition, [int]$TimeoutMs, [string]$Label) {
    $deadline = [Environment]::TickCount64 + $TimeoutMs
    while ([Environment]::TickCount64 -lt $deadline) {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 20
    }
    throw "Timed out waiting for $Label"
}

$start = [System.Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $exe -PassThru
try {
    Wait-Until { $process.HasExited -or (Find-NimbleWindow) -ne [IntPtr]::Zero } 30000 'main window'
    if ($process.HasExited) { throw "NimbleRun exited with code $($process.ExitCode)" }
    $coldWindowReadyMs = $start.Elapsed.TotalMilliseconds
    $startupEvent = [NimNfr001]::OpenEventW(0x00100000, $false, 'Local\NimbleRun.StartupReady')
    if ($startupEvent -eq [IntPtr]::Zero) { throw 'Could not open Local\NimbleRun.StartupReady' }
    if ([NimNfr001]::WaitForSingleObject($startupEvent, 5000) -ne 0) { throw 'StartupReady was not signaled' }
    $hotkeyEvent = [NimNfr001]::OpenEventW(0x00100000, $false, 'Local\NimbleRun.HotkeyReady')
    if ($hotkeyEvent -eq [IntPtr]::Zero) { throw 'Could not open Local\NimbleRun.HotkeyReady' }
    if ([NimNfr001]::WaitForSingleObject($hotkeyEvent, 5000) -ne 0) { throw 'HotkeyReady was not signaled' }
    $coldHotkeyReadyMs = $start.Elapsed.TotalMilliseconds
    $window = Find-NimbleWindow

    # Let the normal startup indexing settle before the compliant idle window.
    Start-Sleep -Seconds $SettleSeconds
    $process.Refresh()
    $cpu0 = $process.TotalProcessorTime.TotalMilliseconds
    $idleStart = [DateTime]::UtcNow
    $wsSamples = @()
    $privateSamples = @()
    while (([DateTime]::UtcNow - $idleStart).TotalSeconds -lt $IdleSeconds) {
        if ($process.HasExited) { throw 'NimbleRun exited during idle measurement' }
        $process.Refresh()
        $wsSamples += $process.WorkingSet64
        $privateSamples += $process.PrivateMemorySize64
        Start-Sleep -Seconds 5
    }
    $process.Refresh()
    $cpuMs = $process.TotalProcessorTime.TotalMilliseconds - $cpu0
    $wallMs = (([DateTime]::UtcNow - $idleStart).TotalMilliseconds)
    $cpuPercent = 100.0 * $cpuMs / $wallMs / [Environment]::ProcessorCount

    $visibleIssue = $null
    $visibleWorkingSetMib = $null
    $visibleRowsReady = $false
    try {
        [void][NimNfr001]::PostMessageW($window, $showMessage, [IntPtr]::Zero, [IntPtr]::Zero)
        Wait-Until { [NimNfr001]::WaitForSingleObject($visibleReadyEvent, 0) -eq 0 } 15000 'visible 20-row grid'
        $process.Refresh()
        $visibleWorkingSetMib = [Math]::Round(($process.WorkingSet64 / 1MB), 2)
        $visibleRowsReady = $true
        $edit = [NimNfr001]::FindWindowExW($window, [IntPtr]::Zero, 'EDIT', $null)
        [void][NimNfr001]::PostMessageW($edit, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero)
        Wait-Until { -not [NimNfr001]::IsWindowVisible($window) } 5000 'visible grid hide'
        Start-Sleep -Seconds 2
    } catch {
        $visibleIssue = $_.Exception.Message
    }

    $process.Refresh()
    $module = $process.MainModule
    $moduleStart = $module.BaseAddress.ToInt64()
    $moduleEnd = $moduleStart + $module.ModuleMemorySize
    $appOwnedThreads = 0
    foreach ($thread in $process.Threads) {
        $threadHandle = [NimNfr001]::OpenThread(0x40, $false, [uint32]$thread.Id)
        if ($threadHandle -eq [IntPtr]::Zero) { continue }
        try {
            $startAddress = [IntPtr]::Zero
            if ([NimNfr001]::NtQueryInformationThread($threadHandle, 9, [ref]$startAddress, [IntPtr]::Size, [IntPtr]::Zero) -eq 0) {
                $address = $startAddress.ToInt64()
                if ($address -ge $moduleStart -and $address -lt $moduleEnd) { $appOwnedThreads++ }
            }
        } finally { [void][NimNfr001]::CloseHandle($threadHandle) }
    }
    $settingsPath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NimbleRun\settings.ini'
    $customRoots = if (Test-Path $settingsPath) { @(Get-Content $settingsPath | Where-Object { $_ -match '^catalog_root=' }).Count } else { 0 }
    $watcherRoots = 2 + $customRoots
    $threadBudget = 2 + $watcherRoots

    $warmMs = @()
    $warmIssue = $null
    for ($i = 0; $i -lt $WarmSamples; $i++) {
        $t = [System.Diagnostics.Stopwatch]::StartNew()
        [void][NimNfr001]::PostMessageW($window, $showMessage, [IntPtr]::Zero, [IntPtr]::Zero)
        try { Wait-Until { [NimNfr001]::WaitForSingleObject($inputReadyEvent, 0) -eq 0 } 5000 "warm input-ready $i" } catch {
            $warmIssue = "warm sample $i input-ready event timed out"
            break
        }
        $warmMs += $t.Elapsed.TotalMilliseconds
        $edit = [NimNfr001]::FindWindowExW($window, [IntPtr]::Zero, 'EDIT', $null)
        [void][NimNfr001]::PostMessageW($edit, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero)
        Wait-Until { -not [NimNfr001]::IsWindowVisible($window) } 5000 "warm hide $i"
    }

    $sortedWarm = @($warmMs | Sort-Object)
    $p95Index = if ($sortedWarm.Count -gt 0) { [Math]::Max(0, [Math]::Ceiling($sortedWarm.Count * 0.95) - 1) } else { 0 }
    $cache = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NimbleRun\icons.cache'
    [pscustomobject]@{
        profile = "Release x64; debugger=$([System.Diagnostics.Debugger]::IsAttached); startup-ready + 60s idle; CPU sample ${IdleSeconds}s"
        cold_window_ready_ms = [Math]::Round($coldWindowReadyMs, 2)
        cold_hotkey_ready_ms = [Math]::Round($coldHotkeyReadyMs, 2)
        idle_cpu_percent_logical_equivalent = [Math]::Round($cpuPercent, 4)
        idle_working_set_peak_mib = [Math]::Round((($wsSamples | Measure-Object -Maximum).Maximum / 1MB), 2)
        idle_private_bytes_peak_mib = [Math]::Round((($privateSamples | Measure-Object -Maximum).Maximum / 1MB), 2)
        warm_show_to_input_ready_p95_ms = if ($sortedWarm.Count -gt 0) { [Math]::Round($sortedWarm[$p95Index], 2) } else { $null }
        warm_samples = $WarmSamples
        warm_measurement_issue = $warmIssue
        visible_panel_20_rows_ready = $visibleRowsReady
        visible_panel_working_set_mib = $visibleWorkingSetMib
        visible_panel_measurement_issue = $visibleIssue
        process_thread_count_context = $process.Threads.Count
        app_owned_thread_count = $appOwnedThreads
        watcher_root_count = $watcherRoots
        app_owned_thread_budget = $threadBudget
        app_owned_thread_verdict = if ($appOwnedThreads -eq $threadBudget) { 'PASS' } else { 'FAIL' }
        icons_cache_bytes_context = if (Test-Path $cache) { (Get-Item $cache).Length } else { $null }
    } | ConvertTo-Json -Compress
}
finally {
    if (-not $process.HasExited) { $process.Kill(); [void]$process.WaitForExit(5000) }
    if ($startupEvent -ne [IntPtr]::Zero) { [void][NimNfr001]::CloseHandle($startupEvent) }
    if ($hotkeyEvent -ne [IntPtr]::Zero) { [void][NimNfr001]::CloseHandle($hotkeyEvent) }
    if ($inputReadyEvent -ne [IntPtr]::Zero) { [void][NimNfr001]::CloseHandle($inputReadyEvent) }
    if ($visibleReadyEvent -ne [IntPtr]::Zero) { [void][NimNfr001]::CloseHandle($visibleReadyEvent) }
    if ($null -eq $previousInputEvent) { Remove-Item Env:NIMBLERUN_TEST_INPUT_READY_EVENT -ErrorAction SilentlyContinue }
    else { $env:NIMBLERUN_TEST_INPUT_READY_EVENT = $previousInputEvent }
    if ($null -eq $previousVisibleEvent) { Remove-Item Env:NIMBLERUN_TEST_VISIBLE_READY_EVENT -ErrorAction SilentlyContinue }
    else { $env:NIMBLERUN_TEST_VISIBLE_READY_EVENT = $previousVisibleEvent }
}
