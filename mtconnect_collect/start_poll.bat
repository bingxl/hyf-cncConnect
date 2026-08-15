@echo off
REM start the stats poller as an independent process (survives terminal close)
set "HERE=%~dp0"
start "mtc-stats" /min cmd /c "cd /d %HERE% && bin\mtc_stats.exe poll 5000 10 stats.db > log\stats_poll.log 2>&1"
echo poller started
