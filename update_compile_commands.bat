@echo off
xmake project -k compile_commands
mv compile_commands.json .vscode/compile_commands.json