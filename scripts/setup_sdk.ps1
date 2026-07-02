$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$sdkDir = Join-Path $root "aviutl2_sdk"
$tempZip = Join-Path $env:TEMP "aviutl2_sdk.zip"
$sdkUrl = "https://spring-fragrance.mints.ne.jp/aviutl/aviutl2_sdk.zip"

$cp932 = [System.Text.Encoding]::GetEncoding(932)
$utf8Bom = [System.Text.UTF8Encoding]::new($true)

Write-Host "Downloading AviUtl2 SDK..."
Invoke-WebRequest -Uri $sdkUrl -OutFile $tempZip

if (Test-Path $sdkDir) {
    Remove-Item $sdkDir -Recurse -Force
}

Write-Host "Extracting SDK..."
Expand-Archive -Path $tempZip -DestinationPath $sdkDir -Force

Remove-Item $tempZip -Force

Write-Host "Converting encoding..."

Get-ChildItem $sdkDir -Recurse -File -Include *.h |
ForEach-Object {
    Write-Host "  $($_.FullName)"
    $text = [System.IO.File]::ReadAllText($_.FullName, $cp932)
    [System.IO.File]::WriteAllText($_.FullName, $text, $utf8Bom)
}

Write-Host "SDK setup completed."