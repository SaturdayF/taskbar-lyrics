$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$target = Join-Path $root 'artifacts/native install check'
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\TaskbarLyrics.Native.Prototype_is1'
if (Test-Path $target) { throw 'Test install directory already exists; refusing to overwrite.' }
if (Test-Path $key) { throw 'Native prototype is already installed; refusing to alter it.' }
$logs = Join-Path $root 'codex_tmp'
New-Item -ItemType Directory -Force $logs | Out-Null
$setup = Join-Path $root 'artifacts/TaskbarLyrics-Native-0.3.0-setup-x64.exe'
try {
    $install = Start-Process -FilePath $setup -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/NOICONS',"/DIR=`"$target`"", "/LOG=`"$logs/native-install.log`"" -WindowStyle Hidden -PassThru -Wait
    if ($install.ExitCode -ne 0) { throw "Installer failed: $($install.ExitCode)" }
    $exe = Join-Path $target 'TaskbarLyrics.Native.exe'
    if (!(Test-Path $exe) -or !(Test-Path $key)) { throw 'Installed executable or uninstall registration missing.' }
    if ((Get-FileHash $exe).Hash -ne (Get-FileHash "$root/artifacts/native-win-x64/TaskbarLyrics.Native.exe").Hash) { throw 'Installed executable hash mismatch.' }
    & "$root/scripts/check-native.ps1" -Executable $exe -OutputDirectory "$root/artifacts/native-build"
    'PASS: per-user installer, installed binary hash, installed demo window' | Set-Content "$logs/native-install-check.txt"
} finally {
    $uninstall = Join-Path $target 'unins000.exe'
    if (Test-Path $uninstall) {
        $remove = Start-Process -FilePath $uninstall -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=`"$logs/native-uninstall.log`"" -WindowStyle Hidden -Wait -PassThru
        if ($remove.ExitCode -ne 0) { throw "Uninstaller failed: $($remove.ExitCode)" }
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while ((Test-Path $target) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
        if ((Test-Path $target) -or (Test-Path $key)) { throw 'Uninstaller left test directory or registration behind.' }
        'PASS: uninstall removed test directory and registration' | Add-Content "$logs/native-install-check.txt"
    }
}
Get-Content "$logs/native-install-check.txt"
