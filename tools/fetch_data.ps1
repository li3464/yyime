# 下载词库原始文件（雾凇拼音、Rime 五笔86）并编译成 data\ 下的二进制词库。
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch_data.ps1 [-Proxy http://127.0.0.1:7890]
param([string]$Proxy = "http://127.0.0.1:7890", [string]$Python = "python")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $root "data_src"
$files = @{
    "rime-ice\cn_dicts_8105.dict.yaml"   = "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/8105.dict.yaml"
    "rime-ice\cn_dicts_41448.dict.yaml"  = "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/41448.dict.yaml"
    "rime-ice\cn_dicts_base.dict.yaml"   = "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/base.dict.yaml"
    "rime-ice\cn_dicts_ext.dict.yaml"    = "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/ext.dict.yaml"
    "rime-ice\cn_dicts_others.dict.yaml" = "https://raw.githubusercontent.com/iDvel/rime-ice/main/cn_dicts/others.dict.yaml"
    "rime-ice\en_dicts_en.dict.yaml"     = "https://raw.githubusercontent.com/iDvel/rime-ice/main/en_dicts/en.dict.yaml"
    "rime-ice\en_dicts_en_ext.dict.yaml" = "https://raw.githubusercontent.com/iDvel/rime-ice/main/en_dicts/en_ext.dict.yaml"
    "rime-ice\LICENSE"                   = "https://raw.githubusercontent.com/iDvel/rime-ice/main/LICENSE"
    "rime-ice\opencc_emoji.txt"          = "https://raw.githubusercontent.com/iDvel/rime-ice/main/opencc/emoji.txt"
    "rime-wubi\wubi86.dict.yaml"         = "https://raw.githubusercontent.com/rime/rime-wubi/master/wubi86.dict.yaml"
    "rime-wubi\LICENSE"                  = "https://raw.githubusercontent.com/rime/rime-wubi/master/LICENSE"
}
foreach ($rel in $files.Keys) {
    $out = Join-Path $src $rel
    New-Item -ItemType Directory -Force (Split-Path $out -Parent) | Out-Null
    $curlArgs = @("-sSL", "--fail", "--max-time", "300", "-o", $out, $files[$rel])
    if ($Proxy) { $curlArgs = @("-x", $Proxy) + $curlArgs }
    & curl.exe @curlArgs
    if ($LASTEXITCODE -ne 0) { throw "下载失败：$($files[$rel])" }
    Write-Output ("下载好了 {0}  {1:N0} 字节" -f $rel, (Get-Item $out).Length)
}
$report = Join-Path $root "data\build_report.txt"
New-Item -ItemType Directory -Force (Join-Path $root "data") | Out-Null
# 整句用的语言模型（rime-octagram-data 的 hans 分支，LGPL-3.0），直接放进 data\
$gram = Join-Path $root "data\zh-hans-t-essay-bgw.gram"
$gramArgs = @("-sSL", "--fail", "--max-time", "900", "-o", $gram,
              "https://raw.githubusercontent.com/lotem/rime-octagram-data/hans/zh-hans-t-essay-bgw.gram")
if ($Proxy) { $gramArgs = @("-x", $Proxy) + $gramArgs }
& curl.exe @gramArgs
if ($LASTEXITCODE -ne 0) { throw "下载语言模型失败" }
Write-Output ("下载好了 语言模型  {0:N0} 字节" -f (Get-Item $gram).Length)
& $Python -I (Join-Path $PSScriptRoot "build_dict.py") $src (Join-Path $root "data") $report
if ($LASTEXITCODE -ne 0) { throw "编译词库失败" }
Get-Content $report -Encoding UTF8 | Select-Object -Last 4
