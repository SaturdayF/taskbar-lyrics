param([string]$OutputDirectory = '', [string]$Executable = '', [switch]$Docked)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Executable) { $Executable = "$root/artifacts/native-win-x64/TaskbarLyrics.Native.exe" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root 'artifacts/native-build' }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing
if (!('NativePrototypeCheck' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NativePrototypeCheck {
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string name);
    public static IntPtr FindByClass(string cls) { return FindWindow(cls, null); }
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
}
'@
}
$oldDpiContext = [NativePrototypeCheck]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$arguments = @('--demo','--offline')
if (!$Docked) { $arguments += '--floating' }
$process = Start-Process -FilePath $Executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) { throw 'Prototype exited before creating its window (or another instance is running).' }
    } while ($process.MainWindowHandle -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    $window = $process.MainWindowHandle
    if ($window -eq [IntPtr]::Zero) { throw 'No visible prototype window (taskbar may have no free space).' }
    Start-Sleep -Milliseconds 1500
    $result = [IntPtr]::Zero
    if ([NativePrototypeCheck]::SendMessageTimeout($window, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$result) -eq [IntPtr]::Zero) { throw "Window did not respond. HWND=$window Win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
    $rect = New-Object NativePrototypeCheck+Rect
    if (![NativePrototypeCheck]::GetWindowRect($window, [ref]$rect)) { throw 'Window bounds unavailable.' }
    if ($Docked) {
        $barRect = New-Object NativePrototypeCheck+Rect
        $bar = [NativePrototypeCheck]::FindByClass('Shell_TrayWnd')
        if ($bar -eq [IntPtr]::Zero -or ![NativePrototypeCheck]::GetWindowRect($bar, [ref]$barRect)) { throw 'Primary taskbar unavailable.' }
        if ($rect.Top -lt $barRect.Top -or $rect.Bottom -gt $barRect.Bottom -or $rect.Left -lt $barRect.Left -or $rect.Right -gt $barRect.Right) { throw 'Lyrics are outside taskbar bounds.' }
        $probePath = Join-Path $OutputDirectory 'native-taskbar-probe.json'
        $probe = Start-Process -FilePath $Executable -ArgumentList '--taskbar-probe', "`"$probePath`"" -WindowStyle Hidden -Wait -PassThru
        if ($probe.ExitCode -ne 0) { throw 'Taskbar probe failed.' }
        $snapshot = Get-Content $probePath -Raw | ConvertFrom-Json
        if (!$snapshot.reliable) { throw 'Taskbar controls could not be read.' }
        foreach ($span in $snapshot.occupied) {
            if ($rect.Left -lt $span[1] -and $rect.Right -gt $span[0]) { throw 'Lyrics overlap a taskbar control.' }
        }
    }
    $bitmap = New-Object System.Drawing.Bitmap(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { if (![NativePrototypeCheck]::PrintWindow($window, $dc, 0)) { throw 'Window render failed.' } }
    finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
    try { $bitmap.Save("$OutputDirectory/native-preview.png", [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
    $process.Refresh()
    $report = "PASS: demo window created and responsive; docked=$Docked; bounds=$($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top); workingSetBytes=$($process.WorkingSet64)"
    $report | Set-Content "$OutputDirectory/native-window-check.txt"
    Write-Output $report
} finally {
    [NativePrototypeCheck]::SetThreadDpiAwarenessContext($oldDpiContext) | Out-Null
    $process.Refresh()
    if (!$process.HasExited) {
        $closeWindow = [NativePrototypeCheck]::FindByClass('TaskbarLyricsNative')
        if ($closeWindow -ne [IntPtr]::Zero) {
            $posted = [NativePrototypeCheck]::PostMessage($closeWindow, 0x111, [IntPtr]::new(104), [IntPtr]::Zero)
            if (!$posted) { throw 'Unable to send the exit command.' }
        }
        if (!$process.WaitForExit(15000)) { $process.Kill(); throw 'Prototype did not exit within 15 seconds.' }
    }
}
