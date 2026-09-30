# XLISP 2.0 - 64-bit build (Visual Studio: File > Open > Folder, pick x64 config)
cmake_minimum_required(VERSION 3.15)
project(xlisp LANGUAGES C)

add_executable(xlisp xlisp.c xl1.c xl2.c xlisp.h xldmem.h xlproto.h)

if(MSVC)
    target_compile_definitions(xlisp PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(xlisp PRIVATE /W3)
    # deep Lisp recursion runs on the C stack; the MSVC default is only 1 MB
    target_link_options(xlisp PRIVATE /STACK:8388608)
else()
    target_link_libraries(xlisp PRIVATE m)
endif()
