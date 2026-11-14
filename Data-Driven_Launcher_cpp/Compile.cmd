@echo off
cd /d %~dp0
rc /nologo DDLaunchEdit.rc
rc /nologo DDLaunch.rc
CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunchEdit.cpp DDLaunchEdit.res /Fe:DDLaunchEdit.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib comdlg32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib
CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunch.cpp DDLaunch.res /Fe:DDLaunch.exe /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib 
