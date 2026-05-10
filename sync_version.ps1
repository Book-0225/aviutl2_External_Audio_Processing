$tomlFile   = "aviutl2.toml"
$headerFile = "Eap2Version.h"
$textFile   = "package.txt"
$objFile    = "@EAP2.obj2"
$utf8Bom   = New-Object System.Text.UTF8Encoding($True)
$utf8NoBom = New-Object System.Text.UTF8Encoding($False)
$sjis      = [System.Text.Encoding]::GetEncoding("shift_jis")
$tomlText = [System.IO.File]::ReadAllText($tomlFile, $utf8NoBom)

if ($tomlText -match '(?m)^version\s*=\s*"([^"]+)"') {
    $ver = $Matches[1]
} else {
    Write-Error "TOMLファイル内に version = `"xxx`" が見つかりませんでした。"
    exit 1
}

if (Test-Path $headerFile) {
    $headerText = [System.IO.File]::ReadAllText($headerFile, $utf8Bom)
    $headerPattern = '(#define\s+PLUGIN_VERSION\s+L"v)[0-9a-zA-Z\.\-]+'
    $headerText = $headerText -replace $headerPattern, ("`${1}" + $ver)
    [System.IO.File]::WriteAllText($headerFile, $headerText, $utf8Bom)
    Write-Host "ヘッダーファイル(${headerFile})を v${ver} に同期しました。"
} else {
    Write-Host "${headerFile} が見つかりませんでした。"
}

if (Test-Path $textFile) {
    $textData = [System.IO.File]::ReadAllText($textFile, $utf8NoBom)
    $textPattern = '(\[External Audio Processing 2 v)[0-9a-zA-Z\.\-]+([^\]]*\])'
    $textData = $textData -replace $textPattern, ("`${1}" + $ver + "`${2}")
    [System.IO.File]::WriteAllText($textFile, $textData, $utf8NoBom)
    Write-Host "テキストファイル(${textFile})を v${ver} に同期しました。"
} else {
    Write-Host "${textFile} が見つかりませんでした。スキップします。"
}

if (Test-Path $objFile) {
    $objData = [System.IO.File]::ReadAllText($objFile, $utf8NoBom)
    $objPattern = '(--information:External Audio Processing 2 obj2 v)[0-9a-zA-Z\.\-]+(.*)'
    $objData = $objData -replace $objPattern, ("`${1}" + $ver + "`${2}")
    [System.IO.File]::WriteAllText($objFile, $objData, $utf8NoBom)
    Write-Host "スクリプト(${objFile})を v${ver} に同期しました。"
} else {
    Write-Host "${objFile} が見つかりませんでした。スキップします。"
}