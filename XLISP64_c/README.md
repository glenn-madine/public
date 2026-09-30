# XLISP 2.0.1 — 64-bit Windows Edition

XLISP 2.0.1 is a small interpreter for a subset of Common Lisp with a built-in object system, written by David Michael Betz (1985–88). This edition ports it to **64-bit Windows with Microsoft Visual C**. It also fixes the security and robustness problems found in a review of the original code.

It still builds with gcc and clang on 64-bit Linux and macOS.

```
XLISP version 2.0.1 64-bit, Copyright (c) 1988, by David Betz
> (defun fact (n) (if (< n 2) 1 (* n (fact (1- n)))))
FACT
> (fact 20)
2432902008176640000
```

---

## Contents

- [Building](#building)
- [Running](#running)
- [Source files](#source-files)
- [How the interpreter works](#how-the-interpreter-works)
- [What this edition changes](#what-this-edition-changes)
- [Build options](#build-options)
- [Testing](#testing)
- [Known limits](#known-limits)
- [License](#license)

---

## Building

### Visual Studio, command line

Open the **x64 Native Tools Command Prompt for VS**, go to the source folder and run:

```bat
build.bat
```

This runs:

```bat
cl /nologo /TC /O2 /W3 /D_CRT_SECURE_NO_WARNINGS xlisp.c xl1.c xl2.c xlcons.c /Fe:xlisp.exe /link /STACK:8388608
```

### Visual Studio, IDE

Use **File › Open › Folder**, pick this folder and choose the `x64-Release` configuration. `CMakeLists.txt` sets the same options as `build.bat`.

To use a classic `.vcxproj` instead:

1. Create an empty x64 console project and add `xlisp.c`, `xl1.c`, `xl2.c` and `xlcons.c`.
2. Set **C/C++ › Advanced › Compile As** to C (`/TC`).
3. Set **Linker › System › Stack Reserve Size** to `8388608`.

### gcc / clang (Linux, macOS)

```sh
cc -std=c11 -O2 -o xlisp xlisp.c xl1.c xl2.c xlcons.c -lm
```

### Requirements

- Visual Studio 2015 or later (the code uses `snprintf`, `strtoll` and `<stdint.h>`), or any C11 compiler.
- A 64-bit target is the goal. The code also compiles for 32-bit, where it relies on no size-specific tricks.

---

## Running

```
xlisp [-v] [-tFILE] [file.lsp ...]
```

| Option | Meaning |
|---|---|
| `file.lsp …` | Load these files after `init.lsp`. The `.lsp` extension is optional. |
| `-v` | Print each value while loading files. |
| `-tFILE` | Write a transcript of the session to `FILE`. |

At start-up the interpreter does the following:

1. It loads the workspace `xlisp.wks` if one exists. Otherwise it builds a fresh one.
2. It loads `init.lsp` from the current folder, if present.
3. It loads any files named on the command line.
4. It starts the `>` prompt.

Input can be piped in, for example `xlisp < script.lsp > out.txt`. The console line editor is then bypassed.

### Keys at the console

| Key | Action |
|---|---|
| Ctrl-C | Abort to the top level |
| Ctrl-G | Leave the current break loop |
| Ctrl-P | Continue from a continuable error |
| Ctrl-B | Enter a break loop |
| Ctrl-T | Show memory statistics |
| Ctrl-Z | End of input (exit) |
| Left / Right arrow | Move the cursor one character |
| Home / End | Move the cursor to the start / end of the line |
| Backspace | Delete the character before the cursor |
| Delete | Delete the character under the cursor |
| Escape | Clear the current line |
| Up / Down arrow | Recall previous / next lines from the command history |

### Command history

The console remembers the last 100 lines you typed. Press **Up** to step back through them and **Down** to step forward. Stepping down past the newest line brings back the line you were typing. A recalled line can be edited anywhere: move with **Left**/**Right** (or **Home**/**End**), type to insert at the cursor, and delete with Backspace or Delete. **Enter** runs the whole line wherever the cursor is. Blank lines and a line identical to the one before it are not stored. The history lasts for the session only; it is not saved to disk.

### Useful variables and functions

| Name | Purpose |
|---|---|
| `*BREAKENABLE*` | When non-`NIL`, errors open a break loop (`1>` prompt) instead of returning to top level |
| `*TRACENABLE*`, `*TRACELIMIT*` | Print a backtrace on errors, limited to *n* frames |
| `(trace f)`, `(untrace f)` | Trace calls to a function |
| `*GC-FLAG*`, `*GC-HOOK*` | Report garbage collections, or call a function after each one |
| `*INTEGER-FORMAT*`, `*FLOAT-FORMAT*` | `printf` format used to print numbers (checked; see below) |
| `(room)`, `(gc)`, `(expand n)`, `(alloc n)` | Inspect and tune the memory manager |
| `(save "file")`, `(restore "file")` | Save and reload the whole workspace |
| `(system "cmd")` | Run a command through `%COMSPEC%` (or `$SHELL`) |
| `(exit)` | Leave the interpreter |

---

## Source files

| File | Original modules | Contents |
|---|---|---|
| `xlisp.h` | — | Configuration (64-bit types, limits, stack sizes), argument macros, type predicates, `CONTEXT` record |
| `xldmem.h` | — | The node structure, type codes and accessor macros |
| `xlproto.h` | — | ANSI prototypes for every global function |
| `osdefs.h`, `osptrs.h` | — | Declarations and function-table entries for `SYSTEM` and `GET-KEY` |
| `xlcons.c` | — | Windows console cursor positioning for the line editor (kept separate because `<windows.h>` clashes with XLISP names) |
| `xlisp.c` | xlsym, xlsys, xlisp | Symbol table, property lists, system functions, `main` and the REPL |
| `xl1.c` | msstuff, xlbfun, xlcont, xldbug, xldmem, xleval, xlfio, xlftab, xlglobals | OS/console layer, basic built-ins, special forms, error handling, memory manager and GC, evaluator, file I/O, function table, globals |
| `xl2.c` | xlimage, xlinit, xlio, xljump, xllist, xlmath, xlobj, xlpp, xlprint, xlread, xlstr, xlsubr | Workspace save/restore, start-up, character I/O, non-local exits, list, math and string functions, objects, printer, pretty-printer, reader, argument helpers |
| `XLISP.DOC` | — | The XLISP 2.0 language manual, updated for this edition |
| `build.bat`, `CMakeLists.txt` | — | Build scripts |
| `test.lsp`, `test-features.lsp`, `test-hardening.lsp` | — | Test scripts |

Each `.c` file is a concatenation of several original modules. Each module still starts with its own comment header, so the originals are easy to find.

---

## How the interpreter works

### Data

Every Lisp value is an `LVAL`, a pointer to a 24-byte `struct node`. A node holds a type code, a GC flag byte and a two-word union. `NIL` is the null pointer. There are 14 node types:

- `CONS`
- `SYMBOL`
- `FIXNUM` (64-bit)
- `FLONUM` (`double`)
- `STRING`
- `CHAR`
- `VECTOR`
- `OBJECT`
- `CLOSURE`
- `SUBR` and `FSUBR` (built-ins)
- `STREAM` (file)
- `USTREAM` (string stream)
- `FREE`

Symbols, objects, closures and vectors share one layout: a size plus a pointer to an array of slots.

### Memory

Nodes are allocated from segments of 2,000 nodes. Two fixed segments hold the fixnums −128…255 and all 256 characters, so those values never allocate.

Collection is stop-the-world mark and sweep. Lists are marked with Deutsch–Schorr–Waite pointer reversal, which uses no C stack.

C code must register every `LVAL` local it keeps across an allocation, using `xlsave`/`xlprotect`. These registrations sit on an evaluation stack of 10,000 slots that the collector scans.

### Evaluation

`xleval` handles values by type:

- A symbol evaluates to its value.
- A cons goes to `evform`.
- Anything else evaluates to itself.

`evform` then dispatches on the function in the head of the form:

- **SUBR:** arguments are evaluated onto the argument stack, then the C function is called.
- **FSUBR** (special forms such as `IF` and `LET`): the argument forms are passed unevaluated.
- **Closure:** the lambda list, already parsed when the closure was made, is bound in a new frame of the closure's saved environment.
- **Macro:** the expander runs on the unevaluated forms, and the result is evaluated in their place.

Scope is lexical. `PROGV` provides dynamic binding.

### Non-local exits

`BLOCK`/`RETURN`, `CATCH`/`THROW`, `TAGBODY`/`GO`, `UNWIND-PROTECT`, `ERRSET` and every error use one mechanism. A chain of `CONTEXT` records lives on the C stack, each with a `jmp_buf`. `xljump` restores the saved interpreter state, then `longjmp`s to the target.

### Reader and printer

The reader is driven by `*READTABLE*`, a 256-entry vector. It maps each character to white space, constituent, escape, or a read-macro function, so Lisp code can add read macros.

The printer honours `*PRINT-CASE*` and the two number-format variables.

### Objects

`OBJECT` and `CLASS` are built in.

- `(send obj :msg args…)` looks up the message in the object's class, then its superclasses.
- Methods see instance variables as ordinary variables.
- `:new` automatically sends `:isnew` to the new object.
- `send-super` starts the lookup at the superclass.

### Workspaces

`SAVE` writes a position-independent image: every pointer is stored as a node offset, and built-ins are stored by function-table index.

`RESTORE` validates the whole file before it replaces the running workspace (see below).

---

## What this edition changes

### 1. 64-bit Visual C port

- **Prototypes.** Every function has an ANSI prototype, and all ~525 K&R definitions were converted. Previously, undeclared functions were assumed to return `int`, which cut 64-bit pointers in half.
- **64-bit fixnums.** `FIXTYPE` is `long long`, because `long` is only 32 bits on Windows x64. So `ADDRESS-OF` and large integers work.
- **Pointer casts.** Pointer/integer conversions go through `intptr_t`, and addresses print with `%p`.
- **Console.** DOS `bdos()` calls were replaced with `<conio.h>` (`_getch`, `_putch`, `_kbhit`). Redirected stdio is supported, and Ctrl-C is handled by a `SIGINT` handler.
- **Name clashes.** `true` became `s_true`, and the closure macros `getenv`/`setenv` became `getcenv`/`setcenv`.
- **Line editing and history.** The console line editor supports Left/Right, Home/End, Backspace and Delete anywhere in the line. It recalls earlier lines with the Up and Down arrows and clears the line with Escape.
- **Warnings.** Functions that never return are marked `__declspec(noreturn)`. The code builds clean at `/W3`.

### 2. Security and robustness fixes

| Problem in the original | Fix |
|---|---|
| Error messages, trace lines and closure names were `sprintf`'d into a 101-byte buffer, which overflowed on long text | Long text is written piece by piece; everything else uses `snprintf` |
| `&key` names and file names were `strcpy`'d into fixed stack buffers | `xlkeyword` handles names of any length; file names up to 1,024 characters (`xladdext`) |
| `*INTEGER-FORMAT*` / `*FLOAT-FORMAT*` went straight to `sprintf`, so `"%s"` crashed it | `safefmt` accepts exactly one numeric conversion (width and precision ≤ 2 digits) and inserts the correct length modifier; anything else falls back to `%lld` / `%g` |
| `PEEK` / `POKE` read and wrote any address | Off unless built with `XL_ALLOW_PEEKPOKE` |
| A corrupt `.wks` file crashed `RESTORE` | New header (magic `XLWKS64`, version, type sizes, function-table size) plus a full validation pass before the current workspace is freed; a bad file makes `RESTORE` return `NIL` |
| Integer overflow wrapped silently (undefined behaviour in C) | Checked arithmetic raises `integer overflow`; `TRUNCATE` range-checked; oversized decimal integers read as floats |
| `(random 0)`, `(gcd 0 5)` and `(hash x 0)` divided by zero | Arguments checked |
| Fixnums were truncated to C `int` for indexes and sizes | Range-checked by `xlfixint`; `DOTIMES` counts in 64 bits |

### 3. Bugs found and fixed along the way

- **`USTREAM` marking:** the GC did not mark unnamed-stream contents, so string output streams could lose data.
- **`pusharg(x)` ordering:** it moved the stack pointer before evaluating `x`, so a GC inside `x` could scan a garbage slot.
- **`RESTORE` use-after-free:** it ran the GC while the argument stack still pointed into the freed image. This was a use-after-free on every restore, present in the original too.
- **`FORMAT` protection:** `FORMAT` to a string did not protect its stream during printing.
- **GC variables at start-up:** `*GC-FLAG*` and `*GC-HOOK*` were treated as set while still unbound, and the hook could re-enter itself.
- **Wrong argument counts:** `xlerror` and `ppterpri` were called with the wrong number of arguments.
- **Mismatched types:** `nfree` was declared `int` in one module and `long` in another.
- **Comparison overflow:** integer comparisons used subtraction, which overflowed.
- **Hash shift:** the symbol hash used a signed shift that could overflow (undefined behaviour).

### 4. Compatibility notes

- Workspace files from earlier builds, including the original DOS ones, must be re-created. Load your sources and `(save …)` again.
- `PEEK`/`POKE` now signal an error by default.
- Arithmetic that used to wrap now signals `integer overflow`.

---

## Build options

Add these with `/D` (MSVC) or `-D` (gcc/clang).

| Define | Effect |
|---|---|
| `XL_ALLOW_PEEKPOKE` | Re-enables `PEEK` and `POKE` |
| `XL_GCSTRESS=n` | Debug aid: force a full collection on every *n*-th allocation, to expose missing `xlsave` calls. `n = 1` is very slow |
| `WKSDEBUG` | Print which check rejected a workspace file |
| `XL_CONSOLE_TEST` | Test aid: drive the console line editor (cursor keys, history, editing) from stdin with raw key bytes, on any platform |
| `EDEPTH=n`, `ADEPTH=n` | Evaluation and argument stack sizes (default 10,000 each) |

---

## Testing

Run the three test scripts. Each ends with `(exit)` or the end of the file.

```
xlisp test.lsp
xlisp test-features.lsp
xlisp test-hardening.lsp
```

| Script | What it covers |
|---|---|
| `test.lsp` | Smoke test: arithmetic, 64-bit integers, lists, GC under load, catch/throw, unwind-protect, strings, format, objects, pretty-printer |
| `test-features.lsp` | Around 90 checks across strings, streams, macros, `&optional`/`&rest`/`&key`/`&aux`, `setf` places, sorting and mapping, control flow, the object system with `send-super`, tracing and math |
| `test-hardening.lsp` | The fixed problems: 400-character error messages and symbol names, long file names, hostile number formats, `PEEK`/`POKE`, overflow at the fixnum limits, out-of-range indexes and sizes |

This edition was verified as follows:

- **Windows:** a real 64-bit Windows executable (MinGW-w64) was run under Wine.
- **Linux:** built with AddressSanitizer and UndefinedBehaviorSanitizer; all scripts run clean, also with `XL_GCSTRESS=1`.
- **Fuzzing:** 1,000 randomly corrupted workspace images caused 0 crashes.

The code has not yet been compiled with Visual Studio itself. Please report any MSVC warnings or errors.

---

## Known limits

- **Recursion depth:** there is no tail-call elimination. Deep recursion stops at the argument-stack limit with a clean `stack overflow` error.
- **Macro expansion:** macros are expanded every time a form is evaluated; there is no caching.
- **Missing Common Lisp features:** no bignums, `DEFSTRUCT`, packages, multiple values or hash tables. `FORMAT` supports `~A`, `~S`, `~%` and `~~` only.
- **Workspace checks:** validation makes loading a damaged workspace memory-safe. It cannot tell whether every closure or object in a well-formed file makes sense.
- **GC protection:** C extensions must still register `LVAL` locals with `xlsave`. Build with `XL_GCSTRESS` to test new code.

---

## License

Original code © 1985–1988 David Michael Betz, all rights reserved:

> Permission is granted for unrestricted non-commercial use.

The 64-bit port and fixes are provided under the same terms.
