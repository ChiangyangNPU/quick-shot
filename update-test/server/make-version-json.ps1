# make-version-json.ps1 - 根据 zip 包生成 version.json（sha256 + fileSize 自动计算）
# 用法: powershell -ExecutionPolicy Bypass -File make-version-json.ps1 [-Version 1.1.0] [-Port 8000]
param(
    [string]$Version = "1.1.0",
    [int]$Port = 8000
)

$ScriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
$ZipName = "QuickShot-Release-v$Version-Windows-x64.zip"
$ZipPath = Join-Path $ScriptDir $ZipName

if (-not (Test-Path $ZipPath)) {
    Write-Error "找不到 zip 包: $ZipPath"
    exit 1
}

$Hash = (Get-FileHash -Algorithm SHA256 -Path $ZipPath).Hash.ToLower()
$Size = (Get-Item $ZipPath).Length

$Info = [ordered]@{
    version      = $Version
    # releaseNotes 每行作为翻译 key；客户端解析时逐行 tm->get()，找不到则保留原文
    releaseNotes = "update.releaseNotes.testHeader`nupdate.releaseNotes.test1`nupdate.releaseNotes.test2`nupdate.releaseNotes.test3"
    downloadUrl  = "http://127.0.0.1:$Port/$ZipName"
    checksum     = "sha256:$Hash"
    fileSize     = $Size
}

$JsonPath = Join-Path $ScriptDir "version.json"
# 用无 BOM 的 UTF-8 写入，避免部分 JSON 解析器对 BOM 敏感
[System.IO.File]::WriteAllText($JsonPath, ($Info | ConvertTo-Json), (New-Object System.Text.UTF8Encoding $false))
Write-Host "已生成: $JsonPath"
Write-Host (Get-Content $JsonPath -Raw)
