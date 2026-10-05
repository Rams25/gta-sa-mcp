# Copies the plugin into a game folder.
#   .\scripts\install.ps1 -GameDir "C:\Games\GTA San Andreas" -From .\gta-sa-mcp-asi
# -From: the folder holding gta-sa-mcp.asi (the unzipped CI artifact).
param(
	[Parameter(Mandatory = $true)][string]$GameDir,
	[Parameter(Mandatory = $true)][string]$From
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'gta_sa.exe'))) {
	throw "gta_sa.exe not found in $GameDir"
}
$asi = Join-Path $From 'gta-sa-mcp.asi'
if (-not (Test-Path -LiteralPath $asi)) {
	throw "gta-sa-mcp.asi not found in $From"
}

# 1.0 US, the only executable the plugin knows.
$size = (Get-Item -LiteralPath (Join-Path $GameDir 'gta_sa.exe')).Length
if ($size -ne 14383616) {
	Write-Warning "gta_sa.exe is $size bytes; the plugin expects the 1.0 US executable (14383616 bytes)."
}

Copy-Item -LiteralPath $asi -Destination $GameDir -Force

# Keep an existing ini: it holds the user's settings.
$ini = Join-Path $GameDir 'gta-sa-mcp.ini'
if (Test-Path -LiteralPath $ini) {
	Write-Host "Kept existing $ini"
} else {
	$source = Join-Path $From 'gta-sa-mcp.ini'
	if (-not (Test-Path -LiteralPath $source)) { $source = Join-Path $PSScriptRoot '..\asi\gta-sa-mcp.ini' }
	Copy-Item -LiteralPath $source -Destination $ini
}

# An .asi is only loaded if the game has an ASI loader.
$loaders = 'vorbisHooked.dll', 'dinput8.dll', 'cleo.asi', 'CLEO.asi', 'd3d9.dll'
if (-not ($loaders | Where-Object { Test-Path -LiteralPath (Join-Path $GameDir $_) })) {
	Write-Warning 'No ASI loader found in the game folder (Silent''s ASI Loader, CLEO...): the plugin will not load.'
}

Write-Host "Installed gta-sa-mcp.asi in $GameDir"
