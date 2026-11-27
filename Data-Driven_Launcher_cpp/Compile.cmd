@echo off
rem Builds DDLaunchEdit.exe and DDLaunch.exe.
rem Run from a "x64 Native Tools Command Prompt for VS" (or after vcvars64.bat).
rem Each exe gets its manifest only from its .rc file (RT_MANIFEST resource 1),
rem so both link lines use /MANIFEST:NO (R11).
rem /utf-8: the .cpp files are UTF-8 without a byte-order mark (B29).
rem Version numbers are set in version.h only (R16).
setlocal
cd /d %~dp0

rem --- Check that every build input is present -------------------------------
set MISSING=
for %%F in (DDLaunch.cpp DDLaunch.rc DDLaunch.manifest DDLaunchEdit.cpp DDLaunchEdit.rc DDLaunchEdit.manifest DDCommon.h version.h resource.h DDLaunch.ico json.hpp) do (
    if not exist "%%F" set MISSING=1& echo Missing build file: %%F
)
if defined MISSING goto :fail

rem --- Resources --------------------------------------------------------------
rc /nologo DDLaunchEdit.rc
if errorlevel 1 goto :fail
rc /nologo DDLaunch.rc
if errorlevel 1 goto :fail

rem --- DDLaunch+ Editor -------------------------------------------------------
CL /nologo /utf-8 /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunchEdit.cpp DDLaunchEdit.res /Fe:DDLaunchEdit.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib comdlg32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib
if errorlevel 1 goto :fail

rem --- DDLaunch+ --------------------------------------------------------------
CL /nologo /utf-8 /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunch.cpp DDLaunch.res /Fe:DDLaunch.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib
if errorlevel 1 goto :fail

rem A DDLaunch.exe.manifest left over from earlier builds (made when the link
rem line lacked /MANIFEST:NO) is no longer needed.
if exist DDLaunch.exe.manifest del DDLaunch.exe.manifest

echo.
echo Build succeeded: DDLaunch.exe and DDLaunchEdit.exe
endlocal
exit /b 0

:fail
echo.
echo *** Build FAILED - see the messages above. ***
endlocal
exit /b 1
