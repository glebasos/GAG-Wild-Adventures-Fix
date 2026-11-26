<#
  GAG Modern - uninstaller. Removes GagModern.exe and gagfx.dll from the game folder.
  gagfix.ini (your settings and the game's options) is kept unless -All is given.
  Saved games are never touched.
#>
param([string]$GameDir, [switch]$All)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $GameDir) {
    foreach ($d in @((Split-Path -Parent $here), $here)) {
        if ($d -and (Test-Path -LiteralPath (Join-Path $d 'GagModern.exe'))) { $GameDir = $d; break }
    }
}
if (-not $GameDir) { throw "GagModern.exe not found. Pass -GameDir <path>." }

$files = @('GagModern.exe', 'gagfx.dll', 'gagfix.log')
if ($All) { $files += 'gagfix.ini' }
foreach ($f in $files) {
    $p = Join-Path $GameDir $f
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p; Write-Host "Removed $p" }
}
