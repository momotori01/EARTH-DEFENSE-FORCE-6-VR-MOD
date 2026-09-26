$ErrorActionPreference='Stop'
. "$PSScriptRoot\..\packaging\Switch-VR.ps1"
function Check($value,$message) {if (!$value) {throw "FAIL: $message"}}
# Use a private fixture, never the installed game, and never launch Steam.
$root=Join-Path $PSScriptRoot ('.package-test-' + [Guid]::NewGuid().ToString('N') + ' space & (test) ! quote'' ' + [char]0x65E5 + [char]0x672C)
New-Item -ItemType Directory -Path (Join-Path $root 'Mods\Plugins') -Force | Out-Null
$paths=@('EDF6.exe','Mods\Plugins\EDF6VR.dll','Mods\Plugins\EDF6VR.ini','Mods\Plugins\Other.dll')
foreach($p in $paths) {[IO.File]::WriteAllText((Join-Path $root $p),$p)}
$dll=Join-Path $root 'Mods\Plugins\EDF6VR.dll'
$ini=Join-Path $root 'Mods\Plugins\EDF6VR.ini'
$other=Join-Path $root 'Mods\Plugins\Other.dll'
$iniHash=(Get-FileHash -LiteralPath $ini).Hash
$otherHash=(Get-FileHash -LiteralPath $other).Hash
function Get-Process {param($Name,$ErrorAction) if($script:pretendRunning){return @{Id=1234}}}
$script:pretendRunning=$false
Set-EDF6VRMode $root Flat
Check (!(Test-Path -LiteralPath $dll)) 'Flat leaves no loadable VR DLL'
Check (Test-Path -LiteralPath "$dll.disabled") 'Flat preserves plugin'
Set-EDF6VRMode $root Flat
Set-EDF6VRMode $root VR
Set-EDF6VRMode $root VR
Check ((Get-Content -LiteralPath $dll) -eq 'Mods\Plugins\EDF6VR.dll') 'round trip bytes'
$script:pretendRunning=$true
$blocked=$false
try {Set-EDF6VRMode $root Flat} catch {$blocked=$true}
Check $blocked 'running game refused'
Check (Test-Path -LiteralPath $dll) 'refusal did not rename'
$script:pretendRunning=$false
[IO.File]::WriteAllText("$dll.disabled",'old version')
Set-EDF6VRMode $root VR
Set-EDF6VRMode $root Flat
$old=@(Get-ChildItem -LiteralPath (Split-Path -Parent $dll) -Filter '*.previous-*')
Check ($old.Count -eq 1) 'update collision archives old disabled DLL'
Check ((Get-Content -LiteralPath $old[0].FullName) -eq 'old version') 'backup bytes'
Set-EDF6VRMode $root VR
Check ((Get-FileHash -LiteralPath $ini).Hash -eq $iniHash) 'INI unchanged'
Check ((Get-FileHash -LiteralPath $other).Hash -eq $otherHash) 'other plugin unchanged'
# Exercise the shipped entry point without NoLaunch. Any launch is a failure.
function Start-Process {throw 'Switch-only helper must never start a process'}
$helperDir=Join-Path $root 'EDF6VR'
New-Item -ItemType Directory -Path $helperDir | Out-Null
$helper=Join-Path $helperDir 'Switch-VR.ps1'
Copy-Item -LiteralPath "$PSScriptRoot\..\packaging\Switch-VR.ps1" -Destination $helper
& $helper -Mode Flat
Check (Test-Path -LiteralPath "$dll.disabled") 'entry point Flat without launch'
& $helper -Mode VR
Check (Test-Path -LiteralPath $dll) 'entry point VR without launch'
$missing=Join-Path $root 'empty'
New-Item -ItemType Directory -Path $missing | Out-Null
$blocked=$false
try {Set-EDF6VRMode $missing VR} catch {$blocked=$true}
Check $blocked 'wrong directory refused'
Write-Host 'Package switch tests PASS: Y/N, repeat, running guard, paths, collision, INI/other plugin preservation. No game launched.'
# No recursive deletion or computed path outside the private fixture.
foreach($item in (Get-ChildItem -LiteralPath $root -Recurse -File)) {Remove-Item -LiteralPath $item.FullName}
Remove-Item -LiteralPath $helperDir,$missing,(Join-Path $root 'Mods\Plugins'),(Join-Path $root 'Mods'),$root
