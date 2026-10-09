# Local online test launcher: login + room + 2 lobby UIs
# Usage:
#   .\scripts\start_local_test.ps1           # restart all
#   .\scripts\start_local_test.ps1 -Stop     # stop only
#   .\scripts\start_local_test.ps1 -NoUi     # servers only
#   .\scripts\start_local_test.ps1 -Clients 1

param(
    [switch]$Stop,
    [switch]$NoUi,
    [int]$Clients = 2,
    [int]$LoginPort = 27000,
    [int]$RoomTcp = 27001,
    [int]$RoomUdp = 27002
)

$ErrorActionPreference = "Stop"
$ServerRoot = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $ServerRoot "build"
$EngineRoot = Split-Path -Parent $ServerRoot
$DataDir = Join-Path $ServerRoot "data"
$LogDir = Join-Path $ServerRoot "logs"
$AccountDb = Join-Path $DataDir "account.db"

$exes = @{
    login = Join-Path $Build "login_server.exe"
    room  = Join-Path $Build "room_server.exe"
    ui    = Join-Path $Build "online_client_ui.exe"
}

function Stop-LocalOnline {
    $names = @("online_client_ui", "login_server", "room_server", "join_smoke", "pf")
    $procs = Get-Process -Name $names -ErrorAction SilentlyContinue
    if ($procs) {
        $procs | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 400
        Write-Host "Stopped: $($procs.ProcessName -join ', ')"
    } else {
        Write-Host "No local online processes running."
    }
}

function Assert-Bins {
    foreach ($k in $exes.Keys) {
        if (-not (Test-Path $exes[$k])) {
            throw "Missing binary: $($exes[$k])  (build server/ first)"
        }
    }
}

function Start-LocalOnline {
    Assert-Bins
    New-Item -ItemType Directory -Force -Path $DataDir, $LogDir | Out-Null

    $mingw = "C:\msys64\mingw64\bin"
    if (Test-Path $mingw) {
        $env:PATH = "$Build;$mingw;" + $env:PATH
    } else {
        $env:PATH = "$Build;" + $env:PATH
    }
    $env:PF_ENGINE_ROOT = $EngineRoot

    $login = Start-Process -FilePath $exes.login `
        -ArgumentList @("$LoginPort", $AccountDb) `
        -WorkingDirectory $ServerRoot -WindowStyle Minimized -PassThru
    $room = Start-Process -FilePath $exes.room `
        -ArgumentList @("$RoomTcp", "$RoomUdp") `
        -WorkingDirectory $ServerRoot -WindowStyle Minimized -PassThru
    Start-Sleep -Milliseconds 500

    $uis = @()
    if (-not $NoUi) {
        $n = [Math]::Max(0, $Clients)
        for ($i = 0; $i -lt $n; $i++) {
            $uis += Start-Process -FilePath $exes.ui `
                -WorkingDirectory $ServerRoot -WindowStyle Normal -PassThru
            Start-Sleep -Milliseconds 250
        }
    }

    Write-Host ""
    Write-Host "=== Local online test ==="
    Write-Host "  engine : $EngineRoot"
    Write-Host "  login  : pid=$($login.Id)  tcp=$LoginPort  db=$AccountDb"
    Write-Host "  room   : pid=$($room.Id)  tcp=$RoomTcp udp=$RoomUdp"
    if ($uis.Count -gt 0) {
        Write-Host "  ui     : $($uis.Count) client(s)  pids=$($uis.Id -join ',')"
    } else {
        Write-Host "  ui     : (skipped)"
    }
    Write-Host ""
    Write-Host "Flow: login -> Online -> create room (client A) -> join (client B) -> start match"
    Write-Host "Stop:  .\scripts\start_local_test.ps1 -Stop"
}

Stop-LocalOnline
if ($Stop) { return }
Start-LocalOnline
