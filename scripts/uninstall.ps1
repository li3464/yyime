# yy输入法 卸载脚本。需要管理员权限，会自动弹出确认窗口。你的设置和自造词（%APPDATA%\yyime）会保留。
param([switch]$Elevated)
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
    Write-Host "卸载 yy输入法 需要管理员权限，马上会弹出确认窗口，请点「是」。"
    Remove-Item $LogFile -ErrorAction SilentlyContinue
    $script = $PSCommandPath
    if ($script -like "$Target*") {
        # 脚本自己就在要删的文件夹里：先拷到临时目录再运行
        $tmp = Join-Path $env:TEMP "yyime-uninstall.ps1"
        Copy-Item $script $tmp -Force
        $script = $tmp
    }
    try {
        $p = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$script`" -Elevated"
    } catch {
        Write-Host "没有拿到管理员权限，卸载取消了。"
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

try {
    Say "== yy输入法 卸载 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =="
    [YY.Native]::InstallLayoutOrTip("0804:$CLSID$ProfileGuid", 1) | Out-Null
    Say "已从输入法列表里移除"
    if (Test-Path "$Target\yyime64.dll") {
        Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList "/u /s `"$Target\yyime64.dll`"" -Wait | Out-Null
    }
    if (Test-Path "$Target\yyime32.dll") {
        Start-Process "$env:SystemRoot\SysWOW64\regsvr32.exe" -ArgumentList "/u /s `"$Target\yyime32.dll`"" -Wait | Out-Null
    }
    Say "已取消注册"
    $pending = 0
    if (Test-Path $Target) {
        Get-ChildItem $Target -Recurse -File | ForEach-Object {
            try {
                Remove-Item $_.FullName -Force -ErrorAction Stop
            } catch {
                [YY.Native]::MoveFileEx($_.FullName, $null, 4) | Out-Null  # 重启后删除
                $pending++
            }
        }
        Remove-Item $Target -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path $Target) { [YY.Native]::MoveFileEx($Target, $null, 4) | Out-Null }
    }
    if ($pending -gt 0) {
        Say "有 $pending 个文件正被打开的程序用着，重启电脑后会自动删掉"
    }
    Say "卸载完成。你的设置和自造词还在 $env:APPDATA\yyime，不要的话可以手动删掉这个文件夹。"
    exit 0
} catch {
    Say ("卸载失败：" + $_.Exception.Message)
    exit 1
}
