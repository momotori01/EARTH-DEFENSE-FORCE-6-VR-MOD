# Builds the drop-in package: release\EDF6MultiSlot-<version>\ and a .zip of its contents (+ .sha256).
# The layout mirrors the game folder, so dropping the contents next to EDF6.exe is the whole install, and
# the EDF6VR package bundles the same files as they are.
#
# No INI files are shipped, so dropping an update never overwrites anyone's settings:
# - ModLoader.ini: EDFModLoader turns every option on when it is missing, and a player who already
#   has one keeps their own (the same policy as the EDF6VR package).
# - EDF6MultiSlot.ini: the plugin writes its documented defaults on first run when it is missing.
# winmm.dll is EDFModLoader with the shared-dispatch race fixed. VR bundling must
# use this file too; copying only the plugin DLL leaves the loader defect active.
# Reads only; writes nothing outside _MultislotDEV\release.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$game = Split-Path -Parent $root

$cmake = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(EDF6MultiSlot VERSION (\d+\.\d+\.\d+)') { throw 'Version not found in CMakeLists.txt' }
$baseVersion = $Matches[1]
$plugin = Get-Content -LiteralPath (Join-Path $root 'src\plugin.cpp') -Raw
if ($plugin -notmatch 'kVersion = "(\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?)"') { throw 'Plugin version not found' }
$version = $Matches[1]
if (($version -split '-')[0] -ne $baseVersion) { throw 'CMake and plugin versions differ' }
$loaderSource = Join-Path $root 'dist\winmm.dll'
$loaderHash = (Get-FileHash -LiteralPath $loaderSource -Algorithm SHA256).Hash
if ($loaderHash -ne 'BE94E1FAC0CA12C41B6924E2EB168851641C999CE951D2A5A9FAEA5161B0F9A3') {
    throw "winmm.dll is not the verified race-fixed EDFModLoader ($loaderHash); run tools/fix_winmm_proxy.py"
}
$name = "EDF6MultiSlot-$version"
$release = Join-Path $root 'release'
$out = Join-Path $release $name
$zip = Join-Path $release "$name.zip"

$sources = @{
    'winmm.dll'                      = $loaderSource
    'EDFModLoader_LICENSE.txt'       = Join-Path $root 'References\EDFModLoader-master\LICENSE'
    'Mods\Plugins\EDF6MultiSlot.dll' = Join-Path $root 'dist\EDF6MultiSlot.dll'
    'HANDSHAKE_RECOVERY_JA.md'       = Join-Path $root 'packaging\HANDSHAKE_RECOVERY_JA.md'
    'LOADER_FIX_JA.md'               = Join-Path $root 'packaging\LOADER_FIX_JA.md'
    'loader-fix.json'               = Join-Path $root 'research\crashes-20260922\loader-fix.json'
}
foreach ($source in $sources.Values) {
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing $source (run build.cmd first?)" }
}

# Verify every deletion target stays below this release directory; use native PowerShell only.
$releaseFull = [IO.Path]::GetFullPath($release).TrimEnd('\') + '\'
foreach ($target in @($out, $zip)) {
    if (-not [IO.Path]::GetFullPath($target).StartsWith($releaseFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside release: $target"
    }
}
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
foreach ($entry in $sources.GetEnumerator()) {
    $target = Join-Path $out $entry.Key
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $entry.Value -Destination $target
}
# Notepad-friendly: UTF-8 with BOM and CRLF. Keep this script ASCII: PowerShell 5.1 reads it as ANSI.
$readme = [IO.File]::ReadAllText((Join-Path $root 'packaging\README_EDF6MultiSlot.txt'), [Text.Encoding]::UTF8) -replace "`r?`n", "`r`n"
$readme = $readme.Replace('{VERSION}', $version)
[IO.File]::WriteAllText((Join-Path $out 'README_EDF6MultiSlot.txt'), $readme, (New-Object Text.UTF8Encoding $true))

# Not Compress-Archive: Windows PowerShell 5.1 stores '\' in entry names, which some unzip tools
# extract as flat file names instead of folders. The ZIP spec uses '/'.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $out -Recurse -File) {
        $entry = $file.FullName.Substring($out.Length + 1).Replace('\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal)
    }
} finally {
    $archive.Dispose()
}
$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
[IO.File]::WriteAllText("$zip.sha256", "$zipHash  $name.zip`n", (New-Object Text.ASCIIEncoding))
Get-ChildItem -LiteralPath $out -Recurse -File | ForEach-Object {
    $_.FullName.Substring($out.Length + 1) + "  " + $_.Length + "  " + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
}
Get-Item -LiteralPath $zip | Select-Object FullName, Length
"SHA256 $zipHash"
