$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\packaging\Update-EDF6VR.ps1"
function Check($value,$message) {if (!$value) {throw "FAIL: $message"}}
# Private fixtures only: never the installed game, never the network.
$base=Join-Path $PSScriptRoot ('.update-test-' + [Guid]::NewGuid().ToString('N') + ' space & (test) ! quote'' ' + [char]0x65E5 + [char]0x672C)
function Write-Text($root,$relative,$text) {
    $path=Join-Path $root ($relative -replace '/','\')
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    [IO.File]::WriteAllText($path,$text)
}
function Hash($root,$relative) {(Get-FileHash -LiteralPath (Join-Path $root ($relative -replace '/','\')) -Algorithm SHA256).Hash}
# A package of the given version, zipped with a manifest of its files' SHA-256.
function New-Package($version,[switch]$Tamper) {
    $stage=Join-Path $base ('stage-' + $version)
    $files=[ordered]@{
        'winmm.dll'="loader $version"
        'Mods/Plugins/EDF6VR.dll'="xx EDF6VR $version cockpit loading xx"
        'Mods/Plugins/EDF6MultiSlot.dll'="multislot for $version"
        'Mods/Plugins/EDF6ClearLoot.ini'="[ClearLoot]`r`nEnabled=1`r`n"
        'README_EDF6VR.txt'="readme $version"
        'Update_EDF6VR.bat'='@echo off'
        'EDF6VR/Update-EDF6VR.ps1'="# updater $version"
    }
    $hashes=[ordered]@{}
    foreach($k in $files.Keys) {Write-Text $stage $k $files[$k]; $hashes[$k]=Hash $stage $k}
    if($Tamper) {Write-Text $stage 'README_EDF6VR.txt' 'changed after the manifest'}
    $manifest=[ordered]@{version=$version; files=$hashes} | ConvertTo-Json -Depth 4
    Write-Text $stage 'EDF6VR/PACKAGE_MANIFEST.json' $manifest
    $zip=Join-Path $base ("EDF6VR-$version" + $(if($Tamper){'-tampered'}else{''}) + '.zip')
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
    [pscustomobject]@{Zip=$zip; Sha=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash}
}
function Get-Process {param($Name,$ErrorAction) if($script:pretendRunning){return @{Id=1234}}}
$script:pretendRunning=$false
try {
    $game=Join-Path $base 'game'
    Write-Text $game 'EDF6.exe' 'game'
    Write-Text $game 'Mods/Plugins/EDF6VR.dll' 'yy EDF6VR 2.0.3 cockpit loading yy'
    Write-Text $game 'Mods/Plugins/EDF6VR.ini' "[VR]`r`nMine=1`r`n"
    Write-Text $game 'Mods/Plugins/EDF6MultiSlot.ini' "[MultiSlot]`r`nEightPlayerRooms=1`r`n"
    Write-Text $game 'Mods/Plugins/EDF6ClearLoot.ini' "[ClearLoot]`r`nEnabled=0`r`n"
    Write-Text $game 'Mods/Plugins/EDF6MultiSlot.dll' 'multislot old'
    $vrIni=Hash $game 'Mods/Plugins/EDF6VR.ini'; $msIni=Hash $game 'Mods/Plugins/EDF6MultiSlot.ini'; $lootIni=Hash $game 'Mods/Plugins/EDF6ClearLoot.ini'

    # The installed version: from the DLL without a manifest, then from the manifest.
    Check ((Get-InstalledEDF6VRVersion $game) -eq '2.0.3') 'version from the DLL loading line'
    Write-Text $game 'EDF6VR/PACKAGE_MANIFEST.json' '{"version":"2.0.3","files":{}}'
    Check ((Get-InstalledEDF6VRVersion $game) -eq '2.0.3') 'version from the manifest'
    $oldDll=Hash $game 'Mods/Plugins/EDF6VR.dll'

    $good=New-Package '2.0.4'
    $bad=New-Package '2.0.4' -Tamper

    # Refusals change nothing.
    $refused=$false; try {Install-EDF6VRPackage $game $good.Zip ('0'*64) '2.0.3'} catch {$refused=$true}
    Check $refused 'a damaged download is refused'
    $refused=$false; try {Install-EDF6VRPackage $game $bad.Zip $bad.Sha '2.0.3'} catch {$refused=$true}
    Check $refused 'a file that does not match the manifest is refused'
    $script:pretendRunning=$true
    $refused=$false; try {Install-EDF6VRPackage $game $good.Zip $good.Sha '2.0.3'} catch {$refused=$true}
    Check $refused 'a running game is refused'
    $script:pretendRunning=$false
    New-Item -ItemType Directory -Path (Join-Path $game '_VRDEV') | Out-Null
    $refused=$false; try {Install-EDF6VRPackage $game $good.Zip $good.Sha '2.0.3'} catch {$refused=$true}
    Check $refused 'the development folder is refused'
    Remove-Item -LiteralPath (Join-Path $game '_VRDEV')
    Check ((Hash $game 'Mods/Plugins/EDF6VR.dll') -eq $oldDll) 'refusals left the DLL alone'
    Check (!(Test-Path -LiteralPath (Join-Path $game 'README_EDF6VR.txt'))) 'refusals copied nothing'

    # The update itself.
    $result=Install-EDF6VRPackage $game $good.Zip $good.Sha '2.0.3'
    Check ((Get-InstalledEDF6VRVersion $game) -eq '2.0.4') 'now 2.0.4'
    Check ((Get-Content -LiteralPath (Join-Path $game 'Mods\Plugins\EDF6MultiSlot.dll')) -eq 'multislot for 2.0.4') 'MultiSlot updated with it'
    Check ((Hash $game 'Mods/Plugins/EDF6VR.ini') -eq $vrIni) 'EDF6VR.ini kept'
    Check ((Hash $game 'Mods/Plugins/EDF6MultiSlot.ini') -eq $msIni) 'EDF6MultiSlot.ini kept'
    Check ((Hash $game 'Mods/Plugins/EDF6ClearLoot.ini') -eq $lootIni) 'the player''s ClearLoot INI kept'
    Check ($result.Changed -ge 6) 'files replaced'
    Check ($result.Backup -and (Test-Path -LiteralPath (Join-Path $result.Backup 'Mods\Plugins\EDF6VR.dll'))) 'old DLL saved'
    Check ((Hash $result.Backup 'Mods/Plugins/EDF6VR.dll') -eq $oldDll) 'saved bytes are the old DLL'
    $again=Install-EDF6VRPackage $game $good.Zip $good.Sha '2.0.4'
    Check ($again.Changed -eq 0) 'the same package twice changes nothing'

    # A Flat-mode install stays Flat.
    Move-Item -LiteralPath (Join-Path $game 'Mods\Plugins\EDF6VR.dll') -Destination (Join-Path $game 'Mods\Plugins\EDF6VR.dll.disabled')
    $flat=New-Package '2.0.5'
    Install-EDF6VRPackage $game $flat.Zip $flat.Sha '2.0.4' | Out-Null
    Check (!(Test-Path -LiteralPath (Join-Path $game 'Mods\Plugins\EDF6VR.dll'))) 'Flat mode: no loadable DLL appears'
    Check ((Get-Content -LiteralPath (Join-Path $game 'Mods\Plugins\EDF6VR.dll.disabled')) -match '2\.0\.5') 'Flat mode: the disabled DLL is the new one'

    # Only the latest backups are kept.
    foreach($v in '2.0.6','2.0.7','2.0.8') { Start-Sleep -Milliseconds 1100; $p=New-Package $v; Install-EDF6VRPackage $game $p.Zip $p.Sha 'x' | Out-Null }
    $kept=@(Get-ChildItem -LiteralPath (Join-Path $game 'EDF6VR\backup') -Directory)
    Check ($kept.Count -eq 3) "three backups kept (found $($kept.Count))"
    Write-Host 'update tests passed'
} finally {
    Remove-Item -LiteralPath $base -Recurse -Force -ErrorAction SilentlyContinue
}
