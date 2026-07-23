@echo off
REM ============================================================
REM  Build script for CNC Monitor (FOCAS2)
REM  Uses official FANUC fwlib32.h + Fwlib32.dll/.lib from:
REM  https://github.com/strangesast/fwlib
REM
REM  Compiler: Visual Studio Build Tools (cl.exe)
REM  Target:   x86 (32-bit) - required by Fwlib32.lib
REM ============================================================

setlocal enabledelayedexpansion
set "CACHE_FILE=%~dp0.vsbuild_cache"
set "VCVARS="

REM --- Check cache first ---
if not exist "%CACHE_FILE%" goto :search_vs
set /p VCVARS=<"%CACHE_FILE%"
if "!VCVARS!"=="" goto :search_vs
if exist "!VCVARS!" (
    echo [1/5] Found cached VSBuild path
    goto :setup_env
)
echo [1/5] Cache invalid, searching VSBuild ...

:search_vs
echo [1/5] Searching for Visual Studio ...

set "VCVARS="

REM -- Check VS 18 (VS2026) --
if not "!VCVARS!"=="" goto :search_2022
set "TEST=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles%\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles(x86)%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache

:search_2022
REM -- Check VS 2022 --
if not "!VCVARS!"=="" goto :search_end
set "TEST=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"
if not "!VCVARS!"=="" goto :save_cache
set "TEST=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if exist "!TEST!" set "VCVARS=!TEST!"

:search_end
if "!VCVARS!"=="" (
    echo [ERROR] Cannot find Visual Studio (vcvarsall.bat)
    echo        Searched: VS 18/2022 (BuildTools/Community/Professional/Enterprise)
    exit /b 1
)

:save_cache
echo       Found: !VCVARS!
echo !VCVARS!>"%CACHE_FILE%"
echo       Saved to cache

:setup_env
echo [2/5] Setting up MSVC environment (x86) ...
call "!VCVARS!" x86 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to setup MSVC environment
    exit /b 1
)
echo       OK

echo.
echo [3/5] Compiling cnc_monitor.exe ...
cl.exe /nologo /W3 /O2 /I fwlib /D ONO8D /Fe:cnc_monitor.exe main.c cnc_ops.c /link fwlib\Fwlib32.lib advapi32.lib
if errorlevel 1 (
    echo.
    echo [ERROR] Compilation of cnc_monitor.exe failed
    exit /b 1
)
echo       OK

echo.
echo [4/5] Compiling cnc_collect.exe ...
cl.exe /nologo /W3 /O2 /I fwlib /D ONO8D /D _CRT_SECURE_NO_WARNINGS /Fe:cnc_collect.exe collect.c cnc_ops.c file_io.c /link fwlib\Fwlib32.lib advapi32.lib
if errorlevel 1 (
    echo.
    echo [ERROR] Compilation of cnc_collect.exe failed
    exit /b 1
)
echo       OK

echo.
echo [5/5] Copying Fwlib32.dll ...
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
