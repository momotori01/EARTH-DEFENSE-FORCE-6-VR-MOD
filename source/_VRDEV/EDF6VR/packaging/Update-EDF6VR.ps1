[CmdletBinding()]
# Brings an installed EDF6VR package (with the EDF6MultiSlot it bundles) up to
# the latest GitHub release, so everyone in a room runs the same versions.
# Started by Update_EDF6VR.bat. -CheckOnly only reports; -Yes skips the question.
param([switch]$CheckOnly, [switch]$Yes)
$ErrorActionPreference = 'Stop'

$Repository = 'momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD'
# Kept when already present: the player's ClearLoot choice is saved in its INI.
# EDF6VR.ini and EDF6MultiSlot.ini are not in the package at all.
$KeepExisting = @('Mods/Plugins/EDF6ClearLoot.ini')
$BackupsKept = 3

# The package's own record (EDF6VR\PACKAGE_MANIFEST.json), or for a folder
# without one the version in the DLL's loading line. $null when neither.
function Get-InstalledEDF6VRVersion {
    param([Parameter(Mandatory)][string]$GameDirectory)
    $manifest = Join-Path $GameDirectory 'EDF6VR\PACKAGE_MANIFEST.json'
    if (Test-Path -LiteralPath $manifest -PathType Leaf) {
        try {
            $version = [string](Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).version
            if ($version -match '^\d+\.\d+\.\d+$') { return $version }
        } catch {}
    }
    foreach ($name in 'EDF6VR.dll', 'EDF6VR.dll.disabled') {
        $dll = Join-Path $GameDirectory ('Mods\Plugins\' + $name)
        if (Test-Path -LiteralPath $dll -PathType Leaf) {
            $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dll))
            $found = [regex]::Match($text, 'EDF6VR (\d+\.\d+\.\d+) cockpit loading')
            if ($found.Success) { return $found.Groups[1].Value }
        }
    }
    return $null
}

# The latest release and its ZIP, with the SHA-256 GitHub records for it.
function Get-LatestEDF6VRRelease {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repository/releases/latest" `
        -Headers @{ 'User-Agent' = 'EDF6VR-Updater'; 'Accept' = 'application/vnd.github+json' }
    $tag = [string]$release.tag_name
    if ($tag -notmatch '^EDF6VR-(\d+\.\d+\.\d+)$') { throw "Unexpected release name '$tag'." }
    $version = $Matches[1]
    $asset = @($release.assets | Where-Object { $_.name -eq "EDF6VR-$version.zip" })[0]
    if (!$asset) { throw "The release $tag has no EDF6VR-$version.zip." }
    if ([string]$asset.digest -notmatch '^sha256:([0-9a-fA-F]{64})$') {
        throw 'GitHub gives no SHA-256 for this download, so it cannot be checked. Nothing was installed.'
    }
    [pscustomobject]@{
        Version = $version; Url = [string]$asset.browser_download_url; Size = [long]$asset.size
        Sha256 = $Matches[1].ToUpperInvariant(); Notes = [string]$release.body
    }
}

