# Focused lifecycle check for NR-002/NR-110 (single instance and tray lifecycle).
#
# Steps:
#   1. Gate the first instance immediately before CreateWindowExW.
#   2. Start a second instance during that gate; it must rendezvous, notify the
#      first exactly once after HWND creation, and exit with code 0.
#   3. Repeat the normal post-HWND second-launch path.
#   4. Post the tray Exit command message to the first instance's window; the
#      process must exit cleanly.
#   5. All test processes are guaranteed to be terminated on exit.
#
# Requires pwsh and a built NimbleRun.exe. Run via ctest or manually:
#   pwsh -NoProfile -File tests/integration/lifecycle_check.ps1 -ExePath build/NimbleRun.exe
#
# Fails fast if another NimbleRun instance is already running; close it first.

param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class NimLifecycle {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string className, string windowName);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder className, int maxCount);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern uint RegisterWindowMessageW(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateEventW(IntPtr attributes, bool manualReset,
        bool initialState, string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateSemaphoreW(IntPtr attributes, int initialCount,
        int maximumCount, string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr OpenEventW(uint access, bool inheritHandle, string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr OpenSemaphoreW(uint access, bool inheritHandle, string name);
    [DllImport("kernel32.dll")]
    public static extern bool SetEvent(IntPtr handle);
    [DllImport("kernel32.dll")]
    public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

# Mirrors src/app_host/main.cpp: window class, registered wake message name,
# and the tray Exit command message (WM_APP + 5).
$WindowClass = 'NimbleRun.Phase0Probe'
$ShowPanelMessage = [NimLifecycle]::RegisterWindowMessageW('NimbleRun.ShowPanel')
$ExitMessage = 0x8000 + 5

function Get-ClassWindows([string]$className) {
    $hits = New-Object System.Collections.Generic.List[IntPtr]
    $cb = [NimLifecycle+EnumWindowsProc]{
        param($h, $l)
        $sb = New-Object System.Text.StringBuilder 256
        [void][NimLifecycle]::GetClassNameW($h, $sb, $sb.Capacity)
        if ($sb.ToString() -eq $className) { [void]$hits.Add($h) }
        return $true
    }
    [void][NimLifecycle]::EnumWindows($cb, [IntPtr]::Zero)
    return $hits
}

function Wait-Until([scriptblock]$condition, [int]$timeoutMs, [string]$label) {
    $deadline = [Environment]::TickCount + $timeoutMs
    while ([Environment]::TickCount -lt $deadline) {
        if (& $condition) { return }
        Start-Sleep -Milliseconds 50
    }
    throw "Timed out waiting for: $label"
}

$exe = (Resolve-Path $ExePath).Path
$testBase = "Local\NimbleRun.TestStartup.$([Guid]::NewGuid().ToString('N'))"
$readyHandle = [NimLifecycle]::CreateEventW(
    [IntPtr]::Zero, $true, $false, "$testBase.ready")
$releaseHandle = [NimLifecycle]::CreateEventW(
    [IntPtr]::Zero, $true, $false, "$testBase.release")
$showSemaphore = [NimLifecycle]::CreateSemaphoreW(
    [IntPtr]::Zero, 0, 8, "$testBase.show")
if ($readyHandle -eq [IntPtr]::Zero -or
    $releaseHandle -eq [IntPtr]::Zero -or
    $showSemaphore -eq [IntPtr]::Zero) {
    throw 'Failed to create NR-110 lifecycle test handles'
}
$previousGate = $env:NIMBLERUN_TEST_STARTUP_GATE
$previousShowSemaphore = $env:NIMBLERUN_TEST_SHOW_SEMAPHORE
$env:NIMBLERUN_TEST_SHOW_SEMAPHORE = "$testBase.show"
$first = $null
$second = $null
$normalSecond = $null
$startupReadyHandle = [IntPtr]::Zero
try {
    # 1. Keep the existing normal lifecycle assertions: a fresh panel starts
    # hidden, a post-HWND second launch shows it exactly once, and Exit works.
    Remove-Item Env:NIMBLERUN_TEST_STARTUP_GATE -ErrorAction SilentlyContinue
    $first = Start-Process -FilePath $exe -PassThru
    Wait-Until { (Get-ClassWindows $WindowClass).Count -gt 0 } 5000 'first instance main window'
    $startupReadyHandle = [NimLifecycle]::OpenEventW(0x00100000, $false,
        'Local\NimbleRun.StartupReady')
    if ($startupReadyHandle -eq [IntPtr]::Zero) {
        throw 'Could not open the startup rendezvous event'
    }
    if ([NimLifecycle]::WaitForSingleObject($startupReadyHandle, 5000) -ne 0) {
        throw 'First instance did not publish startup readiness'
    }
    $windows = Get-ClassWindows $WindowClass
    if ($windows.Count -ne 1) { throw "Expected exactly one main window, found $($windows.Count)" }
    $hwnd = $windows[0]
    [uint32]$ownerPid = 0
    [void][NimLifecycle]::GetWindowThreadProcessId($hwnd, [ref]$ownerPid)
    if ($ownerPid -ne $first.Id) {
        throw "Main window is owned by process $ownerPid, not the started process $($first.Id). " +
              'Is another NimbleRun instance already running? Close it and rerun.'
    }
    if ([NimLifecycle]::IsWindowVisible($hwnd)) { throw 'Panel should start hidden' }

    $normalSecond = Start-Process -FilePath $exe -PassThru
    if (-not $normalSecond.WaitForExit(5000)) { throw 'Normal second instance did not exit within 5s' }
    if ($normalSecond.ExitCode -ne 0) { throw "Normal second instance exited with code $($normalSecond.ExitCode), expected 0" }
    if ([NimLifecycle]::WaitForSingleObject($showSemaphore, 5000) -ne 0) {
        throw 'Normal second launch did not deliver a show request'
    }
    if ([NimLifecycle]::WaitForSingleObject($showSemaphore, 0) -ne 258) {
        throw 'Normal second launch delivered more than one show request'
    }
    Wait-Until { [NimLifecycle]::IsWindowVisible($hwnd) } 5000 'first instance shows panel after wake'
    [void][NimLifecycle]::PostMessageW($hwnd, $ExitMessage, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $first.WaitForExit(30000)) { throw 'First instance did not exit after normal tray Exit' }
    if ($first.ExitCode -ne 0) { throw "First instance exited with code $($first.ExitCode), expected 0" }
    [void][NimLifecycle]::CloseHandle($startupReadyHandle)
    $startupReadyHandle = [IntPtr]::Zero

    # 2. The first process now owns the mutex, then stops before CreateWindowExW.
    $env:NIMBLERUN_TEST_STARTUP_GATE = $testBase
    $first = Start-Process -FilePath $exe -PassThru
    if ([NimLifecycle]::WaitForSingleObject($readyHandle, 5000) -ne 0) {
        throw 'First instance did not reach the pre-HWND startup gate'
    }

    # This is the production rendezvous, not a timing probe: it is created
    # before the mutex and signaled immediately after CreateWindowExW succeeds.
    $startupReadyHandle = [NimLifecycle]::OpenEventW(0x00100000, $false,
        'Local\NimbleRun.StartupReady')
    if ($startupReadyHandle -eq [IntPtr]::Zero) {
        throw 'Could not open the startup rendezvous event'
    }

    # 2. Launch while the first process is definitely pre-HWND, then release it.
    $second = Start-Process -FilePath $exe -PassThru
    if (-not [NimLifecycle]::SetEvent($releaseHandle)) {
        throw 'Failed to release the pre-HWND startup gate'
    }
    if ([NimLifecycle]::WaitForSingleObject($startupReadyHandle, 5000) -ne 0) {
        throw 'First instance did not publish HWND startup readiness'
    }

    $windows = Get-ClassWindows $WindowClass
    if ($windows.Count -ne 1) { throw "Expected exactly one main window, found $($windows.Count)" }
    $hwnd = $windows[0]

    [uint32]$ownerPid = 0
    [void][NimLifecycle]::GetWindowThreadProcessId($hwnd, [ref]$ownerPid)
    if ($ownerPid -ne $first.Id) {
        throw "Main window is owned by process $ownerPid, not the started process $($first.Id). " +
              'Is another NimbleRun instance already running? Close it and rerun.'
    }

    if (-not $second.WaitForExit(5000)) { throw 'Second instance did not exit within 5s' }
    if ($second.ExitCode -ne 0) { throw "Second instance exited with code $($second.ExitCode), expected 0" }

    if ($first.HasExited) { throw 'First instance exited after second launch' }
    if ([NimLifecycle]::WaitForSingleObject($showSemaphore, 5000) -ne 0) {
        throw 'Pre-HWND second launch did not deliver a show request'
    }
    if ([NimLifecycle]::WaitForSingleObject($showSemaphore, 0) -ne 258) {
        throw 'Pre-HWND second launch delivered more than one show request'
    }
    Wait-Until { [NimLifecycle]::IsWindowVisible($hwnd) } 5000 'first instance shows panel after wake'

    $windows = Get-ClassWindows $WindowClass
    if ($windows.Count -ne 1) {
        throw "Expected exactly one main window after second launch, found $($windows.Count)"
    }

    # 3. Tray Exit command must terminate the race-test first instance cleanly.
    [void][NimLifecycle]::PostMessageW($hwnd, $ExitMessage, [IntPtr]::Zero, [IntPtr]::Zero)
    # NR-049: WM_DESTROY now joins any in-flight catalog rebuild before tearing
    # down, so an immediate Exit during the startup full rebuild can take as
    # long as the scan. Warm that is ~0.3 s on the dev machine, but the first
    # run after a build (cold file cache / AV scan) measured ~21.5 s. The bound
    # is for the process to terminate with code 0, not for shutdown speed.
    if (-not $first.WaitForExit(30000)) { throw 'First instance did not exit after tray Exit command' }
    if ($first.ExitCode -ne 0) { throw "First instance exited with code $($first.ExitCode), expected 0" }

    Write-Output 'NR-110 race lifecycle check PASSED'
    Write-Output 'NR-002 normal lifecycle check PASSED'
}
finally {
    foreach ($p in @($first, $second, $normalSecond)) {
        if ($null -ne $p -and -not $p.HasExited) {
            $p.Kill()
            $p.WaitForExit(2000)
        }
    }
    foreach ($handle in @($startupReadyHandle, $readyHandle, $releaseHandle, $showSemaphore)) {
        if ($handle -ne [IntPtr]::Zero) { [void][NimLifecycle]::CloseHandle($handle) }
    }
    if ($null -eq $previousGate) {
        Remove-Item Env:NIMBLERUN_TEST_STARTUP_GATE -ErrorAction SilentlyContinue
    } else {
        $env:NIMBLERUN_TEST_STARTUP_GATE = $previousGate
    }
    if ($null -eq $previousShowSemaphore) {
        Remove-Item Env:NIMBLERUN_TEST_SHOW_SEMAPHORE -ErrorAction SilentlyContinue
    } else {
        $env:NIMBLERUN_TEST_SHOW_SEMAPHORE = $previousShowSemaphore
    }
}
