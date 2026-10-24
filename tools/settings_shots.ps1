# 开发用：只编译设置程序，并把每一页截图到 build\shots（窗口开在屏幕外，不会弹出来）。
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File tools\settings_shots.ps1 [-Theme deepseek] [-NoBuild]
param([string]$Theme = "deepseek", [switch]$NoBuild, [int[]]$Pages = @(0, 1, 2, 3))
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$bin = if ($env:LLVM_MINGW) { "$env:LLVM_MINGW\bin" } else { "C:\Users\17821\tools\llvm-mingw\bin" }
if (-not $NoBuild) {
    $engine = @("util", "dict", "pinyin", "userdict", "config", "default_config", "phrases", "grammar", "converter", "engine", "extras", "session") |
        ForEach-Object { "$root\src\engine\$_.cpp" }
    & "$bin\x86_64-w64-mingw32-windres.exe" -I "$root\src" "$root\src\ui\settings.rc" -O coff -o "$root\build\settings.res.o"
    if ($LASTEXITCODE -ne 0) { throw "windres failed" }
    $flags = @("-std=c++20", "-O2", "-DUNICODE", "-D_UNICODE", "-DNOMINMAX", "-D_WIN32_WINNT=0x0A00", "-DWINVER=0x0A00",
               "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-missing-field-initializers",
               "-municode", "-mwindows", "-static", "-o", "$root\build\yyime-settings.exe")
    $libs = @("-lole32", "-loleaut32", "-luuid", "-ladvapi32", "-luser32", "-lgdi32", "-lshell32", "-ld2d1", "-ldwrite",
              "-lwinhttp", "-limm32", "-lwindowscodecs", "-lcomctl32", "-lgdiplus", "-ldwmapi", "-luxtheme")
    & "$bin\x86_64-w64-mingw32-clang++.exe" @($flags + $engine + @("$root\src\ui\settings_main.cpp", "$root\build\settings.res.o") + $libs)
    if ($LASTEXITCODE -ne 0) { throw "settings build failed" }
}
$user = "$root\build\shot-user"
New-Item -ItemType Directory -Force $user, "$root\build\shots" | Out-Null
"[ui]`r`ntheme = $Theme`r`ncharacter_art = true`r`n[ai]`r`napi_key = sk-demo-key-for-screenshot`r`n" |
    Set-Content -Encoding ASCII "$user\yyime.ini"
foreach ($p in $Pages) {
    $out = "$root\build\shots\page$p.png"
    Remove-Item $out -ErrorAction SilentlyContinue
    $proc = Start-Process "$root\build\yyime-settings.exe" -ArgumentList "`"$user\yyime.ini`" --shot `"$out`" $p" -Wait -PassThru
    if (-not (Test-Path $out)) { throw "no screenshot for page $p (exit $($proc.ExitCode))" }
    Write-Output "page $p -> $out"
}
