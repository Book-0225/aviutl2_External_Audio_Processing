param(
    [Parameter(Mandatory)]
    [string]$Version
)

$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$readme = Join-Path $root "README.md"
$lines = Get-Content $readme
$startPattern = "- **v$Version**"
$start = -1

for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i].Trim() -eq $startPattern) {
        $start = $i
        break
    }
}

if ($start -lt 0) {
    throw "Release notes for v$Version were not found."
}

$result = New-Object System.Collections.Generic.List[string]

for ($i = $start + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^- \*\*v(.+?)\*\*$') {
        break
    }
    $line = $lines[$i]
    $line = $line -replace '^  ', ''
    $result.Add($line)
}

$result -join "`n"