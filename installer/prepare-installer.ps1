# Staging folder + optional redist download for Inno Setup
param(
    [switch]$DownloadRedist,
    [string]$Config = "Release",
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$staging = Join-Path $PSScriptRoot "staging"
$redist = Join-Path $PSScriptRoot "redist"
$release = Join-Path $BuildDir "$Config\AnCoreBrowser.exe"
$resourcesSrc = Join-Path $root "resources"

if (-not (Test-Path $release)) {
    Write-Error "Not found: $release. Run build.ps1 first."
}

New-Item -ItemType Directory -Force -Path $staging, $redist | Out-Null

if (Test-Path $staging) {
    Get-ChildItem $staging -Force | Remove-Item -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $staging "resources") | Out-Null

Copy-Item -Force $release (Join-Path $staging "AnCoreBrowser.exe")
Copy-Item -Recurse -Force (Join-Path $resourcesSrc "*") (Join-Path $staging "resources")

Write-Host "Staging:" -ForegroundColor Green
Get-ChildItem $staging -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($staging.Length + 1)
    Write-Host "  $rel"
}

$redistUrls = @{
    "MicrosoftEdgeWebview2Setup.exe" = "https://go.microsoft.com/fwlink/p/?LinkId=2124703"
    "vc_redist.x64.exe"              = "https://aka.ms/vs/17/release/vc_redist.x64.exe"
}

foreach ($name in $redistUrls.Keys) {
    $dest = Join-Path $redist $name
    if (Test-Path $dest) {
        Write-Host "Redist OK: $name" -ForegroundColor DarkGray
        continue
    }
    if (-not $DownloadRedist) {
        Write-Warning "Missing $dest - run with -DownloadRedist"
        continue
    }
    Write-Host "Downloading $name ..."
    Invoke-WebRequest -Uri $redistUrls[$name] -OutFile $dest -UseBasicParsing
    Write-Host "  -> $dest" -ForegroundColor Green
}

$missing = @()
foreach ($name in $redistUrls.Keys) {
    if (-not (Test-Path (Join-Path $redist $name))) { $missing += $name }
}
if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Required in installer\redist:" -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  $_" }
    Write-Host "Run: .\prepare-installer.ps1 -DownloadRedist" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "Ready for Inno Setup compile." -ForegroundColor Green
