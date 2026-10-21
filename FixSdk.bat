@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\FixSdk.ps1" %* || exit /b 1
