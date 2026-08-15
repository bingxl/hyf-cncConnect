@echo off
set "HERE=%~dp0"
start "mtc-web" /min cmd /c "cd /d %HERE% && bin\webserver.exe 8088 stats.db 5000 web\dist > log\webserver.log 2>&1"
echo webserver started
