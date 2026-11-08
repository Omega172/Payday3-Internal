#!/usr/bin/env bash
exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(dirname "$0")/ProfileGame.ps1" "$@"
