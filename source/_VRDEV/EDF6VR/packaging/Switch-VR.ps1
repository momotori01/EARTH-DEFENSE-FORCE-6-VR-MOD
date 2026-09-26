[CmdletBinding()]
# NoLaunch remains accepted for older offline tools; launching is never performed.
param([ValidateSet('VR','Flat')][string]$Mode, [switch]$NoLaunch)
$ErrorActionPreference = 'Stop'

function Set-EDF6VRMode {
    param([Parameter(Mandatory)][string]$GameDirectory,
          [Parameter(Mandatory)][ValidateSet('VR','Flat')][string]$SelectedMode)
    $root = (Resolve-Path -LiteralPath $GameDirectory).Path
    if (!(Test-Path -LiteralPath (Join-Path $root 'EDF6.exe') -PathType Leaf)) {
        throw 'EDF6.exe is missing. Extract the package into the Steam game folder.'
    }
    if (Get-Process -Name EDF6,LaunchGame -ErrorAction SilentlyContinue) {
        throw 'Close EDF6 and its launcher before switching VR mode. No files were changed.'
    }
    $enabled = Join-Path $root 'Mods\Plugins\EDF6VR.dll'
    $disabled = $enabled + '.disabled'
    if (!(Test-Path -LiteralPath $enabled -PathType Leaf) -and
        !(Test-Path -LiteralPath $disabled -PathType Leaf)) {
        throw 'EDF6VR.dll is missing. Extract the complete package again.'
    }
    # Never edit the INI or other plugins. Keep the latest enabled DLL when an
    # update was extracted over an older disabled copy. Archive the old copy.
    if ($SelectedMode -eq 'VR') {
        if (!(Test-Path -LiteralPath $enabled -PathType Leaf)) {
            Move-Item -LiteralPath $disabled -Destination $enabled -ErrorAction Stop
        }
    } elseif (Test-Path -LiteralPath $enabled -PathType Leaf) {
        if (Test-Path -LiteralPath $disabled -PathType Leaf) {
            $backup = $disabled + '.previous-' + [Guid]::NewGuid().ToString('N')
            Move-Item -LiteralPath $disabled -Destination $backup -ErrorAction Stop
        }
        Move-Item -LiteralPath $enabled -Destination $disabled -ErrorAction Stop
    }
    Write-Host ("Mode: {0}. Your INI settings have been preserved." -f $SelectedMode)
}

# Dot-sourcing exposes only the switch function to the offline regression test.
if ($MyInvocation.InvocationName -ne '.') {
    try {
        if (!$Mode) { throw 'Start VR_Play.bat to choose Y or N.' }
        $gameRoot = Split-Path -Parent $PSScriptRoot
        Set-EDF6VRMode -GameDirectory $gameRoot -SelectedMode $Mode
        Write-Host 'Mode saved. Launch the game from Steam whenever you are ready.'
    } catch {
        Write-Host ("ERROR: {0}" -f $_.Exception.Message) -ForegroundColor Red
        exit 1
    }
}
