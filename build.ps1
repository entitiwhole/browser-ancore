# AnCore Browser — сборка без cmake в PATH
$ErrorActionPreference = "Stop"

$cmakeCandidates = @(
    "${env:ProgramFiles}\CMake\bin\cmake.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

$cmake = $cmakeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $cmake) {
    Write-Error "CMake не найден. Установите CMake или Visual Studio Build Tools с компонентом C++ CMake."
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root "build"
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

Set-Location $buildDir

# Генератор из существующего кэша или VS 18 / VS 17
$generator = $null
$cache = Join-Path $buildDir "CMakeCache.txt"
if (Test-Path $cache) {
    $genLine = Select-String -Path $cache -Pattern '^CMAKE_GENERATOR:' | Select-Object -First 1
    if ($genLine) { $generator = ($genLine.Line -split '=', 2)[1].Trim() }
}
if (-not $generator) {
    $generators = @("Visual Studio 18 2026", "Visual Studio 17 2022")
    foreach ($g in $generators) {
        & $cmake .. -G $g -A x64 2>$null
        if ($LASTEXITCODE -eq 0) { $generator = $g; break }
    }
}
if (-not $generator) {
    & $cmake .. -A x64
} else {
    & $cmake .. -G $generator -A x64
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build . --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $buildDir "Release\AnCoreBrowser.exe"
$deploy = Join-Path $root "deploy\AnCoreBrowser\Application\AnCoreBrowser.exe"
Write-Host ""
Write-Host "Готово:" -ForegroundColor Green
Write-Host "  $exe"
if (Test-Path $deploy) { Write-Host "  $deploy" }
