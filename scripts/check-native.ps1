param([string]$OutputDirectory = '', [string]$Executable = '')
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
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr wp, IntPtr lp, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
}
'@
}
$oldDpiContext = [NativePrototypeCheck]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$process = Start-Process -FilePath $Executable -ArgumentList '--demo', '--offline' -WindowStyle Hidden -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) { throw 'Prototype exited before creating its window (or another instance is running).' }
    } while (!$process.MainWindowHandle -and [DateTime]::UtcNow -lt $deadline)
    $window = $process.MainWindowHandle
    if (!$window) { throw 'No prototype window.' }
    Start-Sleep -Milliseconds 500
    $result = [IntPtr]::Zero
    if ([NativePrototypeCheck]::SendMessageTimeout($window, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 2000, [ref]$result) -eq [IntPtr]::Zero) { throw 'Window did not respond.' }
    $rect = New-Object NativePrototypeCheck+Rect
    if (![NativePrototypeCheck]::GetWindowRect($window, [ref]$rect)) { throw 'Window bounds unavailable.' }
    $bitmap = New-Object System.Drawing.Bitmap(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { if (![NativePrototypeCheck]::PrintWindow($window, $dc, 0)) { throw 'Window render failed.' } }
    finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
    try { $bitmap.Save("$OutputDirectory/native-preview.png", [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
    $process.Refresh()
    $report = "PASS: demo window created and responsive; bounds=$($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top); workingSetBytes=$($process.WorkingSet64)"
    $report | Set-Content "$OutputDirectory/native-window-check.txt"
    Write-Output $report
} finally {
    [NativePrototypeCheck]::SetThreadDpiAwarenessContext($oldDpiContext) | Out-Null
    $process.Refresh()
    if (!$process.HasExited) {
        [NativePrototypeCheck]::PostMessage($process.MainWindowHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        if (!$process.WaitForExit(15000)) { $process.Kill(); throw 'Prototype did not exit within 15 seconds.' }
    }
}
