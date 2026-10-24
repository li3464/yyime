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

# 保留旧版本；正在使用的 DLL 改名后，重新启动的应用会加载新版。
function Copy-Safe([string]$src, [string]$dst) {
    if (Test-Path $dst) {
        $stamp = Get-Date -Format "yyyyMMddHHmmssfff"
        Rename-Item -LiteralPath $dst -NewName ((Split-Path $dst -Leaf) + ".old" + $stamp) -ErrorAction Stop
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

try {
    Say "== yy输入法 安装 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =="
    foreach ($f in @("yyime64.dll", "yyime32.dll", "yyime-settings.exe", "data\pinyin.bin", "data\wubi86.bin", "data\english.bin", "data\zh-hans-t-essay-bgw.gram")) {
        if (-not (Test-Path (Join-Path $Source $f))) { throw "安装包里缺少文件：$f" }
    }
    foreach ($name in @('deepseek','gpt','claude','gemini','qwen','glm','minimax','zcode','kimi','grok')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Source "themes\$name.png"))) { throw "Missing character artwork: $name" }
    }
    New-Item -ItemType Directory -Force $Target | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $Target "data") | Out-Null
    foreach ($f in @("yyime64.dll", "yyime32.dll", "data\pinyin.bin", "data\wubi86.bin", "data\english.bin",
                     "data\zh-hans-t-essay-bgw.gram", "yyime-settings.exe", "uninstall.ps1", "README.txt")) {
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

    Add-Type -Namespace YY -Name Input -MemberDefinition @'
[DllImport("input.dll", CharSet = CharSet.Unicode)]
public static extern bool InstallLayoutOrTip(string psz, uint dwFlags);
'@
    if ([YY.Input]::InstallLayoutOrTip("0804:$CLSID$ProfileGuid", 0)) {
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
