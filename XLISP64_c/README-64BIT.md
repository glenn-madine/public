# XLISP 2.0.1: 64-bit Visual Studio port

## Build

**Command line.** Open the *x64 Native Tools Command Prompt for VS* and run:

    build.bat

This runs:

    cl /nologo /TC /O2 /W3 /D_CRT_SECURE_NO_WARNINGS xlisp.c xl1.c xl2.c xlcons.c /Fe:xlisp.exe /link /STACK:8388608

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

## Security and robustness fixes (second pass)

These fix every item in the "Risks found in the code" table of the code analysis. They also fix four bugs found while testing.

### Build options

| Define | Effect |
|---|---|
| `XL_ALLOW_PEEKPOKE` | Turns `PEEK` and `POKE` back on. They are off by default and signal an error. `ADDRESS-OF` is always available. |
| `XL_GCSTRESS=n` | Debug aid. Forces a full garbage collection on every n-th allocation, so a missing `xlsave` shows up at once in testing. `n = 1` is very slow. |
| `WKSDEBUG` | Prints which check rejected a workspace file. |

### Fixes

* **Fixed-size buffers.** Error messages, trace lines, the "; loading" line and the names in `#<Closure-…>` / `#<Subr-…>` are now written piece by piece instead of through `sprintf` into the 101-byte `buf`. Everything else that formats into a fixed buffer uses `snprintf`.
* **Long names.** `&key` keywords are built with `xlkeyword`, so a symbol name can be any length.
* **File names.** `LOAD`, `SAVE` and `RESTORE` take names up to 1024 characters (`FNAMEMAX`) through `xladdext`. A longer name fails cleanly (`NIL`) instead of overflowing the stack.
* **Number formats.** `*INTEGER-FORMAT*` and `*FLOAT-FORMAT*` are checked before use. A format may hold at most 64 characters of text and exactly one numeric conversion, with a width and precision of up to 2 digits each. The right length modifier is put in automatically. Anything else (`%s`, `%n`, `%*d`, two conversions) falls back to `%lld` / `%g`.
* **PEEK / POKE.** Off by default; see the build options.
* **Workspace files.** Files now start with a header: magic `XLWKS64`, a format version, the node, fixnum and float sizes, and the function-table size. `RESTORE` reads the whole file in a validation pass before freeing the current workspace. That pass checks every type code, size, function-table index and pointer, the fixnum and character tables, symbol print names and the symbol table. A damaged or foreign file makes `RESTORE` return `NIL` and leaves the current workspace untouched. In a fuzz test, 1,000 corrupted images gave 0 crashes. `.wks` files from earlier builds must be re-saved.
* **Integer overflow.** `+`, `-`, `*`, `/`, `REM`, `ABS`, `1+`, `1-`, unary minus and `GCD` now signal "integer overflow" instead of wrapping silently (signed overflow is undefined behaviour in C).
  * `TRUNCATE` rejects floats that don't fit in 64 bits.
  * A decimal integer too large for a fixnum is read as a float; an out-of-range `#x`/`#o`/`#b` number is a read error.
  * Fixnum arguments used as C `int`s are range-checked with `xlfixint`. This covers array and string indexes, sizes and `:start`/`:end`.
  * `DOTIMES` counts in 64 bits.
  * `(random 0)`, `(hash x 0)`, `(gcd 0 n)` and `(make-array -1)` no longer crash.
* **GC protection.** Running the test suite with `XL_GCSTRESS=1` under AddressSanitizer found and fixed the bugs in the next section; it now runs clean.

### Bugs found while testing the fixes

* `pusharg(x)` evaluated `x` after `xlsp` was already incremented. When `x` allocated (for example `pusharg(xleval(...))`) the collector could mark a garbage slot. The macro now evaluates `x` first.
* `RESTORE` ran the garbage collector while the argument stack still pointed into the freed image, a use-after-free on every restore. That was also true of the original code. The stack is now reset first.
* `FORMAT` with a `NIL` destination did not protect its string stream, so a collection during printing could free it.
* `*GC-FLAG*` and `*GC-HOOK*` were treated as set while still unbound during start-up. A hook that allocated could also call itself recursively without end.
* The symbol hash function used an undefined signed shift. It now uses `unsigned`, with the same results.

## Console command history (third pass)

* The console line editor keeps the last 100 lines typed (`HISTMAX`). **Up** and **Down** step through them, and stepping past the newest line restores the line being typed. **Escape** clears the line. Blank lines and immediate repeats are not stored.
* Arrow keys arrive from `_getch()` as a prefix byte (0 or 0xE0) plus a scan code. `xgetkeycode()` folds the pair into one key code. Other extended keys (Left, Right, F-keys…) are now ignored; before, the prefix byte reached the reader as a stray character.
* The editor code is in `xl1.c` (`ostgetc`, `echochar`, `rubout`, `setline`, `histadd`, `histmove`). Build with `XL_CONSOLE_TEST` to drive it from stdin with raw key bytes, for testing without a Windows console.

## Cursor movement in the line editor (fourth pass)

* **Left**/**Right** move the cursor within the line, **Home**/**End** jump to either end, and **Delete** removes the character under the cursor. Typing inserts at the cursor, Backspace deletes before it, and Enter accepts the whole line from any cursor position. This works on new lines and on lines recalled from the history.
* A typed tab is stored as spaces to the next tab stop, so every character in the edit buffer is one screen column wide.
* Moving left uses `oscurback()` in the new file **`xlcons.c`**, which calls `SetConsoleCursorPosition`, so editing still works when a long line wraps onto the next console row. It is a separate file because `<windows.h>` defines `CONTEXT` and `CHAR`, which clash with XLISP's names. On other systems it falls back to backspace characters.
* `build.bat` and `CMakeLists.txt` now compile `xlcons.c` too.
