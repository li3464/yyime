param([string]$MockBaseUrl = "")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = if ($env:LLVM_MINGW) { "$env:LLVM_MINGW\bin" } else { 'C:\Users\17821\tools\llvm-mingw\bin' }
$engine = @('util','dict','pinyin','userdict','config','default_config','phrases','grammar','converter','engine','session') |
    ForEach-Object { "$root\src\engine\$_.cpp" }
New-Item -ItemType Directory -Force "$root\build" | Out-Null
& "$bin\x86_64-w64-mingw32-clang++.exe" -std=c++20 -O2 -municode -static -DUNICODE -D_UNICODE -DNOMINMAX `
    @engine "$root\src\ai\ai_client.cpp" "$root\test\regression_test.cpp" `
    -lole32 -lshell32 -ladvapi32 -luser32 -lwinhttp -luuid -o "$root\build\regression_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'regression test build failed' }
$testDir = Join-Path "$root\build" ('test-user-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
$arguments = @("$root\data", $testDir)
if ($MockBaseUrl) { $arguments += $MockBaseUrl }
& "$root\build\regression_test.exe" @arguments
if ($LASTEXITCODE -ne 0) { throw 'regression tests failed' }
