@echo off
REM ============================================================
REM  stop.bat - stop agent, collectors and simulators
REM ============================================================
echo Stopping MTConnect agent ...
taskkill /F /IM agent.exe 2>nul
echo Stopping FANUC collectors ...
taskkill /F /IM fanuc_adapter.exe 2>nul
echo Stopping MAZAK collectors ...
taskkill /F /IM mazak_adapter.exe 2>nul
echo Stopping simulators ...
taskkill /F /IM shdr_sim.exe 2>nul
taskkill /F /IM mazak_sim.exe 2>nul
echo Done.
