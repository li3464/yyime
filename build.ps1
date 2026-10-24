# Build yyime: 64-bit and 32-bit IME DLLs plus the engine test program.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 [-SkipTest]
# Toolchain: llvm-mingw; set LLVM_MINGW to its folder if it is not in C:\Users\17821\tools\llvm-mingw.
param([switch]$SkipTest)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$bin = if ($env:LLVM_MINGW) { "$env:LLVM_MINGW\bin" } else { "C:\Users\17821\tools\llvm-mingw\bin" }
$env:PATH = "$bin;$env:PATH"

$engine = @("util", "dict", "pinyin", "userdict", "config", "default_config", "phrases", "grammar", "converter", "engine", "session") |
    ForEach-Object { "$root\src\engine\$_.cpp" }
$tsf = @("$root\src\tsf\dllmain.cpp", "$root\src\tsf\register.cpp", "$root\src\tsf\text_service.cpp",
         "$root\src\tsf\display_attribute.cpp", "$root\src\tsf\langbar.cpp",
         "$root\src\ui\candidate_window.cpp", "$root\src\ai\ai_client.cpp")
$cflags = @("-std=c++20", "-O2", "-DUNICODE", "-D_UNICODE", "-DNOMINMAX", "-D_WIN32_WINNT=0x0A00", "-DWINVER=0x0A00",
            "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-missing-field-initializers", "-mguard=cf")
$libs = @("-lole32", "-loleaut32", "-luuid", "-ladvapi32", "-luser32", "-lgdi32", "-lshell32", "-ld2d1", "-ldwrite",
          "-lwinhttp", "-limm32", "-lwindowscodecs")

function Invoke-ArchBuild($triple, $outDir, $dllName) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $res = "$outDir\yyime.res.o"
    & "$bin\$triple-windres.exe" -I "$root\src" "$root\src\yyime.rc" -O coff -o $res
    if ($LASTEXITCODE -ne 0) { throw "windres failed ($triple)" }
    $clangArgs = $cflags + @("-shared", "-static", "-o", "$outDir\$dllName") + $engine + $tsf + @($res, "$root\src\yyime.def") + $libs
    & "$bin\$triple-clang++.exe" @clangArgs
    if ($LASTEXITCODE -ne 0) { throw "compile failed ($triple)" }
    Write-Output "built $outDir\$dllName"
}

Invoke-ArchBuild "x86_64-w64-mingw32" "$root\build\x64" "yyime64.dll"
Invoke-ArchBuild "i686-w64-mingw32" "$root\build\x86" "yyime32.dll"

& "$bin\x86_64-w64-mingw32-windres.exe" -I "$root\src" "$root\src\ui\settings.rc" -O coff -o "$root\build\settings.res.o"
if ($LASTEXITCODE -ne 0) { throw "settings resources failed" }
$settingsArgs = $cflags + @("-municode", "-mwindows", "-static", "-o", "$root\build\yyime-settings.exe") +
    $engine + @("$root\src\ui\settings_main.cpp", "$root\build\settings.res.o") + $libs + @("-lcomctl32", "-lgdiplus", "-ldwmapi")
& "$bin\x86_64-w64-mingw32-clang++.exe" @settingsArgs
if ($LASTEXITCODE -ne 0) { throw "settings build failed" }
Write-Output "built $root\build\yyime-settings.exe"

if (-not $SkipTest) {
    $clangArgs = $cflags + @("-municode", "-static", "-o", "$root\build\engine_test.exe") + $engine + @("$root\src\ai\ai_client.cpp", "$root\test\engine_test.cpp") + $libs
    & "$bin\x86_64-w64-mingw32-clang++.exe" @clangArgs
    if ($LASTEXITCODE -ne 0) { throw "test build failed" }
    Write-Output "built $root\build\engine_test.exe"
}
