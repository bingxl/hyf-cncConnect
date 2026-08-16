@echo off
REM start the stats poller as an independent process (survives terminal close)
set "HERE=%~dp0"
start "mtc-stats" /min cmd /c "cd /d %HERE% && bin\mtc_stats.exe stream 5000 stats.db 5000 > log\stats_poll.log 2>&1"
echo poller started