# Checks the ZIP and every file in it before anything is copied, saves each
# file it is about to replace, then copies the package over the game folder.
# Returns the number of files changed and where the old ones were saved.
function Install-EDF6VRPackage {
    param([Parameter(Mandatory)][string]$GameDirectory, [Parameter(Mandatory)][string]$ZipPath,
          [Parameter(Mandatory)][string]$Sha256, [string]$FromVersion, [switch]$AllowDevelopmentFolder)
    $root = (Resolve-Path -LiteralPath $GameDirectory).Path
    if (!(Test-Path -LiteralPath (Join-Path $root 'EDF6.exe') -PathType Leaf)) {
        throw 'EDF6.exe is missing. Put Update_EDF6VR.bat in the Steam game folder.'
    }
    if (!$AllowDevelopmentFolder -and (Test-Path -LiteralPath (Join-Path $root '_VRDEV'))) {
        throw 'This is the development folder (_VRDEV). Not updating it from a release.'
    }
    if (Get-Process -Name EDF6, LaunchGame -ErrorAction SilentlyContinue) {
        throw 'Close EDF6 and its launcher before updating. No files were changed.'
    }
    $actual = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash
    if ($actual -ne $Sha256.ToUpperInvariant()) {
        throw "The download is damaged (SHA-256 $actual, expected $Sha256). No files were changed."
    }
    $stage = Join-Path ([IO.Path]::GetTempPath()) ('EDF6VR-update-' + [Guid]::NewGuid().ToString('N'))
    $changed = 0
    $backup = $null
    try {
        Expand-Archive -LiteralPath $ZipPath -DestinationPath $stage
        $manifestPath = Join-Path $stage 'EDF6VR\PACKAGE_MANIFEST.json'
        if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'The download has no package manifest. No files were changed.' }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $listed = @($manifest.files.PSObject.Properties)
        if (!$listed.Count) { throw 'The package manifest lists no files. No files were changed.' }
        foreach ($entry in $listed) {
            if ($entry.Name -match '(^|/)\.\.(/|$)' -or $entry.Name -match '^[/\\]' -or $entry.Name -match ':') {
                throw "The package lists an unsafe path ($($entry.Name)). No files were changed."
            }
            $file = Join-Path $stage ($entry.Name -replace '/', '\')
            if (!(Test-Path -LiteralPath $file -PathType Leaf) -or
                (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne ([string]$entry.Value).ToUpperInvariant()) {
                throw "$($entry.Name) in the download does not match its manifest. No files were changed."
            }
        }
        # A Flat-mode install keeps its DLL disabled (Switch-VR.ps1).
        $plugins = Join-Path $root 'Mods\Plugins'
        $flat = (Test-Path -LiteralPath (Join-Path $plugins 'EDF6VR.dll.disabled') -PathType Leaf) -and
                !(Test-Path -LiteralPath (Join-Path $plugins 'EDF6VR.dll') -PathType Leaf)
        $label = if ($FromVersion) { $FromVersion } else { 'unknown' }
        $backup = Join-Path $root ('EDF6VR\backup\' + $label + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        $names = @($listed | ForEach-Object { $_.Name }) + @('EDF6VR/PACKAGE_MANIFEST.json')
        foreach ($name in $names) {
            $source = Join-Path $stage ($name -replace '/', '\')
            $targetName = if ($flat -and $name -eq 'Mods/Plugins/EDF6VR.dll') { 'Mods/Plugins/EDF6VR.dll.disabled' } else { $name }
            $target = Join-Path $root ($targetName -replace '/', '\')
            $exists = Test-Path -LiteralPath $target -PathType Leaf
            if ($exists -and $KeepExisting -contains $name) { continue }
            if ($exists) {
                if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) { continue }
                $saved = Join-Path $backup ($targetName -replace '/', '\')
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $saved) | Out-Null
                Copy-Item -LiteralPath $target -Destination $saved -Force
            }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            try { Copy-Item -LiteralPath $source -Destination $target -Force }
            catch { throw "Could not write $targetName ($($_.Exception.Message)). The files replaced so far are saved in $backup." }
            $changed++
        }
        # Only the latest few backups are kept.
        $backups = Join-Path $root 'EDF6VR\backup'
        if (Test-Path -LiteralPath $backups) {
            @(Get-ChildItem -LiteralPath $backups -Directory | Sort-Object LastWriteTime -Descending) |
                Select-Object -Skip $BackupsKept | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
        }
    } finally {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (!(Test-Path -LiteralPath $backup)) { $backup = $null }
    return [pscustomobject]@{ Changed = $changed; Backup = $backup }
}

# Dot-sourcing exposes only the functions to the offline regression test.
if ($MyInvocation.InvocationName -ne '.') {
    try {
        $gameRoot = Split-Path -Parent $PSScriptRoot
        $installed = Get-InstalledEDF6VRVersion -GameDirectory $gameRoot
        Write-Host ('Installed: EDF6VR {0}' -f $(if ($installed) { $installed } else { '(unknown)' }))
        $latest = Get-LatestEDF6VRRelease
        Write-Host ('Latest:    EDF6VR {0} ({1:N0} MB)' -f $latest.Version, ($latest.Size / 1MB))
        if ($installed -and [version]$installed -ge [version]$latest.Version) {
            Write-Host 'Already up to date. Nothing was changed.'
            exit 0
        }
        if ($CheckOnly) { exit 0 }
        if (!$Yes) {
            $answer = Read-Host 'Download and install it? Y/N'
            if ($answer -notmatch '^[Yy]') { Write-Host 'Nothing was changed.'; exit 0 }
        }
        $zip = Join-Path ([IO.Path]::GetTempPath()) ('EDF6VR-{0}-{1}.zip' -f $latest.Version, [Guid]::NewGuid().ToString('N'))
        try {
            Write-Host 'Downloading...'
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $latest.Url -OutFile $zip -UseBasicParsing
            $result = Install-EDF6VRPackage -GameDirectory $gameRoot -ZipPath $zip -Sha256 $latest.Sha256 -FromVersion $installed
        } finally {
            Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
        }
        Write-Host ('Updated to EDF6VR {0}: {1} file(s) replaced.' -f $latest.Version, $result.Changed) -ForegroundColor Green
        if ($result.Backup) { Write-Host ('The replaced files are saved in {0}' -f $result.Backup) }
        Write-Host 'Your settings (EDF6VR.ini, EDF6MultiSlot.ini, EDF6ClearLoot.ini) were kept.'
        if ($latest.Notes) { Write-Host ''; Write-Host $latest.Notes.Trim() }
    } catch {
        Write-Host ('ERROR: {0}' -f $_.Exception.Message) -ForegroundColor Red
        exit 1
    }
}
