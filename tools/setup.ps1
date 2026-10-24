# 小白一键安装：下载编译器和词库 → 编译 → 打包 → 安装。
# 用法：运行仓库根目录的 setup.bat。开着系统代理（比如 Clash 的「系统代理」）会自动用上；
#       也可以手动指定：setup.bat -Proxy http://127.0.0.1:7890
# 下载过的东西会留在 .tools\ 和 data\，再运行会跳过；git pull 以后再运行一次就是更新。
param([string]$Proxy = "", [switch]$NoInstall)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$LlvmVersion = "20260922"  # 和开发时用的 llvm-mingw 同一版（自带 Python，编译词库也用它）

function Say($m) { Write-Host $m -ForegroundColor Cyan }
function Fail($m) {
    Write-Host ""
    Write-Host $m -ForegroundColor Red
    exit 1
}

Say "== yy输入法 一键安装 =="

# curl 不认 Windows 的系统代理，自己读出来用
if (-not $Proxy) {
    $ie = Get-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings" -ErrorAction SilentlyContinue
    if ($ie -and $ie.ProxyEnable -eq 1 -and $ie.ProxyServer) {
        $server = $ie.ProxyServer
        if ($server -match "https=([^;]+)") { $server = $Matches[1] }
        elseif ($server -match "http=([^;]+)") { $server = $Matches[1] }
        $Proxy = if ($server -match "://") { $server } else { "http://$server" }
    }
}
if ($Proxy) { Say "下载走代理 $Proxy" }

# 1. 编译器
$llvm = if ($env:LLVM_MINGW) { $env:LLVM_MINGW } else { Join-Path $root ".tools\llvm-mingw" }
if (Test-Path "$llvm\bin\x86_64-w64-mingw32-clang++.exe") {
    Say "[1/4] 编译器已经有了，跳过"
} elseif ($env:LLVM_MINGW) {
    Fail "环境变量 LLVM_MINGW 指向的 $llvm 里没有编译器"
} else {
    $tools = Join-Path $root ".tools"
    $name = "llvm-mingw-$LlvmVersion-ucrt-x86_64"
    $zip = Join-Path $tools "$name.zip"
    New-Item -ItemType Directory -Force $tools | Out-Null
    if (-not (Test-Path $zip)) {
        Say "[1/4] 下载编译器 llvm-mingw（约 180 MB）…"
        # 先下到 .part，断了再运行会接着下
        $proxyArgs = if ($Proxy) { @("-x", $Proxy) } else { @() }
        & curl.exe -L --fail -# --retry 4 --retry-all-errors --retry-delay 2 -C - --connect-timeout 30 @proxyArgs `
            -o "$zip.part" "https://github.com/mstorsjo/llvm-mingw/releases/download/$LlvmVersion/$name.zip"
        if ($LASTEXITCODE -ne 0) { Fail "编译器下载失败，检查网络（代理）后再运行一次 setup.bat，会接着下" }
        Move-Item -Force "$zip.part" $zip
    } else {
        Say "[1/4] 编译器已经下载好了"
    }
    Say "      解压…"
    if (Test-Path (Join-Path $tools $name)) { Remove-Item -Recurse -Force (Join-Path $tools $name) }
    & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $tools
    if ($LASTEXITCODE -ne 0) { Fail "编译器解压失败，删掉 .tools 文件夹再试一次" }
    Rename-Item (Join-Path $tools $name) "llvm-mingw"
    Remove-Item $zip
}
$env:LLVM_MINGW = $llvm
$python = Join-Path $llvm "python\bin\python3.exe"
if (-not (Test-Path $python)) { $python = "python" }

# 2. 词库
$dataFiles = @("pinyin.bin", "wubi86.bin", "english.bin", "zh-hans-t-essay-bgw.gram", "emoji.txt", "chaizi.txt", "stroke.txt")
$missing = @($dataFiles | Where-Object { -not (Test-Path (Join-Path $root "data\$_")) })
if ($missing.Count -eq 0) {
    Say "[2/4] 词库已经有了，跳过"
} else {
    Say "[2/4] 下载并编译词库（约 80 MB）…"
    try {
        & (Join-Path $root "tools\fetch_data.ps1") -Proxy $Proxy -Python $python -SkipExisting
    } catch {
        Fail "词库没弄好：$($_.Exception.Message)`n检查网络（代理）后再运行一次 setup.bat，已经下好的会跳过"
    }
}

# 3. 编译、打包
Say "[3/4] 编译（一两分钟）…"
try {
    & (Join-Path $root "build.ps1") -SkipTest
    & (Join-Path $root "scripts\package.ps1") | Out-Null
} catch {
    Fail "编译失败：$($_.Exception.Message)"
}

# 4. 安装
if ($NoInstall) {
    Say "[4/4] 跳过安装。安装包在 $root\dist\yyime，双击里面的 安装.bat 就能装"
    exit 0
}
Say "[4/4] 安装：马上会弹出管理员确认窗口，请点「是」"
& (Join-Path $root "dist\yyime\install.ps1")
