@echo off
rem ---------------------------------------------------------------
rem  Build XLISP 2.0 as a 64-bit Windows console program with MSVC.
rem  Run this from an "x64 Native Tools Command Prompt for VS".
rem ---------------------------------------------------------------
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    echo WARNING: this prompt does not target x64 ^(VSCMD_ARG_TGT_ARCH=%VSCMD_ARG_TGT_ARCH%^).
    echo          Open the "x64 Native Tools Command Prompt for VS" for a 64-bit build.
)
cl /nologo /TC /O2 /W3 /D_CRT_SECURE_NO_WARNINGS xlisp.c xl1.c xl2.c /Fe:xlisp.exe /link /STACK:8388608
