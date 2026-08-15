@echo off
REM ============================================================
REM  run_all.bat - start everything (independent windows, safe to
REM  close this terminal afterwards)
REM    collectors + agent + stats poller + web server
REM ============================================================
set "HERE=%~dp0"
cd /d "%HERE%"
call "%HERE%start.bat"
call "%HERE%start_poll.bat"
call "%HERE%start_web.bat"
echo.
echo All services are running:
echo   MTConnect agent : http://127.0.0.1:5000
echo   Web dashboard   : http://127.0.0.1:8088
echo   Stats           : stats.db
echo Stop with: stop.bat
