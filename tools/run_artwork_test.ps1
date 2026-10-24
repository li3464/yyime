param([ValidateSet('x64','x86')][string]$Arch = 'x64')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = if ($env:LLVM_MINGW) { "$env:LLVM_MINGW\bin" } else { 'C:\Users\17821\tools\llvm-mingw\bin' }
$triple = if ($Arch -eq 'x64') { 'x86_64-w64-mingw32' } else { 'i686-w64-mingw32' }
$engine = @('util','dict','pinyin','userdict','config','default_config','phrases','grammar','converter','engine','session') |
    ForEach-Object { "$root\src\engine\$_.cpp" }
$target = "$root\build\artwork_test_$Arch.exe"
& "$bin\$triple-clang++.exe" -std=c++20 -O2 -municode -static -DUNICODE -D_UNICODE -DNOMINMAX `
    @engine "$root\src\ui\candidate_window.cpp" "$root\test\artwork_test.cpp" `
    -lole32 -lwindowscodecs -luuid -lshell32 -ladvapi32 -luser32 -lgdi32 -ld2d1 -ldwrite -o $target
if ($LASTEXITCODE -ne 0) { throw 'artwork test build failed' }
& $target
if ($LASTEXITCODE -ne 0) { throw 'artwork rendering checks failed' }
