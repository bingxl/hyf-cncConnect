@echo off
REM ============================================================
REM  view.bat - show collection stats
REM  default: last 24h / 30min buckets
REM  usage: view.bat [bucket_sec] [from_unix] [to_unix]
REM         e.g. view.bat 1800
REM              view.bat 1800 <yesterday0_unix> <now>
REM ============================================================
set "HERE=%~dp0"
cd /d "%HERE%"
chcp 65001 >nul
bin\mtc_stats.exe report stats.db %~1 %~2 %~3
echo.
echo Press any key to close...
pause >nul
