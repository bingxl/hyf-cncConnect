@echo off
REM ============================================================
REM  Build script for CNC Monitor (CMake + MSVC)
REM  Usage:
REM    build.bat           - Configure and build (Release)
REM    build.bat clean     - Clean build directory
REM    build.bat debug     - Build Debug configuration
REM ============================================================

setlocal enabledelayedexpansion
set "BUILD_DIR=build"
set "CONFIG=Release"

if "%1"=="clean" (
    echo Cleaning build directory ...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo Done.
    exit /b 0
)

if "%1"=="debug" set "CONFIG=Debug"

REM --- Locate CMake ---
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH
    echo        Install CMake: https://cmake.org/download/
    exit /b 1
)

REM --- Find and setup Visual Studio environment ---
set "VCVARS="
set "CACHE_FILE=%~dp0.vsbuild_cache"

if exist "%CACHE_FILE%" (
    set /p VCVARS=<"%CACHE_FILE%"
    if exist "!VCVARS!" goto :setup_env
)

REM -- VS 18 (VS2026) --
for %%V in (Community BuildTools Professional Enterprise) do (
    set "TEST=%ProgramFiles%\Microsoft Visual Studio\18\%%V\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!TEST!" set "VCVARS=!TEST!"
    if not "!VCVARS!"=="" goto :save_cache
    set "TEST=%ProgramFiles(x86)%\Microsoft Visual Studio\18\%%V\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!TEST!" set "VCVARS=!TEST!"
    if not "!VCVARS!"=="" goto :save_cache
)

REM -- VS 2022 --
for %%V in (Community BuildTools Professional Enterprise) do (
    set "TEST=%ProgramFiles%\Microsoft Visual Studio\2022\%%V\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!TEST!" set "VCVARS=!TEST!"
    if not "!VCVARS!"=="" goto :save_cache
    set "TEST=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%V\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!TEST!" set "VCVARS=!TEST!"
    if not "!VCVARS!"=="" goto :save_cache
)

if "!VCVARS!"=="" (
    echo [ERROR] Cannot find Visual Studio (vcvarsall.bat)
    exit /b 1
)

:save_cache
echo !VCVARS!>"%CACHE_FILE%"

:setup_env
echo [1/3] Setting up MSVC environment (x86) ...
call "!VCVARS!" x86 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to setup MSVC environment
    exit /b 1
)

REM --- Configure ---
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM --- Drop stale CMake cache from a different generator (e.g. VS "Open Folder") ---
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /C:"CMAKE_GENERATOR:INTERNAL=Ninja" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 (
        echo [INFO] Stale CMake cache detected (non-Ninja), cleaning build cache ...
        rmdir /s /q "%BUILD_DIR%\CMakeFiles" 2>nul
        del /q "%BUILD_DIR%\CMakeCache.txt" 2>nul
    )
)

echo [2/3] Configuring (Ninja, %CONFIG%) ...
cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_C_COMPILER=cl.exe -DCMAKE_LINKER=link.exe
if errorlevel 1 (
    echo [ERROR] CMake configure failed
    exit /b 1
)

REM --- Build ---
echo [3/3] Building (%CONFIG%) ...
cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo [ERROR] Build failed
    exit /b 1
)

echo.
echo ============================================================
echo  Build successful!
echo.
echo  Executables:
echo    %BUILD_DIR%\cnc_monitor.exe
echo    %BUILD_DIR%\cnc_collect.exe
echo    %BUILD_DIR%\cnc_win32ui.exe
echo.
echo  Usage:
echo    %BUILD_DIR%\cnc_monitor.exe ^<IP^> [port]
echo    %BUILD_DIR%\cnc_collect.exe
echo    %BUILD_DIR%\cnc_win32ui.exe
echo ============================================================

endlocal
