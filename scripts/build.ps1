param([string]$Dotnet = 'dotnet', [switch]$Publish)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
& $Dotnet run --project "$root/tests/TaskbarLyrics.Tests" -c Release
if ($LASTEXITCODE -ne 0) { throw 'Core checks failed' }
& $Dotnet build "$root/src/TaskbarLyrics.App" -c Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($Publish) {
    & $Dotnet publish "$root/src/TaskbarLyrics.App" -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o "$root/artifacts/win-x64"
    if ($LASTEXITCODE -ne 0) { throw 'Publish failed' }
    Copy-Item "$root/README.md","$root/LICENSE" "$root/artifacts/win-x64"
    Compress-Archive -Path "$root/artifacts/win-x64/*" -DestinationPath "$root/artifacts/TaskbarLyrics-v0.3.1-win-x64.zip" -Force
}
