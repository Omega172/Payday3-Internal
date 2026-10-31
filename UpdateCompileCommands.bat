@echo off
setlocal
if exist "%ProgramFiles%\LLVM\bin" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
cd /d "%~dp0"
if not exist .vscode mkdir .vscode
xmake project -k compile_commands .vscode || exit /b 1
echo compile_commands.json written to .vscode
