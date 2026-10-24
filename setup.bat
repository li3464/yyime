@echo off
rem yyime one-click setup: downloads the compiler and dictionaries, builds, packages and installs.
rem Usage: setup.bat            or, behind a proxy: setup.bat -Proxy http://127.0.0.1:7890
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1" %*
pause
