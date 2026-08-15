@echo off
REM ============================================================
REM  test.bat - offline demo with simulators
REM  Replaces FANUC/MAZAK entries in jichuang.txt with local
REM  simulators, then starts everything and prints a status table.
REM  Usage: test.bat [agent_http_port] [shdr_base_port]
REM ============================================================
setlocal enabledelayedexpansion
set "HERE=%~dp0"
set "HTTP_PORT=%~1"
if "%HTTP_PORT%"=="" set "HTTP_PORT=5000"
set "BASE_PORT=%~2"
if "%BASE_PORT%"=="" set "BASE_PORT=7878"

set "JICHUANG=%HERE%jichuang.txt"
if not exist "%JICHUANG%" set "JICHUANG=%HERE%..\jichuang.txt"
if not exist "%JICHUANG%" (
    echo [ERROR] jichuang.txt not found
    exit /b 1
)

call "%HERE%stop.bat"

echo [1/4] Generating config with SIM for every machine ...
"%HERE%bin\genconfig.exe" "%JICHUANG%" "%HERE%agent" %HTTP_PORT% %BASE_PORT% 127.0.0.1 "%HERE%devices"
if errorlevel 1 exit /b 1

if not exist "%HERE%log" mkdir "%HERE%log"

echo [2/4] Starting simulators ...
for /f "usebackq tokens=1-5" %%t in ("%HERE%agent\adapters.txt") do (
    echo   simulating %%~u  on SHDR %%x
    start "sim-%%u" /min "%HERE%bin\shdr_sim.exe" %%x 500
)
echo [3/4] Starting MTConnect agent  ^(http://127.0.0.1:%HTTP_PORT%^) ...
start "mtc-agent" /min cmd /c "cd /d %HERE%agent && agent.exe run agent.cfg > %HERE%log\agent_console.log 2>&1"

echo [4/4] Waiting 6s for the agent to collect data ...
ping -n 7 127.0.0.1 >nul

call "%HERE%status.bat" %HTTP_PORT%
echo.
echo  Stop the demo:  stop.bat
endlocal
