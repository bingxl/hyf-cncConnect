@echo off
REM ============================================================
REM  mtconnect_collect - build all collectors + tools (MSVC x86)
REM
REM  Builds:
REM    bin\fanuc_adapter.exe   FANUC collector (FOCAS2 -> SHDR)
REM    bin\mazak_adapter.exe   MAZAK collector (MTConnect pull -> SHDR)
REM    bin\genconfig.exe       generate agent config from jichuang.txt
REM    bin\shdr_sim.exe        offline SHDR simulator
REM    bin\mazak_sim.exe       offline MAZAK (pull-mode) simulator
REM    bin\mtc_stats.exe       sampling + report tool (needs SQLite)
REM ============================================================
setlocal enabledelayedexpansion

set "HERE=%~dp0"
set "FW=%~dp0..\third_party\fwlib"
set "SQLITE=%HERE%..\third_party\sqlite3"

REM --- find vcvarsall ---
set "VCVARS="
set "CACHE_FILE=%HERE%.vsbuild_cache"
if exist "%CACHE_FILE%" (
    set /p VCVARS=<"%CACHE_FILE%"
    if exist "!VCVARS!" goto :setup_env
)
for %%V in (Community BuildTools Professional Enterprise) do (
    set "TEST=%ProgramFiles(x86)%\Microsoft Visual Studio\18\%%V\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!TEST!" set "VCVARS=!TEST!"
    if not "!VCVARS!"=="" goto :save_cache
    set "TEST=%ProgramFiles%\Microsoft Visual Studio\18\%%V\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!TEST!" set "VCVARS=!TEST!"
    if not "!VCVARS!"=="" goto :save_cache
)
for %%V in (Community BuildTools Professional Enterprise) do (
    set "TEST=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%V\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!TEST!" set "VCVARS=!TEST!"
    if not "!VCVARS!"=="" goto :save_cache
    set "TEST=%ProgramFiles%\Microsoft Visual Studio\2022\%%V\VC\Auxiliary\Build\vcvarsall.bat"
    if exist "!TEST!" set "VCVARS=!TEST!"
    if not "!VCVARS!"=="" goto :save_cache
)
if "!VCVARS!"=="" (
    echo [ERROR] Cannot find Visual Studio vcvarsall.bat
    exit /b 1
)
:save_cache
echo !VCVARS!>"%CACHE_FILE%"

:setup_env
echo [1/5] Setting up MSVC environment (x86) ...
call "!VCVARS!" x86 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to setup MSVC environment
    exit /b 1
)

if not exist "%HERE%bin" mkdir "%HERE%bin"

set "SRC=%HERE%src"
set "COMMON=%SRC%\adapter.cpp %SRC%\client.cpp %SRC%\condition_list.cpp %SRC%\device_datum.cpp %SRC%\logger.cpp %SRC%\server.cpp %SRC%\service.cpp %SRC%\string_buffer.cpp %SRC%\minIni.c"

echo [2/5] Building fanuc_adapter.exe ...
cl /nologo /O2 /EHsc /utf-8 /D WIN32 /D _CRT_SECURE_NO_WARNINGS ^
    /I "%SRC%" /I "%SRC%\fanuc" /I "%FW%" ^
    %COMMON% %SRC%\fanuc\FanucAdapter.cpp %SRC%\fanuc\fanuc_adapter.cpp ^
    /Fe:"%HERE%bin\fanuc_adapter.exe" ^
    /link "%FW%\Fwlib32.lib" wsock32.lib
if errorlevel 1 (
    echo [ERROR] fanuc_adapter build failed
    exit /b 1
)

echo [3/5] Building mazak_adapter.exe ...
cl /nologo /O2 /EHsc /utf-8 /D WIN32 /D _CRT_SECURE_NO_WARNINGS ^
    /I "%SRC%" /I "%SRC%\mazak" ^
    %COMMON% %SRC%\mazak\mazak_adapter.cpp ^
    /Fe:"%HERE%bin\mazak_adapter.exe" ^
    /link wsock32.lib
if errorlevel 1 (
    echo [ERROR] mazak_adapter build failed
    exit /b 1
)

echo [4/5] Building tools ...
cl /nologo /O2 /D _CRT_SECURE_NO_WARNINGS "%HERE%tools\genconfig.c" /Fe:"%HERE%bin\genconfig.exe"
if errorlevel 1 ( echo [ERROR] genconfig build failed & exit /b 1 )
cl /nologo /O2 /D _CRT_SECURE_NO_WARNINGS "%HERE%tools\shdr_sim.c" /Fe:"%HERE%bin\shdr_sim.exe" /link wsock32.lib
if errorlevel 1 ( echo [ERROR] shdr_sim build failed & exit /b 1 )
cl /nologo /O2 /D _CRT_SECURE_NO_WARNINGS "%HERE%tools\mazak_sim.c" /Fe:"%HERE%bin\mazak_sim.exe" /link wsock32.lib
if errorlevel 1 ( echo [ERROR] mazak_sim build failed & exit /b 1 )

echo [5/5] Building mtc_stats.exe ...
if not exist "%HERE%bin\sqlite3.obj" (
    cl /nologo /O2 /c /D SQLITE_THREADSAFE=0 /D _CRT_SECURE_NO_WARNINGS "%SQLITE%\sqlite3.c" /Fo:"%HERE%bin\sqlite3.obj"
    if errorlevel 1 ( echo [ERROR] sqlite3 build failed & exit /b 1 )
)
cl /nologo /O2 /D _CRT_SECURE_NO_WARNINGS /I "%SQLITE%" "%HERE%tools\mtc_stats.c" "%HERE%bin\sqlite3.obj" /Fe:"%HERE%bin\mtc_stats.exe" /link winhttp.lib
if errorlevel 1 ( echo [ERROR] mtc_stats build failed & exit /b 1 )

REM --- copy runtime files (gitignored binaries) ---
if not exist "%HERE%bin\Fwlib32.dll" (
    copy /y "%FW%\Fwlib32.dll" "%HERE%bin\" >nul
)
if not exist "%HERE%agent\agent.exe" (
    copy /y "%HERE%..\third_party\mtconnect-agent\bin\agent.exe" "%HERE%agent\" >nul 2>&1
    if errorlevel 1 echo [WARN] cannot copy agent.exe - put it in %HERE%agent manually
)


echo [5.5/5] Building webserver.exe ...
cl /nologo /O2 /EHsc /D _CRT_SECURE_NO_WARNINGS /I "%SQLITE%" "%HERE%src\webserver\webserver.cpp" "%HERE%bin\sqlite3.obj" /Fe:"%HERE%bin\webserver.exe" /link winhttp.lib ws2_32.lib
if errorlevel 1 ( echo [ERROR] webserver build failed & exit /b 1 )
echo.
echo ============================================================
echo  Build successful!
echo.
echo  %HERE%bin\fanuc_adapter.exe
echo  %HERE%bin\mazak_adapter.exe
echo  %HERE%bin\genconfig.exe
echo  %HERE%bin\shdr_sim.exe
echo  %HERE%bin\mazak_sim.exe
echo  %HERE%bin\mtc_stats.exe
echo ============================================================

REM --- remove stray .obj artifacts left in the project root ---
del /q "%HERE%*.obj" 2>nul

endlocal
