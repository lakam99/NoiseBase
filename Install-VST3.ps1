param([string]$Vst3Folder = (Join-Path $env:LOCALAPPDATA 'Programs/Common/VST3'))
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'VST3/NoiseBase.vst3'
$base = [System.IO.Path]::GetFullPath($Vst3Folder)
$target = [System.IO.Path]::GetFullPath((Join-Path $base 'NoiseBase.vst3'))
if (-not $target.StartsWith($base.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Invalid destination.'
}
if (-not (Test-Path -LiteralPath $source -PathType Container)) {throw 'Keep the complete VST3 folder with this installer.'}
if (Test-Path -LiteralPath $target) {throw ('An installation already exists at ' + $target + '. It was not changed. Choose another -Vst3Folder or replace the old install manually with the DAW closed.')}
New-Item -ItemType Directory -Path $base -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $base -Recurse
Write-Host ('Installed: ' + $target)
Write-Host 'Restart/rescan plug-ins in your DAW. Keep the entire .vst3 directory intact.'
