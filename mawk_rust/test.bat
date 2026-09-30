@echo off
rem ==========================================================================
rem  test.bat -- regression tests for mawk_rs on Windows (cmd.exe)
rem
rem  Usage:  test.bat [path\to\mawk.exe]
rem          (default: target\release\mawk.exe next to this script)
rem
rem  Each case lives in tests\ as NNN_name.awk plus:
rem    .out   expected stdout+stderr, byte for byte     (required)
rem    .in    stdin                                     (optional, else NUL)
rem    .rc    expected exit code                        (optional, else 0)
rem    .args  options placed before -f, e.g. -F,        (optional, one line)
rem    .post  operands placed after the program file    (optional, one line)
rem  Programs are passed with -f so cmd.exe never has to quote AWK code.
rem ==========================================================================
setlocal EnableExtensions DisableDelayedExpansion

set "AWK=%~1"
if "%AWK%"=="" set "AWK=%~dp0target\release\mawk.exe"
if not exist "%AWK%" if "%~1"=="" set "AWK=%~dp0target\release\mawk_rs.exe"
if not exist "%AWK%" (
    echo mawk binary not found: "%AWK%"
    echo usage: %~nx0 [path\to\mawk.exe]
    exit /b 2
)
for %%A in ("%AWK%") do set "AWK=%%~fA"

set "TDIR=%~dp0tests"
if not exist "%TDIR%\*.awk" (
    echo test cases not found in "%TDIR%"
    exit /b 2
)

set "WORK=%TEMP%\mawk_rs_tests_%RANDOM%%RANDOM%"
mkdir "%WORK%" || exit /b 2
set /a PASS=0, FAIL=0

echo Testing "%AWK%"
pushd "%WORK%"
for %%F in ("%TDIR%\*.awk") do call :run "%%~nF"
call :closed_pipe
popd
rmdir /s /q "%WORK%" 2>nul

echo.
echo passed: %PASS%  failed: %FAIL%
if %FAIL% gtr 0 exit /b 1
exit /b 0

rem --------------------------------------------------------------------------
:run
set "NAME=%~1"
set "PRE="
set "POST="
set "WANTRC=0"
set "IN=nul"
if exist "%TDIR%\%NAME%.args" set /p PRE=<"%TDIR%\%NAME%.args"
if exist "%TDIR%\%NAME%.post" set /p POST=<"%TDIR%\%NAME%.post"
if exist "%TDIR%\%NAME%.rc"   set /p WANTRC=<"%TDIR%\%NAME%.rc"
if exist "%TDIR%\%NAME%.in"   set "IN=%TDIR%\%NAME%.in"

"%AWK%" %PRE% -f "%TDIR%\%NAME%.awk" %POST% < "%IN%" > "%WORK%\got.txt" 2>&1
set "RC=%ERRORLEVEL%"

fc /b "%WORK%\got.txt" "%TDIR%\%NAME%.out" > nul 2>&1
if errorlevel 1 goto :run_fail
if not "%RC%"=="%WANTRC%" goto :run_fail
set /a PASS+=1
exit /b 0

:run_fail
set /a FAIL+=1
echo.
echo FAIL %NAME%   exit code %RC%, expected %WANTRC%
echo   ---- expected ----
type "%TDIR%\%NAME%.out"
echo   ---- got ---------
type "%WORK%\got.txt"
exit /b 0

rem --------------------------------------------------------------------------
rem  Writing to a pipe whose reader has gone away must stop mawk instead of
rem  looping forever. `set /p` reads one line and exits, closing the pipe.
:closed_pipe
> "%WORK%\yes.awk" echo BEGIN { while (1) print "y" }
echo closed-pipe test... if this hangs, press Ctrl+C: mawk ignores a closed output pipe
"%AWK%" -f "%WORK%\yes.awk" | set /p LINE=
set /a PASS+=1
exit /b 0
