<#
  GAG Modern - installer
  Creates GagModern.exe next to the game, plus gagfx.dll and gagfix.ini.

  Usage (from this folder):
      powershell -ExecutionPolicy Bypass -File install.ps1 [-GameDir <path>] [-Shortcut]

  The game folder is the one containing Gag01.cdf. By default it is looked for in this
  script's parent folder and in this folder.
  Nothing in the original game files is modified.
#>
param(
    [string]$GameDir,
    [switch]$Shortcut,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Find-GameDir {
    foreach ($d in @((Split-Path -Parent $here), $here)) {
        if ($d -and (Test-Path -LiteralPath (Join-Path $d 'Gag01.cdf'))) { return $d }
    }
    return $null
}

if (-not $GameDir) { $GameDir = Find-GameDir }
if (-not $GameDir -or -not (Test-Path -LiteralPath (Join-Path $GameDir 'Gag01.cdf'))) {
    throw "Game folder not found (no Gag01.cdf). Put GAG_Modern inside the game folder or pass -GameDir <path>."
}
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
Write-Host "Game folder: $GameDir"

# Known executables (SHA-256)
$known = @{
    'CC069AEE614E7E318F873B1C14E7C6FB98EF5FA9B0E011E7140B755247A7786D' = 'Gag_patched.exe (community no-CD patch, v2.51 RU)'
    '5E74C0A4E95E9D706F827A9ABABD6162D765FEDDF3704CD56754FC2B6C6A4FC8' = 'Gag.exe (original v2.51 RU, needs the CD)'
}

$source = $null
foreach ($name in 'Gag_patched.exe', 'Gag.exe', 'GAG.EXE') {
    $p = Join-Path $GameDir $name
    if (Test-Path -LiteralPath $p) {
        $h = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
        if ($known.ContainsKey($h)) { $source = $p; Write-Host "Using $($known[$h])"; break }
        if ($Force) { $source = $p; Write-Warning "$name is an unknown version; patching anyway (-Force)"; break }
        Write-Warning "$name is an unknown version (SHA-256 $h), skipping. Use -Force to try anyway."
    }
}
if (-not $source) { throw "No supported game executable (Gag_patched.exe or Gag.exe) found in $GameDir." }

# Patch: the import "WINMM.dll" becomes "GAGFX.dll" (same length). gagfx.dll forwards
# the sound functions to the real winmm.dll and fixes graphics/display handling.
[byte[]]$bytes = [System.IO.File]::ReadAllBytes($source)
$latin1 = [System.Text.Encoding]::GetEncoding(28591)   # maps every byte 1:1 to a char
$text   = $latin1.GetString($bytes)
$repl   = [System.Text.Encoding]::ASCII.GetBytes("GAGFX.dll")
$hits = @()
$i = $text.IndexOf("WINMM.dll`0", [StringComparison]::Ordinal)
while ($i -ge 0) { $hits += $i; $i = $text.IndexOf("WINMM.dll`0", $i + 1, [StringComparison]::Ordinal) }
if ($hits.Count -ne 1) { throw "Unexpected executable layout (found $($hits.Count) WINMM.dll imports)." }
[Array]::Copy($repl, 0, $bytes, $hits[0], $repl.Length)

$target = Join-Path $GameDir 'GagModern.exe'
[System.IO.File]::WriteAllBytes($target, $bytes)
Write-Host "Created  $target"

Copy-Item -LiteralPath (Join-Path $here 'bin\gagfx.dll') -Destination (Join-Path $GameDir 'gagfx.dll') -Force
Write-Host "Copied   gagfx.dll"

$ini = Join-Path $GameDir 'gagfix.ini'
if (-not (Test-Path -LiteralPath $ini)) {
    Copy-Item -LiteralPath (Join-Path $here 'gagfix.ini') -Destination $ini
    Write-Host "Copied   gagfix.ini"
} else {
    Write-Host "Kept     existing gagfix.ini"
}

# Old compatibility settings on GagModern.exe would fight the fix: make sure there are none.
$layers = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers'
if ((Test-Path $layers) -and (Get-ItemProperty -Path $layers -Name $target -ErrorAction SilentlyContinue)) {
    Remove-ItemProperty -Path $layers -Name $target
    Write-Host "Removed  compatibility-mode settings from GagModern.exe"
}

if ($Shortcut) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $ws = New-Object -ComObject WScript.Shell
    $lnk = $ws.CreateShortcut((Join-Path $desktop 'GAG.lnk'))
    $lnk.TargetPath = $target
    $lnk.WorkingDirectory = $GameDir
    $lnk.IconLocation = "$target,0"
    $lnk.Save()
    Write-Host "Created  desktop shortcut GAG.lnk"
}

Write-Host ""
Write-Host "Done. Start the game with GagModern.exe (no compatibility mode needed)."
Write-Host "Alt+Enter switches between fullscreen and window. Settings: gagfix.ini"
