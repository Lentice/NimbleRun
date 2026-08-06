# Focused lifecycle check for NR-002 (single instance and tray lifecycle).
#
# Steps:
#   1. Start a first NimbleRun instance; its main window must exist and start hidden.
#   2. Start a second instance; it must notify the first to show the panel and
#      exit with code 0, while the first instance survives.
#   3. Post the tray Exit command message to the first instance's window; the
#      process must exit cleanly.
#   4. All test processes are guaranteed to be terminated on exit.
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
$first = $null
$second = $null
try {
    # 1. First instance becomes the resident process.
    $first = Start-Process -FilePath $exe -PassThru

    Wait-Until { (Get-ClassWindows $WindowClass).Count -gt 0 } 5000 'first instance main window'
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

    # 2. Second instance must wake the first and then exit.
    $second = Start-Process -FilePath $exe -PassThru
    if (-not $second.WaitForExit(5000)) { throw 'Second instance did not exit within 5s' }
    if ($second.ExitCode -ne 0) { throw "Second instance exited with code $($second.ExitCode), expected 0" }

    if ($first.HasExited) { throw 'First instance exited after second launch' }
    Wait-Until { [NimLifecycle]::IsWindowVisible($hwnd) } 5000 'first instance shows panel after wake'

    $windows = Get-ClassWindows $WindowClass
    if ($windows.Count -ne 1) {
        throw "Expected exactly one main window after second launch, found $($windows.Count)"
    }

    # 3. Tray Exit command must terminate the first instance cleanly.
    [void][NimLifecycle]::PostMessageW($hwnd, $ExitMessage, [IntPtr]::Zero, [IntPtr]::Zero)
    # NR-049: WM_DESTROY now joins any in-flight catalog rebuild before tearing
    # down, so an immediate Exit during the startup full rebuild can take as
    # long as the scan. Warm that is ~0.3 s on the dev machine, but the first
    # run after a build (cold file cache / AV scan) measured ~21.5 s. The bound
    # is for the process to terminate with code 0, not for shutdown speed.
    if (-not $first.WaitForExit(30000)) { throw 'First instance did not exit after tray Exit command' }
    if ($first.ExitCode -ne 0) { throw "First instance exited with code $($first.ExitCode), expected 0" }

    Write-Output 'NR-002 lifecycle check PASSED'
}
finally {
    foreach ($p in @($first, $second)) {
        if ($null -ne $p -and -not $p.HasExited) {
            $p.Kill()
            $p.WaitForExit(2000)
        }
    }
}
