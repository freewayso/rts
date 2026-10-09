# Deploy MinGW/protobuf runtime DLLs next to server/client exes.
param(
  [string]$MingwBin = $env:MINGW_BIN,
  [string]$BuildDir = ""
)
$ErrorActionPreference = "Stop"
if (-not $MingwBin) { $MingwBin = "C:\msys64\mingw64\bin" }
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build" }

if (-not (Test-Path (Join-Path $MingwBin "libgcc_s_seh-1.dll"))) {
  Write-Error "MinGW bin not found: $MingwBin (set MINGW_BIN)"
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$names = @(
  "libgcc_s_seh-1.dll",
  "libstdc++-6.dll",
  "libwinpthread-1.dll",
  "libprotobuf.dll",
  "libprotobuf-lite.dll",
  "zlib1.dll",
  "libutf8_validity.dll",
  "libutf8_range.dll"
)
$copied = 0
$skipped = 0
foreach ($n in $names) {
  $src = Join-Path $MingwBin $n
  if (Test-Path $src) {
    $dst = Join-Path $BuildDir $n
    try {
      Copy-Item -Force $src $dst -ErrorAction Stop
      $copied++
    } catch {
      $skipped++
      Write-Host "skip locked: $n"
    }
  }
}
Get-ChildItem (Join-Path $MingwBin "libabsl*.dll") -ErrorAction SilentlyContinue | ForEach-Object {
  $dst = Join-Path $BuildDir $_.Name
  try {
    Copy-Item -Force $_.FullName $dst -ErrorAction Stop
    $copied++
  } catch {
    $skipped++
    Write-Host ("skip locked: " + $_.Name)
  }
}
Write-Host "Deployed $copied runtime DLLs -> $BuildDir (skipped $skipped)"
