# 端到端测试：不用重装输入法，直接测 build 目录里刚编好的 DLL。
# 原理：测试程序旁边放一个清单文件，让 COM 从旁边加载 DLL，而不是用系统里登记的那份。
# 注意：测试时会有一个 2x2 像素的小窗口在右下角拿走键盘焦点一两秒。
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_host_test.ps1 [-Arch x64|x86]
param([ValidateSet("x64", "x86")][string]$Arch = "x64", [string]$MockBaseUrl = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$bin = if ($env:LLVM_MINGW) { "$env:LLVM_MINGW\bin" } else { "C:\Users\17821\tools\llvm-mingw\bin" }
$triple = if ($Arch -eq "x64") { "x86_64-w64-mingw32" } else { "i686-w64-mingw32" }
$dll = if ($Arch -eq "x64") { "yyime64.dll" } else { "yyime32.dll" }

$work = Join-Path "$root\build" ("hosttest-$Arch-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force "$work\user" | Out-Null

& "$bin\$triple-clang++.exe" -std=c++20 -O1 -municode -DUNICODE -D_UNICODE -DNOMINMAX "$root\test\tsf_host_test.cpp" `
    -static -lole32 -loleaut32 -luuid -luser32 -o "$work\host.exe"
if ($LASTEXITCODE -ne 0) { throw "编译测试程序失败" }
Copy-Item "$root\build\$Arch\$dll" $work
@"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="yyime.hosttest" version="1.0.0.0"/>
  <file name="$dll">
    <comClass clsid="{73F0FA18-2039-4C68-A952-D14AD44F3C9B}" threadingModel="Apartment"/>
  </file>
</assembly>
"@ | Set-Content -Encoding ASCII "$work\host.exe.manifest"
"[general]`r`ndebug_log = true`r`n[ui]`r`ntheme = deepseek`r`ncharacter_art = true`r`n" | Set-Content -Encoding ASCII "$work\user\yyime.ini"
if ($MockBaseUrl) {
    if ($MockBaseUrl -notmatch '^http://127\.0\.0\.1:\d+/v1$') { throw 'Only a loopback mock server is accepted' }
    "[ai]`r`nbase_url = $MockBaseUrl`r`napi_key = fake-test-only`r`nmodel = test-model" | Add-Content -Encoding ASCII "$work\user\yyime.ini"
}

$env:YYIME_USER_DIR = "$work\user"
$env:YYIME_DATA_DIR = "$root\data"
$hostArguments = "`"$work\result.txt`" local"
if ($MockBaseUrl) { $hostArguments += ' ai' }
$p = Start-Process "$work\host.exe" -ArgumentList $hostArguments -WorkingDirectory $work -Wait -PassThru -WindowStyle Hidden
Remove-Item Env:YYIME_USER_DIR, Env:YYIME_DATA_DIR
Get-Content "$work\result.txt" -Encoding UTF8
if ($p.ExitCode -ne 0) { exit $p.ExitCode }
