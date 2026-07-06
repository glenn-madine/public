@echo off
cd /d %~dp0
START "Compiling" /WAIT CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 RDP_Plus.cpp RDP_Plus.res /Fe:RDP_Plus.exe /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib 
