$ErrorActionPreference = "Stop"

$toml = Join-Path (Split-Path $PSScriptRoot -Parent) "aviutl2.toml"

$version = (
    Select-String -Path $toml -Pattern '^\s*version\s*=\s*"([^"]+)"\s*$'
).Matches[0].Groups[1].Value

Write-Output $version