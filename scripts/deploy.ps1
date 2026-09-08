#!/usr/bin/env pwsh
# Deploys this App Lab project to the UNO Q over the network and (re)starts it.
#
# Usage: scripts/deploy.ps1 [board-user@host]
# Defaults to $env:BOARD_HOST, or arduino@<BOARD_IP> (env var $env:BOARD_IP, or 10.0.0.195) if unset.
#
# Requires: tar.exe (bundled with Windows 10 1803+/Windows 11), the OpenSSH client
# (Settings > Optional Features, or `Add-WindowsCapability -Online -Name OpenSSH.Client`),
# and SSH key-based access to the board (password auth doesn't work non-interactively --
# run scripts/setup_ssh_key.py once to set this up).

param(
    [Parameter(Position = 0, Mandatory = $false)]
    [string]$BoardHostArg,
    [Parameter(Position = 1, Mandatory = $false)]
    [string]$AppName = 'smoothsensors03'
)

$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string]$Command,
        [Parameter(Mandatory)][string]$ErrorMessage
    )
    cmd /c $Command;
    if ($LASTEXITCODE -ne 0) {
        throw "==> ERROR: $ErrorMessage (exit code $LASTEXITCODE), last command was: [$Command]";
    }
}

function extractWithRegex([string]$str, [string]$patternWithOneGroupMarker)
{
    $ret = $null;
    if ($str -match $patternWithOneGroupMarker)
    {
        $ret = $matches[1];
    }
    return $ret;
}

if ([string]::IsNullOrEmpty($BoardHostArg) -and [string]::IsNullOrEmpty($env:BOARD_HOST) -and [string]::IsNullOrEmpty($env:BOARD_IP)) {
    if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
        Write-Error "==> ERROR: arduino-cli not found in PATH (install from https://arduino.github.io/arduino-cli/installation/)";
        exit 1;
    }
    Write-Host "==> BOARD_HOST and BOARD_IP not set, trying to auto-detect board IP from arduino-cli" -ForegroundColor Yellow;
    $env:BOARD_IP = extractWithRegex (arduino-cli board list | select-Object -Skip 1) "([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+) ";
    Write-Host "==> Detected BOARD_IP=$env:BOARD_IP" -ForegroundColor Green;
}

function Get-BoardHost {
    $BoardIp = if (-not [string]::IsNullOrEmpty($BoardHostArg)) { (extractWithRegex $BoardHostArg "([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)")} 
                elseif ($env:BOARD_IP) { $env:BOARD_IP } 
                else { '10.0.0.195' };
    if (-not (Test-Connection -ComputerName $BoardIp -Count 1 -Quiet)) {
        Write-Error "==> ERROR: Board not reachable at $BoardIp (check BOARD_IP or network)";
        exit 1;
    }
    $BoardHost = if (-not [string]::IsNullOrEmpty($BoardHostArg)) { $BoardHostArg }
                elseif (-not [string]::IsNullOrEmpty($env:BOARD_HOST)) { $env:BOARD_HOST }
                else { "arduino@$BoardIp" };
    return $BoardHost;
}


$BoardHost = Get-BoardHost;
$RemoteDir = "ArduinoApps/$AppName";
$LocalDir = Split-Path -Parent $PSScriptRoot;

Write-Host "==> Syncing code to ${BoardHost}:${RemoteDir}" -ForegroundColor Green;
$syncCmd = "tar czf - -C `"$LocalDir`" --exclude=python/model --exclude=python/__pycache__ sketch python app.yaml | ssh $BoardHost `"mkdir -p $RemoteDir && tar xzf - -C $RemoteDir`"";
Invoke-Checked -Command $syncCmd -ErrorMessage "sync to board failed";

$checkCmd = "ssh $BoardHost `"test -d $RemoteDir/python/model/am`"";
cmd /c $checkCmd;
if ($LASTEXITCODE -eq 0) {
    Write-Host "==> Vosk model already present on board, skipping (delete $RemoteDir/python/model there to force a re-upload)" -ForegroundColor Yellow;
} else {
    Write-Host "==> Vosk model not found on board, uploading python/model/ (this is large and may take a while)" -ForegroundColor Yellow;
    $modelCmd = "tar czf - -C `"$LocalDir/python`" model | ssh $BoardHost `"mkdir -p $RemoteDir/python && tar xzf - -C $RemoteDir/python`"";
    Invoke-Checked -Command $modelCmd -ErrorMessage "model upload failed";
}

Write-Host "==> Restarting the app (compiles + uploads the sketch, restarts the Python app)" -ForegroundColor Green;
$restartCmd = "ssh $BoardHost `"arduino-app-cli app restart $RemoteDir`"";
cmd /c $restartCmd;
if ($LASTEXITCODE -ne 0) {
    Write-Host "==> restart failed (arduino-app-cli sometimes leaves the app stopped instead of restarting it), falling back to stop + start" -ForegroundColor Yellow;
    cmd /c "ssh $BoardHost `"arduino-app-cli app stop $RemoteDir`"" | Out-Null;
    $startCmd = "ssh $BoardHost `"arduino-app-cli app start $RemoteDir`"";
    Invoke-Checked -Command $startCmd -ErrorMessage "app start failed";
}

Write-Host "==> Done. Tail logs with: ssh $BoardHost arduino-app-cli app logs $RemoteDir --follow" -ForegroundColor Green;

