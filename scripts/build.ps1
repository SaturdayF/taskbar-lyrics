param([string]$Iscc = '')
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/build-native.ps1" -Iscc $Iscc
