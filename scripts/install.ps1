# yy输入法 安装脚本。需要管理员权限，会自动弹出确认窗口。
param([string]$Source = $PSScriptRoot, [switch]$Elevated)
$ErrorActionPreference = "Stop"
$CLSID = "{73F0FA18-2039-4C68-A952-D14AD44F3C9B}"
$ProfileGuid = "{78C27759-B3C0-443B-A0BF-1AC02088F263}"
$Target = Join-Path $env:ProgramFiles "yyime"
$LogFile = Join-Path $env:TEMP "yyime-install.log"

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
    Write-Host "安装 yy输入法 需要管理员权限，马上会弹出确认窗口，请点「是」。"
    Remove-Item $LogFile -ErrorAction SilentlyContinue
    try {
        $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Source `"$Source`" -Elevated"
        $p = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList $argList
    } catch {
        Write-Host "没有拿到管理员权限，安装取消了。"
        exit 1
    }
    if (Test-Path $LogFile) { Get-Content $LogFile -Encoding UTF8 | ForEach-Object { Write-Host $_ } }
    exit $p.ExitCode
}

function Say([string]$m) {
    Write-Host $m
    Add-Content -Path $LogFile -Value $m -Encoding UTF8
}

Add-Type -Namespace YY -Name Native -MemberDefinition @'
[DllImport("input.dll", CharSet = CharSet.Unicode)]
public static extern bool InstallLayoutOrTip(string psz, uint dwFlags);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool MoveFileEx(string existing, string replacement, int flags);
'@

# 删掉一个文件；正被别的程序占着就登记成重启后删除
function Remove-OrLater([string]$path) {
    try { Remove-Item -LiteralPath $path -Force -ErrorAction Stop }
    catch { [YY.Native]::MoveFileEx($path, $null, 4) | Out-Null }  # 4 = 重启后删除
}

# 能直接覆盖就覆盖。正被打字的程序占着的（DLL、词库）没法覆盖：改名挪开再放新的，
# 程序重新打开就用上新版；挪开的旧文件重启后自动删掉，不会越攒越多
function Copy-Safe([string]$src, [string]$dst) {
    if (Test-Path $dst) {
        try {
            Copy-Item -LiteralPath $src -Destination $dst -Force -ErrorAction Stop
            return
        } catch { }
        $old = $dst + ".old" + (Get-Date -Format "yyyyMMddHHmmssfff")
        Rename-Item -LiteralPath $dst -NewName (Split-Path $old -Leaf) -ErrorAction Stop
        Remove-OrLater $old
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

try {
    Say "== yy输入法 安装 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =="
    # 以前的版本挪开的旧文件（*.oldXXXX）：没人用了就删，还在用的等重启后删
    if (Test-Path $Target) {
        $leftovers = @(Get-ChildItem -LiteralPath $Target -Recurse -File -Filter "*.old*" -ErrorAction SilentlyContinue)
        if ($leftovers.Count -gt 0) {
            $mb = ($leftovers | Measure-Object Length -Sum).Sum / 1MB
            $leftovers | ForEach-Object { Remove-OrLater $_.FullName }
            Say ("清理了以前安装留下的 {0} 个旧文件（{1:N0} MB，正被占用的重启后删）" -f $leftovers.Count, $mb)
        }
    }
    foreach ($f in @("yyime64.dll", "yyime32.dll", "yyime-settings.exe", "data\pinyin.bin", "data\wubi86.bin", "data\english.bin", "data\zh-hans-t-essay-bgw.gram")) {
        if (-not (Test-Path (Join-Path $Source $f))) { throw "安装包里缺少文件：$f" }
    }
    foreach ($name in @('deepseek','gpt','claude','gemini','qwen','glm','minimax','zcode','kimi','grok')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Source "themes\$name.png"))) { throw "Missing character artwork: $name" }
    }
    New-Item -ItemType Directory -Force $Target | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $Target "data") | Out-Null
    foreach ($f in @("yyime64.dll", "yyime32.dll", "data\pinyin.bin", "data\wubi86.bin", "data\english.bin",
                     "data\zh-hans-t-essay-bgw.gram", "data\emoji.txt", "data\chaizi.txt", "data\stroke.txt",
                     "yyime-settings.exe", "uninstall.ps1", "README.txt")) {
        $src = Join-Path $Source $f
        if (Test-Path $src) { Copy-Safe $src (Join-Path $Target $f) }
    }
    Say "文件已复制到 $Target"
    if (Test-Path -LiteralPath (Join-Path $Source "licenses")) {
        Copy-Item -LiteralPath (Join-Path $Source "licenses") -Destination $Target -Recurse -Force
    }
    if (Test-Path -LiteralPath (Join-Path $Source "themes")) {
        Copy-Item -LiteralPath (Join-Path $Source "themes") -Destination $Target -Recurse -Force
    }
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $Target "输入法设置.lnk"))
    $shortcut.TargetPath = Join-Path $Target "yyime-settings.exe"
    $shortcut.Save()

    $r64 = Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList "/s `"$Target\yyime64.dll`"" -Wait -PassThru
    if ($r64.ExitCode -ne 0) { throw "注册 64 位组件失败（代码 $($r64.ExitCode)）" }
    $r32 = Start-Process "$env:SystemRoot\SysWOW64\regsvr32.exe" -ArgumentList "/s `"$Target\yyime32.dll`"" -Wait -PassThru
    if ($r32.ExitCode -ne 0) { throw "注册 32 位组件失败（代码 $($r32.ExitCode)）" }
    Say "已注册 64 位和 32 位组件"

    if ([YY.Native]::InstallLayoutOrTip("0804:$CLSID$ProfileGuid", 0)) {
        Say "已加入「中文(简体)」的输入法列表"
    } else {
        Say "没能自动加进输入法列表，可以在 设置 → 时间和语言 → 语言和区域 → 中文 → 语言选项 → 添加键盘 里手动加"
    }

    # 让商店应用（比如开始菜单搜索框）也能读到你的设置和词库
    $userDir = Join-Path $env:APPDATA "yyime"
    New-Item -ItemType Directory -Force $userDir | Out-Null
    & icacls.exe $userDir /grant "*S-1-15-2-1:(OI)(CI)(RX)" | Out-Null

    Say "装好了！按 Win+空格 切换到「yy输入法」就能用。"
    Say "设置文件：$userDir\yyime.ini（右键任务栏上的 中/英 图标也能打开）"
    exit 0
} catch {
    Say ("安装失败：" + $_.Exception.Message)
    exit 1
}
