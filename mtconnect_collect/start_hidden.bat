@echo off
REM ============================================================
REM  start_hidden.bat - start everything with NO console windows
REM  (uses wscript hidden_run.vbs; safe to close any terminal)
REM ============================================================
set "HERE=%~dp0"
set "HTTP_PORT=5000"
set "BASE_PORT=7878"

set "JICHUANG=%HERE%jichuang.txt"
if not exist "%JICHUANG%" set "JICHUANG=%HERE%..\jichuang.txt"

REM --- stop leftovers (hidden) ---
wscript "%HERE%tools\hidden_run.vbs" "cmd /c taskkill /F /IM agent.exe 2>nul & taskkill /F /IM fanuc_adapter.exe 2>nul & taskkill /F /IM mazak_adapter.exe 2>nul & taskkill /F /IM mtc_stats.exe 2>nul & taskkill /F /IM webserver.exe 2>nul & taskkill /F /IM shdr_sim.exe 2>nul & taskkill /F /IM mazak_sim.exe 2>nul"

REM --- regenerate agent config ---
"%HERE%bin\genconfig.exe" "%JICHUANG%" "%HERE%agent" %HTTP_PORT% %BASE_PORT% 127.0.0.1 "%HERE%devices"
if errorlevel 1 exit /b 1

if not exist "%HERE%log" mkdir "%HERE%log"

REM --- start collectors hidden, one per machine ---
for /f "usebackq tokens=1-5" %%t in ("%HERE%agent\adapters.txt") do (
    if "%%t"=="FANUC" (
        wscript "%HERE%tools\hidden_run.vbs" "cmd /c cd /d %HERE%agent\adapters\%%u && %HERE%bin\fanuc_adapter.exe run %%v %%w %%x > %HERE%log\%%u.log 2>&1"
    ) else if "%%t"=="MAZAK" (
        wscript "%HERE%tools\hidden_run.vbs" "cmd /c %HERE%bin\mazak_adapter.exe run %%v %%w %%x > %HERE%log\%%u.log 2>&1"
    ) else if "%%t"=="SIM" (
        wscript "%HERE%tools\hidden_run.vbs" "%HERE%bin\shdr_sim.exe %%x 500 > %HERE%log\%%u.log 2>&1"
    )
)

REM --- agent ---
wscript "%HERE%tools\hidden_run.vbs" "cmd /c cd /d %HERE%agent && %HERE%agent\agent.exe run agent.cfg > %HERE%log\agent_console.log 2>&1"

REM --- stats poller ---
wscript "%HERE%tools\hidden_run.vbs" "cmd /c cd /d %HERE% && %HERE%bin\mtc_stats.exe poll 5000 10 stats.db > %HERE%log\stats_poll.log 2>&1"

REM --- web server ---
wscript "%HERE%tools\hidden_run.vbs" "cmd /c cd /d %HERE% && %HERE%bin\webserver.exe 8088 stats.db 5000 %HERE%web\dist > %HERE%log\webserver.log 2>&1"

echo All services started (hidden).
