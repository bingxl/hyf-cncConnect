@echo off
REM ============================================================
REM  status.bat - show machine connectivity via the agent
REM  Usage: status.bat [http_port]
REM ============================================================
set "PORT=%~1"
if "%PORT%"=="" set "PORT=5000"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0status.ps1" -Port %PORT%
