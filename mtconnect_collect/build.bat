@echo off
REM ============================================================
REM  mtconnect_collect - build all collectors + tools
REM
REM  架构：仅 fanuc_adapter.exe 为 x86（Fwlib32.lib 是 32 位），
REM        其余全部为 x64。
REM
REM  Builds:
REM    bin\fanuc_adapter.exe   FANUC collector (FOCAS2 -> SHDR)  [x86]
REM    bin\mazak_adapter.exe   MAZAK collector (MTConnect pull -> SHDR) [x64]
REM    bin\genconfig.exe       generate agent config from jichuang.txt
REM    bin\shdr_sim.exe        offline SHDR simulator
REM    bin\mazak_sim.exe       offline MAZAK (pull-mode) simulator
REM    bin\cnc_sim.exe         controllable realistic CNC simulator (SHDR + HTTP control)
REM    bin\cnc_sim_ctl.exe     command line control tool for cnc_sim.exe
REM    bin\mtc_stats.exe       sampling + report tool (SQLite / MySQL-MariaDB)
REM    bin\webserver.exe       stats web server
REM    bin\mtc_ctl.exe         service console
REM
REM  数据库：config.json 中 db.type=sqlite|mysql|postgres，由程序按配置选择。
REM    MySQL/MariaDB 客户端：设置环境变量 MTC_MYSQL_ROOT 指向
REM    含 include\mysql.h 与 lib\libmariadb.lib（或 libmysql.lib）的目录，
REM    即可编译真正的 MySQL 后端；未设置时编译为运行时提示不可用的 stub。
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
    if exist "!VCVARS!" goto :setup_x86
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

:setup_x86
echo [1/6] Setting up MSVC x86 (fanuc_adapter only) ...
call "!VCVARS!" x86 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to setup MSVC x86 environment
    exit /b 1
)

if not exist "%HERE%bin" mkdir "%HERE%bin"

set "SRC=%HERE%src"
set "COMMON=%SRC%\adapter.cpp %SRC%\client.cpp %SRC%\condition_list.cpp %SRC%\device_datum.cpp %SRC%\logger.cpp %SRC%\server.cpp %SRC%\service.cpp %SRC%\string_buffer.cpp %SRC%\minIni.c %SRC%\config.cpp"

echo [2/6] Building fanuc_adapter.exe (x86) ...
cl /nologo /O2 /EHsc /utf-8 /D WIN32 /D _CRT_SECURE_NO_WARNINGS ^
    /I "%SRC%" /I "%SRC%\fanuc" /I "%FW%" ^
    %COMMON% %SRC%\fanuc\FanucAdapter.cpp %SRC%\fanuc\fanuc_adapter.cpp ^
    /Fe:"%HERE%bin\fanuc_adapter.exe" ^
    /link "%FW%\Fwlib32.lib" wsock32.lib
if errorlevel 1 (
    echo [ERROR] fanuc_adapter build failed
    exit /b 1
)

echo [3/6] Switching to MSVC x64 ...
call "!VCVARS!" x64 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to setup MSVC x64 environment
    exit /b 1
)

REM 旧的 32 位 sqlite3.obj 会与 x64 链接冲突，强制重建
if exist "%HERE%bin\sqlite3.obj" del /q "%HERE%bin\sqlite3.obj"

echo [4/6] Building mazak_adapter.exe + tools (x64) ...
cl /nologo /O2 /EHsc /utf-8 /D WIN32 /D _CRT_SECURE_NO_WARNINGS ^
    /I "%SRC%" /I "%SRC%\mazak" ^
    %COMMON% %SRC%\mazak\mazak_adapter.cpp ^
    /Fe:"%HERE%bin\mazak_adapter.exe" ^
    /link wsock32.lib
if errorlevel 1 ( echo [ERROR] mazak_adapter build failed & exit /b 1 )

cl /nologo /O2 /utf-8 /D _CRT_SECURE_NO_WARNINGS "%HERE%tools\genconfig.c" /Fe:"%HERE%bin\genconfig.exe"
if errorlevel 1 ( echo [ERROR] genconfig build failed & exit /b 1 )
cl /nologo /O2 /utf-8 /D _CRT_SECURE_NO_WARNINGS "%HERE%tools\shdr_sim.c" /Fe:"%HERE%bin\shdr_sim.exe" /link wsock32.lib
if errorlevel 1 ( echo [ERROR] shdr_sim build failed & exit /b 1 )
cl /nologo /O2 /utf-8 /D _CRT_SECURE_NO_WARNINGS "%HERE%tools\mazak_sim.c" /Fe:"%HERE%bin\mazak_sim.exe" /link wsock32.lib
if errorlevel 1 ( echo [ERROR] mazak_sim build failed & exit /b 1 )
cl /nologo /O2 /utf-8 /D _CRT_SECURE_NO_WARNINGS "%HERE%tools\cnc_sim.c" /Fe:"%HERE%bin\cnc_sim.exe" /link wsock32.lib
if errorlevel 1 ( echo [ERROR] cnc_sim build failed & exit /b 1 )
cl /nologo /O2 /utf-8 /D _CRT_SECURE_NO_WARNINGS "%HERE%tools\cnc_sim_ctl.c" /Fe:"%HERE%bin\cnc_sim_ctl.exe" /link winhttp.lib
if errorlevel 1 ( echo [ERROR] cnc_sim_ctl build failed & exit /b 1 )

