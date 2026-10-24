# 下载词库原始文件（雾凇拼音、Rime 五笔86、生僻字的拆字/笔画/读音）并编译成 data\ 下的词库。
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch_data.ps1 [-Proxy http://127.0.0.1:7890] [-SkipExisting]
# -SkipExisting：data_src 里已经下好的文件不重下（setup.bat 断了重跑时用）
param([string]$Proxy = "http://127.0.0.1:7890", [string]$Python = "python", [switch]$SkipExisting)
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
    # 生僻字：拆字（GPL-3.0）、笔画（LGPL-3.0）、带声调的读音（MIT）
    "rime-ice\radical_pinyin.dict.yaml"  = "https://raw.githubusercontent.com/iDvel/rime-ice/main/radical_pinyin.dict.yaml"
    "rime-radical-pinyin\LICENSE"        = "https://raw.githubusercontent.com/mirtlecn/rime-radical-pinyin/master/LICENSE"
    "rime-stroke\stroke.dict.yaml"       = "https://raw.githubusercontent.com/rime/rime-stroke/master/stroke.dict.yaml"
    "rime-stroke\LICENSE"                = "https://raw.githubusercontent.com/rime/rime-stroke/master/LICENSE"
    "pinyin-data\pinyin.txt"             = "https://raw.githubusercontent.com/mozillazg/pinyin-data/master/pinyin.txt"
    "pinyin-data\LICENSE"                = "https://raw.githubusercontent.com/mozillazg/pinyin-data/master/LICENSE"
}
# 断线会重试、接着下；先下到 .part，下完才改名，半截文件不会被当成好的
function Get-File($url, $out) {
    $part = "$out.part"
    if (Test-Path $part) { Remove-Item -Force $part }
    $proxyArgs = if ($Proxy) { @("-x", $Proxy) } else { @() }
    & curl.exe -sSL --fail --retry 4 --retry-all-errors --retry-delay 2 -C - --connect-timeout 20 `
        --speed-limit 512 --speed-time 60 @proxyArgs -o $part $url
    if ($LASTEXITCODE -ne 0) { return $false }
    Move-Item -Force $part $out
    return $true
}
foreach ($rel in $files.Keys) {
    $out = Join-Path $src $rel
    if ($SkipExisting -and (Test-Path $out)) { continue }
    New-Item -ItemType Directory -Force (Split-Path $out -Parent) | Out-Null
    Write-Host "  下载 $rel …"
    if (-not (Get-File $files[$rel] $out)) { throw "下载失败：$($files[$rel])" }
    Write-Output ("下载好了 {0}  {1:N0} 字节" -f $rel, (Get-Item $out).Length)
}
$report = Join-Path $root "data\build_report.txt"
New-Item -ItemType Directory -Force (Join-Path $root "data") | Out-Null
# 整句用的语言模型（rime-octagram-data 的 hans 分支，LGPL-3.0），直接放进 data\
$gram = Join-Path $root "data\zh-hans-t-essay-bgw.gram"
if (-not ($SkipExisting -and (Test-Path $gram))) {
    Write-Host "  下载 整句模型（41 MB）…"
    if (-not (Get-File "https://raw.githubusercontent.com/lotem/rime-octagram-data/hans/zh-hans-t-essay-bgw.gram" $gram)) {
        throw "下载语言模型失败"
    }
    Write-Output ("下载好了 语言模型  {0:N0} 字节" -f (Get-Item $gram).Length)
}
& $Python -I (Join-Path $PSScriptRoot "build_dict.py") $src (Join-Path $root "data") $report
if ($LASTEXITCODE -ne 0) { throw "编译词库失败" }
Get-Content $report -Encoding UTF8 | Select-Object -Last 4
