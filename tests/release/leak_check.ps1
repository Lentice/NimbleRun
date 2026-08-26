# NFR-001 leak check (design-spec §12.4): repeated show/hide must not grow
# GDI objects, USER objects, Handles, or Private Bytes. Separate from
# nfr001_probe.ps1 (idle/latency budgets) and release_evidence.ps1 (build +
# suite); run manually before a release alongside those two.

param(
    [string]$ExePath = "",
    [int]$Cycles = 1000,
    [int]$SettleSeconds = 5
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot\..\..").Path
if ($ExePath -eq "") { $ExePath = "$workspace\build\NimbleRun.exe" }
$exe = (Resolve-Path $ExePath).Path

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NimLeak {
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder b, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessageW(string n);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string n);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenEventW(uint a, bool i, string n);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
'@

# GetGuiResources flags.
$GR_GDIOBJECTS = 0
$GR_USEROBJECTS = 1

$showMessage = [NimLeak]::RegisterWindowMessageW('NimbleRun.ShowPanel')
$kStartupReadyEvent = 'Local\NimbleRun.StartupReady'

function Find-NimbleWindow {
    $script:found = [IntPtr]::Zero
    $callback = [NimLeak+EnumWindowsProc]{ param($h, $l)
        $name = New-Object System.Text.StringBuilder 128
        [void][NimLeak]::GetClassNameW($h, $name, $name.Capacity)
        if ($name.ToString() -eq 'NimbleRun.Phase0Probe') { $script:found = $h; return $false }
        return $true
    }
    [void][NimLeak]::EnumWindows($callback, [IntPtr]::Zero)
    return $script:found
}

function Wait-Until([scriptblock]$Condition, [int]$TimeoutMs, [string]$Label) {
    $deadline = [Environment]::TickCount64 + $TimeoutMs
    while ([Environment]::TickCount64 -lt $deadline) {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 5
    }
    throw "Timed out waiting for $Label"
}

function Sample([System.Diagnostics.Process]$p) {
    $p.Refresh()
    [pscustomobject]@{
        gdi = [NimLeak]::GetGuiResources($p.Handle, $GR_GDIOBJECTS)
        user = [NimLeak]::GetGuiResources($p.Handle, $GR_USEROBJECTS)
        handles = $p.HandleCount
        private_bytes = $p.PrivateMemorySize64
    }
}

$process = Start-Process -FilePath $exe -PassThru
$startupEvent = [IntPtr]::Zero
try {
    Wait-Until { $process.HasExited -or (Find-NimbleWindow) -ne [IntPtr]::Zero } 30000 'main window'
    if ($process.HasExited) { throw "NimbleRun exited with code $($process.ExitCode)" }
    $startupEvent = [NimLeak]::OpenEventW(0x00100000, $false, $kStartupReadyEvent)
    if ($startupEvent -ne [IntPtr]::Zero) {
        [void][NimLeak]::WaitForSingleObject($startupEvent, 5000)
    }
    $window = Find-NimbleWindow
    Start-Sleep -Seconds $SettleSeconds

    $before = Sample $process

    for ($i = 0; $i -lt $Cycles; $i++) {
        [void][NimLeak]::PostMessageW($window, $showMessage, [IntPtr]::Zero, [IntPtr]::Zero)
        Wait-Until { [NimLeak]::IsWindowVisible($window) } 5000 "cycle $i show"
        $edit = [NimLeak]::FindWindowExW($window, [IntPtr]::Zero, 'EDIT', $null)
        [void][NimLeak]::PostMessageW($edit, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero)  # WM_KEYDOWN VK_ESCAPE
        Wait-Until { -not [NimLeak]::IsWindowVisible($window) } 5000 "cycle $i hide"
    }

    # Let any deferred teardown (e.g. icon request drain) settle before the
    # final sample so a normal async cleanup lag doesn't read as a leak.
    Start-Sleep -Seconds $SettleSeconds
    $after = Sample $process

    [pscustomobject]@{
        cycles = $Cycles
        before = $before
        after = $after
        gdi_delta = $after.gdi - $before.gdi
        user_delta = $after.user - $before.user
        handle_delta = $after.handles - $before.handles
        private_bytes_delta = $after.private_bytes - $before.private_bytes
    } | ConvertTo-Json -Compress
}
finally {
    if (-not $process.HasExited) { $process.Kill(); [void]$process.WaitForExit(5000) }
    if ($startupEvent -ne [IntPtr]::Zero) { [void][NimLeak]::CloseHandle($startupEvent) }
}
