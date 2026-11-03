param([string]$Iscc = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if ($Iscc) { $Iscc = (Resolve-Path -LiteralPath $Iscc).Path }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++ and Windows 10/11 SDK.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC x64 build tools not found.' }
$msvc = Get-ChildItem "$vs/VC/Tools/MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdk = Get-ChildItem "$sdkRoot/Include" -Directory | Where-Object { Test-Path "$($_.FullName)/cppwinrt/winrt/Windows.Media.Control.h" } | Sort-Object Name -Descending | Select-Object -First 1
if (!$sdk) { throw 'Windows SDK with C++/WinRT headers not found.' }
$compiler = Join-Path $msvc.FullName 'bin/Hostx64/x64/cl.exe'
$rc = "$sdkRoot/bin/$($sdk.Name)/x64/rc.exe"
$savedPath = $env:PATH
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
$env:PATH = "$(Split-Path $compiler);$env:PATH"
$env:INCLUDE = "$($msvc.FullName)/include;$($sdk.FullName)/ucrt;$($sdk.FullName)/shared;$($sdk.FullName)/um;$($sdk.FullName)/winrt;$($sdk.FullName)/cppwinrt"
$env:LIB = "$($msvc.FullName)/lib/x64;$sdkRoot/Lib/$($sdk.Name)/ucrt/x64;$sdkRoot/Lib/$($sdk.Name)/um/x64"
$out = Join-Path $root 'artifacts/native-win-x64'
$build = Join-Path $root 'artifacts/native-build'
New-Item -ItemType Directory -Force $out | Out-Null
New-Item -ItemType Directory -Force $build | Out-Null
Push-Location "$root/native"
try {
    & $rc /nologo /fo "$build/app.res" app.rc
    if ($LASTEXITCODE -ne 0) { throw 'Native resources failed.' }
    $flags = @('/nologo','/std:c++20','/EHsc','/utf-8','/W4','/WX','/O1','/MT','/DUNICODE','/D_UNICODE','/DNDEBUG')
    & $compiler @flags "/Fo$build/main.obj" main.cpp "$build/app.res" "/Fe$out/TaskbarLyrics.Native.exe" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF user32.lib gdi32.lib shell32.lib comdlg32.lib winhttp.lib windowsapp.lib normaliz.lib ole32.lib oleaut32.lib uiautomationcore.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native application build failed.' }
    & $compiler @flags "/Fo$build/tests.obj" tests.cpp "/Fe$build/native-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Native tests build failed.' }
    & "$build/native-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Native checks failed.' }
    & "$(Split-Path $compiler)/dumpbin.exe" /nologo /dependents "$out/TaskbarLyrics.Native.exe" | Set-Content "$build/dependencies.txt"
    if ($LASTEXITCODE -ne 0) { throw 'Dependency inspection failed.' }
    Copy-Item "$root/native/README.md" "$out/README.md"
    Copy-Item "$root/LICENSE" "$out/LICENSE"
    Compress-Archive -Path "$out/TaskbarLyrics.Native.exe", "$out/README.md", "$out/LICENSE" -DestinationPath "$root/artifacts/TaskbarLyrics-Native-0.1.2-prototype-win-x64.zip" -Force
    if ($Iscc) {
        & $Iscc "$root/native/installer.iss"
        if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
    }
    Get-Item "$out/TaskbarLyrics.Native.exe", "$root/artifacts/TaskbarLyrics-Native-0.1.2-prototype-win-x64.zip" | Select-Object FullName,Length
} finally {
    Pop-Location
    $env:PATH = $savedPath
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
}
