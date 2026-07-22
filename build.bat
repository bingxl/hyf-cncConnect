@echo off
REM ============================================================
REM  Build script for CNC Monitor (FOCAS2)
REM  Uses official FANUC fwlib32.h + Fwlib32.dll/.lib from:
REM  https://github.com/strangesast/fwlib
REM
REM  Compiler: Visual Studio Build Tools (cl.exe)
REM  Target:   x86 (32-bit) - required by Fwlib32.lib
REM ============================================================

setlocal

set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

echo [1/3] Setting up MSVC environment (x86) ...
call "%VCVARS%" x86 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to setup MSVC environment
    exit /b 1
)
echo       OK

echo.
echo [2/3] Compiling cnc_monitor.exe ...
cl.exe /nologo /W3 /O2 /I fwlib /D ONO8D /Fe:cnc_monitor.exe main.c cnc_ops.c /link fwlib\Fwlib32.lib advapi32.lib
if errorlevel 1 (
    echo.
    echo [ERROR] Compilation of cnc_monitor.exe failed
    exit /b 1
)
echo       OK

echo.
echo [3/3] Compiling cnc_collect.exe ...
cl.exe /nologo /W3 /O2 /I fwlib /D ONO8D /D _CRT_SECURE_NO_WARNINGS /Fe:cnc_collect.exe collect.c cnc_ops.c file_io.c /link fwlib\Fwlib32.lib advapi32.lib
if errorlevel 1 (
    echo.
    echo [ERROR] Compilation of cnc_collect.exe failed
    exit /b 1
)
echo       OK

echo.
echo [4/4] Copying Fwlib32.dll ...
copy /Y fwlib\Fwlib32.dll . >nul 2>&1
del /q *.obj 2>nul

echo.
echo ============================================================
echo  Build successful: cnc_monitor.exe + cnc_collect.exe
echo.
echo  Usage:
echo    cnc_monitor.exe ^<IP^> [port]   - Interactive CNC monitor
echo    cnc_collect.exe                - Batch collect data from jichuang.txt
echo.
echo  Fwlib32.dll has been copied to current directory.
echo ============================================================

endlocal
