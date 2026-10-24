# 把编译好的 DLL、词库和安装脚本收进 dist\yyime。
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root "dist\yyime"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $dist "data") | Out-Null

Copy-Item "$root\build\x64\yyime64.dll" $dist
Copy-Item "$root\build\x86\yyime32.dll" $dist
foreach ($f in @("pinyin.bin", "wubi86.bin", "english.bin")) { Copy-Item "$root\data\$f" "$dist\data\$f" }
foreach ($f in @("install.ps1", "uninstall.ps1", "README.txt")) { Copy-Item "$root\scripts\$f" $dist }

# 双击用的 .bat：内容只有英文，文件名用中文
$ascii = New-Object System.Text.ASCIIEncoding
$batInstall = "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0install.ps1`"`r`npause`r`n"
$batUninstall = "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0uninstall.ps1`"`r`npause`r`n"
[System.IO.File]::WriteAllText((Join-Path $dist "安装.bat"), $batInstall, $ascii)
[System.IO.File]::WriteAllText((Join-Path $dist "卸载.bat"), $batUninstall, $ascii)

Get-ChildItem $dist -Recurse -File | ForEach-Object {
    "{0,-22} {1,10:N0}" -f $_.FullName.Substring($dist.Length + 1), $_.Length
}
