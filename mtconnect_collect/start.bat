@echo off
REM ============================================================
REM  start.bat - launch MTConnect collection (multi-system)
REM   1. reads jichuang.txt (name,type,ip,port)
REM   2. regenerates agent config (agent.cfg + Devices.xml + adapters.txt)
REM   3. starts collectors per machine type:
REM        FANUC -> fanuc_adapter.exe (FOCAS2)
REM        MAZAK -> mazak_adapter.exe (MTConnect pull)
REM        SIM   -> shdr_sim.exe       (offline)
REM        SHDR  -> (none; agent connects directly to remote SHDR)
REM   4. starts the MTConnect agent (HTTP)
REM
REM  Usage:  start.bat [agent_http_port] [shdr_base_port]
REM ============================================================
setlocal enabledelayedexpansion
set "HERE=%~dp0"
set "HTTP_PORT=%~1"
if "%HTTP_PORT%"=="" set "HTTP_PORT=5000"
set "BASE_PORT=%~2"
if "%BASE_PORT%"=="" set "BASE_PORT=7878"

REM --- locate jichuang.txt (local copy, or the one next to the project) ---
set "JICHUANG=%HERE%jichuang.txt"
if not exist "%JICHUANG%" set "JICHUANG=%HERE%..\jichuang.txt"
if not exist "%JICHUANG%" (
    echo [ERROR] jichuang.txt not found. Copy it here or place it in:
    echo         %HERE%..\jichuang.txt
    exit /b 1
)
echo [INFO] machine list: %JICHUANG%

REM --- stop any leftover processes to avoid port/file conflicts ---
call "%HERE%stop.bat"

REM --- regenerate agent config from the machine list ---
echo [1/4] Generating agent configuration ...
"%HERE%bin\genconfig.exe" "%JICHUANG%" "%HERE%agent" %HTTP_PORT% %BASE_PORT% 127.0.0.1 "%HERE%devices"
if errorlevel 1 exit /b 1

if not exist "%HERE%log" mkdir "%HERE%log"

REM --- start collectors per machine type ---
echo [2/4] Starting collectors ...
for /f "usebackq tokens=1-5" %%t in ("%HERE%agent\adapters.txt") do (
    if "%%t"=="FANUC" (
        echo   FANUC  %%~u  %%v:%%w  -^>  SHDR %%x
        start "mtc-%%u" /min cmd /c "cd /d %HERE%agent\adapters\%%u && %HERE%bin\fanuc_adapter.exe run %%v %%w %%x > %HERE%log\%%u.log 2>&1"
    ) else if "%%t"=="MAZAK" (
        echo   MAZAK  %%~u  %%v:%%w  -^>  SHDR %%x
        start "mtc-%%u" /min cmd /c "%HERE%bin\mazak_adapter.exe run %%v %%w %%x > %HERE%log\%%u.log 2>&1"
    ) else if "%%t"=="SIM" (
        echo   SIM    %%~u  -^>  SHDR %%x
        start "sim-%%u" /min "%HERE%bin\shdr_sim.exe" %%x 500
    ) else if "%%t"=="SHDR" (
        echo   SHDR   %%~u  passthrough  %%v:%%w
    )
)

REM --- start the agent ---
echo [3/4] Starting MTConnect agent  ^(http://127.0.0.1:%HTTP_PORT%^) ...
start "mtc-agent" /min cmd /c "cd /d %HERE%agent && agent.exe run agent.cfg > %HERE%log\agent_console.log 2>&1"

echo [4/4] Done.
echo.
echo   Check:   http://127.0.0.1:%HTTP_PORT%/probe
echo            http://127.0.0.1:%HTTP_PORT%/current
echo   Logs:    %HERE%log\
echo   Stop:    stop.bat
endlocal
