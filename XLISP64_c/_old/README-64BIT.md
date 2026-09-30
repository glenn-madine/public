# XLISP 2.0: 64-bit Visual Studio port

## Build

**Command line.** Open the *x64 Native Tools Command Prompt for VS* and run:

    build.bat

This runs:

    cl /nologo /TC /O2 /W3 /D_CRT_SECURE_NO_WARNINGS xlisp.c xl1.c xl2.c /Fe:xlisp.exe /link /STACK:8388608

**Visual Studio IDE.** Use *File > Open > Folder*, open this folder, and choose the `x64-Release` configuration. `CMakeLists.txt` is included. If you'd rather use a classic `.vcxproj`, add the three `.c` files to an empty x64 console project. Then set *Linker > System > Stack Reserve Size* to `8388608`, and make sure the files compile as C (`/TC`), not C++.

**Test.** `xlisp test.lsp`

## Files

| File | Contents |
|---|---|
| `xlisp.h` | Main header. Host config for 64-bit, includes, macros |
| `xldmem.h` | Original node/segment definitions, with minimal 64-bit edits (marked in the file) |
| `osdefs.h`, `osptrs.h` | Original OS-specific declarations and function-table entries (`osdefs.h` now uses ANSI prototypes) |
| `xlproto.h` | **New.** ANSI prototypes for every global function |
| `xlisp.c` | xlsym, xlsys, main |
| `xl1.c` | OS layer (msstuff), xlbfun, xlcont, xldbug, xldmem, xleval, xlfio, xlftab, xlglobals |
| `xl2.c` | xlimage, xlinit, xlio, xljump, xllist, xlmath, xlobj, xlpp, xlprint, xlread, xlstr, xlsubr |

## What changed and why

### 64-bit correctness (the important part)

* **Every function now has an ANSI prototype.** In K&R C, a call to an undeclared function assumes it returns `int`. On x64 that cuts every returned `LVAL` pointer to 32 bits, and the program crashes as soon as the heap sits above 4 GB. This was the main 64-bit bug.
* **All ~500 function definitions were converted from K&R to ANSI style.** Functions with no declared type that return nothing are now `void`.
* **`FIXTYPE` is now `long long`.** It was `long`, which is only 32 bits on Windows x64. Fixnums are now 64-bit, and `ADDRESS-OF`, `PEEK` and `POKE` can hold real pointers. `IFMT` is `%lld` and `ICNV` is `strtoll`.
* **Pointer/integer casts now go through `intptr_t`**, with no truncating `(long)` or `(int)` casts. `AFMT` is `%p`. The image save/restore code no longer casts `NIL` to and from `OFFTYPE`.
* **Silent `long long`/`size_t` to `int` narrowings now have explicit casts.** MSVC reports these as C4244 and C4267.

### Visual C / modern C compatibility

* The DOS `bdos()` console calls were replaced with `<conio.h>` (`_getch`, `_putch`, `_kbhit`). Ctrl-C is handled by a `SIGINT` handler.
* When stdin or stdout is redirected, the interpreter uses normal stdio, so `xlisp < script.lsp > out.txt` works.
* `SYSTEM` uses `%COMSPEC%` instead of `COMMAND`.
* The global `true` was renamed to `s_true` (`true` is a keyword in C23 and a macro in `<stdbool.h>`).
* In `xldmem.h`, the closure macros `getenv`/`setenv` were renamed `getcenv`/`setcenv` so they don't collide with the CRT's `getenv()`. The subr function pointer got a prototype, and the old `extern LVAL cons();`-style declarations moved to `xlproto.h`.
* Removed the hand-written `extern char *malloc()`, `extern int errno`, `FORWARD` and `extern ... ()` declarations. The standard headers and prototypes replace them.
* Functions that never return (`xlfail`, `xlerror`, `xljump`, …) are marked `__declspec(noreturn)`, so there are no false "not all control paths return a value" warnings.

### Bugs in the original source that the prototypes exposed, now fixed

* `xlerror("zero length name")` was called with one argument instead of two. It is now `xlfail`.
* `ppterpri(ppfile)` was called with an extra argument.
* `xinfo()` declared `nfree` as `int`, but it is a `long`.
* The garbage collector didn't mark the contents of unnamed streams (`USTREAM`). String output streams could lose their data after a GC.
* Integer `<`, `>` and `=` compared by subtracting, which overflows with large fixnums. They now compare directly.

### Tuning

* `EDEPTH`/`ADEPTH` were raised to 10000, and the link uses an 8 MB stack. Runaway recursion stops cleanly with "stack overflow" instead of crashing.
* Saved workspace (`.wks`) files are tied to the build that wrote them. Old 16-bit DOS images can't be loaded.