echo [4.5/6] Building sqlite3.obj (x64) ...
cl /nologo /O2 /c /D SQLITE_THREADSAFE=0 /D _CRT_SECURE_NO_WARNINGS "%SQLITE%\sqlite3.c" /Fo:"%HERE%bin\sqlite3.obj"
if errorlevel 1 ( echo [ERROR] sqlite3 build failed & exit /b 1 )

REM --- optional MySQL / MariaDB client ---
set "MYSQL_CFLAGS="
set "MYSQL_LIBS="
if defined MTC_MYSQL_ROOT (
    if exist "%MTC_MYSQL_ROOT%\include\mysql.h" (
        set "MYSQL_CFLAGS=/DMTC_HAVE_MYSQL /I"%MTC_MYSQL_ROOT%\include""
        for %%L in ("%MTC_MYSQL_ROOT%\lib\libmariadb.lib" "%MTC_MYSQL_ROOT%\lib\mariadb\libmariadb.lib" "%MTC_MYSQL_ROOT%\lib\libmysql.lib" "%MTC_MYSQL_ROOT%\lib\mysql\libmysql.lib") do (
            if exist "%%~L" set "MYSQL_LIBS=%%~L"
        )
        if defined MYSQL_LIBS (
            echo [INFO] MySQL/MariaDB client found: !MYSQL_LIBS!
        ) else (
            echo [WARN] MTC_MYSQL_ROOT set but no client lib found - MySQL backend stub
        )
    ) else (
        echo [WARN] MTC_MYSQL_ROOT set but include\mysql.h not found - MySQL backend stub
    )
) else (
    echo [INFO] MTC_MYSQL_ROOT not set - MySQL backend will report unavailable at runtime
)

echo [5/6] Building mtc_stats.exe (x64) ...
cl /nologo /O2 /EHsc /utf-8 /D _CRT_SECURE_NO_WARNINGS /I "%SQLITE%" /I "%HERE%src" ^
    "%HERE%tools\mtc_stats.cpp" "%HERE%src\config.cpp" ^
    "%HERE%src\db\db.cpp" "%HERE%src\db\stats_db.cpp" "%HERE%src\db\db_mysql.cpp" ^
    "%HERE%bin\sqlite3.obj" !MYSQL_CFLAGS! /Fe:"%HERE%bin\mtc_stats.exe" ^
    /link winhttp.lib !MYSQL_LIBS!
if errorlevel 1 ( echo [ERROR] mtc_stats build failed & exit /b 1 )

REM --- copy runtime files (gitignored binaries) ---
if not exist "%HERE%bin\Fwlib32.dll" (
    copy /y "%FW%\Fwlib32.dll" "%HERE%bin\" >nul
)
if not exist "%HERE%agent\agent.exe" (
    copy /y "%HERE%..\third_party\mtconnect-agent\bin\agent.exe" "%HERE%agent\" >nul 2>&1
    if errorlevel 1 echo [WARN] cannot copy agent.exe - put it in %HERE%agent manually
)

echo [5.5/6] Building webserver.exe (x64) ...
cl /nologo /O2 /EHsc /utf-8 /D _CRT_SECURE_NO_WARNINGS /I "%SQLITE%" /I "%HERE%src" ^
    "%HERE%src\webserver\webserver.cpp" "%HERE%src\config.cpp" ^
    "%HERE%src\db\db.cpp" "%HERE%src\db\stats_db.cpp" "%HERE%src\db\db_mysql.cpp" ^
    "%HERE%bin\sqlite3.obj" !MYSQL_CFLAGS! /Fe:"%HERE%bin\webserver.exe" ^
    /link winhttp.lib ws2_32.lib !MYSQL_LIBS!
if errorlevel 1 ( echo [ERROR] webserver build failed & exit /b 1 )

echo [5.6/6] Building mtc_ctl.exe (x64) ...
cl /nologo /O2 /EHsc /utf-8 /D _CRT_SECURE_NO_WARNINGS /I "%HERE%src" ^
    "%HERE%tools\mtc_ctl.cpp" "%HERE%src\config.cpp" /Fe:"%HERE%bin\mtc_ctl.exe" /link winhttp.lib
if errorlevel 1 ( echo [ERROR] mtc_ctl build failed & exit /b 1 )

echo.
echo ============================================================
echo  Build successful!
echo    fanuc_adapter.exe [x86]  - others [x64]
echo.
echo  %HERE%bin\fanuc_adapter.exe
echo  %HERE%bin\mazak_adapter.exe
echo  %HERE%bin\genconfig.exe
echo  %HERE%bin\shdr_sim.exe
echo  %HERE%bin\mazak_sim.exe
echo  %HERE%bin\cnc_sim.exe
echo  %HERE%bin\cnc_sim_ctl.exe
echo  %HERE%bin\mtc_stats.exe
echo  %HERE%bin\webserver.exe
echo  %HERE%bin\mtc_ctl.exe
echo ============================================================

REM --- remove stray .obj artifacts left in the project root ---
del /q "%HERE%*.obj" 2>nul

endlocal
