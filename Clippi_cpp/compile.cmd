rc ClipBoardButtons.rc
cl /nologo /EHsc /std:c++17 /O2 /W4 /DUNICODE /D_UNICODE ClipBoardButtons.cpp ClipBoardButtons.res /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib advapi32.lib user32.lib gdi32.lib
rc ClippiEditor.rc
cl /nologo /EHsc /std:c++17 /O2 /W4 /DUNICODE /D_UNICODE ClippiEditor.cpp ClippiEditor.res /link /SUBSYSTEM:WINDOWS comctl32.lib comdlg32.lib shell32.lib ole32.lib windowscodecs.lib uuid.lib user32.lib gdi32.lib