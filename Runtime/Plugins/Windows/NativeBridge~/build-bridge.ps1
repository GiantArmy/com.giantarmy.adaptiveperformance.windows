param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"

$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
if ($null -eq $cmakeCmd) {
    $vsCmakeCandidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )

    $cmakePath = $vsCmakeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($null -eq $cmakePath) {
        throw "CMake was not found. Install CMake or Visual Studio C++ tools with bundled CMake."
    }
}
else {
    $cmakePath = $cmakeCmd.Source
}

& $cmakePath -S $Root -B $BuildDir -G "Visual Studio 17 2022" -A x64
& $cmakePath --build $BuildDir --config $Configuration --target GiantArmyWindowsThermalBridge ThermalBridgeCli

Write-Host "Built bridge and CLI in $BuildDir ($Configuration)."
