# Сборка AnCore Browser + установщик Inno Setup
param(
    [string]$InnoCompiler = "",
    [switch]$SkipBuild,
    [switch]$DownloadRedist
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$iss = Join-Path $PSScriptRoot "AnCoreBrowser.iss"

if (-not $SkipBuild) {
    & (Join-Path $root "build.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($DownloadRedist) {
    & (Join-Path $PSScriptRoot "prepare-installer.ps1") -DownloadRedist
} else {
    & (Join-Path $PSScriptRoot "prepare-installer.ps1")
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $InnoCompiler) {
    $candidates = @(
        "E:\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
    )
    $InnoCompiler = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $InnoCompiler -or -not (Test-Path $InnoCompiler)) {
    $gui = "E:\Inno Setup 6\Compil32.exe"
    if (Test-Path $gui) {
        Write-Host "ISCC.exe не найден. Скомпилируйте скрипт в Inno Setup (Compil32):" -ForegroundColor Yellow
        Write-Host "  $iss"
        exit 0
    }
    Write-Error "Inno Setup 6 не найден (нужен ISCC.exe или Compil32.exe)"
}

$outputDir = Join-Path $PSScriptRoot "output"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

Write-Host "Inno Setup: $InnoCompiler" -ForegroundColor Cyan
& $InnoCompiler $iss
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$setup = Get-ChildItem $outputDir -Filter "AnCoreBrowser-Setup-*.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Host ""
Write-Host "Установщик:" -ForegroundColor Green
if ($setup) { Write-Host "  $($setup.FullName)" }
