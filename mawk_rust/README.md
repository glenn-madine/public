# mawk_rs

An AWK interpreter written in Rust, ported from the C program `mawk`.
It runs standard POSIX AWK programs — patterns and actions, `BEGIN`/`END`,
user functions, associative arrays, `printf`, every form of `getline`, and
output redirection to files and pipes — with no dependencies outside the
Rust standard library.

**Version 1.0.1** (released 9/27/2026)

```sh
$ printf 'alice 30\nbob 25\ncarol 35\n' | mawk_rs '$2 > 28 { print $1 }'
alice
carol
```

---

## Contents

- [Building](#building)
- [Usage](#usage)
- [Language support](#language-support)
- [Differences from mawk](#differences-from-mawk)
- [Known limitations](#known-limitations)
- [How it works](#how-it-works)
- [Source layout](#source-layout)
- [Testing](#testing)
- [Windows notes](#windows-notes)
- [Exit status](#exit-status)

---

## Building

Requires **Rust 1.82 or newer** (edition 2024, and `unsafe extern` blocks).

```sh
cargo build --release
```

The binary is written to `target/release/`, named after the package in
`Cargo.toml`. The examples in this file call it `mawk_rs`.

There are no crate dependencies.

## Usage

```text
mawk_rs [-F fs] [-v var=value] [--] 'program text' [file | var=value ...]
mawk_rs [-F fs] [-v var=value] -f progfile [-f progfile ...] [--] [file | var=value ...]
```

| Option | Meaning |
|---|---|
| `-F fs` | Field separator. `-Ft` means tab. Escapes such as `\t` are processed. |
| `-v var=value` | Assign a variable before `BEGIN` runs. The value is a *numeric string* (compares as a number if it looks like one) and escapes are processed. |
| `-f progfile` | Read the program from a file. Repeatable; files are joined in order. `-f -` reads the program from standard input. |
| `--` | End of options. |
| `-W version`, `-Wv`, `--version`, `-V` | Print version and compiled limits, then exit 0. |
| `-W usage`, `--help`, `-h` | Print usage, then exit 0. |
| `-W anything-else` | Ignored with a warning (`vacuous option`), as in mawk. |

Anything after the program text is an operand: an input file, `-` for
standard input, or a `var=value` assignment that takes effect when the reader
reaches it. With no input files, standard input is read.

Program files are expected to be UTF-8. A file that is not valid UTF-8 (for
example a Windows-1252 script) is read as Latin-1 rather than rejected.

### Examples

```sh
# Sum a column
mawk_rs '{ s += $3 } END { print s }' data.txt

# CSV-ish input, custom output separator
mawk_rs -F, -v OFS='\t' '{ $1 = $1; print }' data.csv

# Count words, most frequent first
mawk_rs '{ for (i = 1; i <= NF; i++) n[tolower($i)]++ }
         END { for (w in n) print n[w], w | "sort -rn | head" }' book.txt

# Read a second file with getline
mawk_rs 'BEGIN { while ((getline line < "hosts.txt") > 0) seen[line] = 1 }
         !($1 in seen)' access.log
```

---

## Language support

### Program structure

- `BEGIN { }` and `END { }`; any number of each, run in order.
- `pattern { action }`, `pattern` alone (prints `$0`), `{ action }` alone.
- Range patterns: `start, stop { action }`.
- Regex patterns, and combinations of them: `/a/ && /b/`, `!/x/`.
- `function name(params) { }` (also `func`). Extra parameters act as locals.

A pattern and its `{` must be on the same line: `pattern` followed by `{ ... }`
on the next line is two separate rules, as POSIX specifies.

### Statements

`if`/`else`, `while`, `do`/`while`, `for (;;)`, `for (key in array)`,
`break`, `continue`, `next`, `nextfile`, `exit [code]`, `return [value]`,
`delete array[key]`, `delete array`, `print`, `printf`, and `{ }` blocks.

`print` and `printf` accept redirection: `> "file"`, `>> "file"`,
`| "command"`. The argument list may be in parentheses:
`printf("%d\n", x) > "out.txt"`.

### Expressions

Full POSIX precedence, lowest to highest:

```text
= += -= *= /= %= ^=      assignment (right-assoc)
?:                        conditional
||   &&                   logical
in                        array membership, incl. (i, j) in arr
~   !~                    regex match
<  <=  ==  !=  >=  >      comparison
(juxtaposition)           string concatenation
+  -    *  /  %           arithmetic
!  unary + -              (so -2^2 == -4)
^                         exponent (right-assoc; 2^-1 works)
++  --                    increment / decrement
$                         field ($i++ is ($i)++)
( )                       grouping
```

Comparisons follow POSIX rules: numeric when both sides are numbers or
numeric strings (input fields, `-v` values, `split()` elements and so on),
otherwise string. A variable that has never been assigned compares equal to
both `0` and `""`.

String escapes: `\n \t \r \\ \" \a \b \f \v`, octal `\033`, hex `\x41`.
Unknown escapes keep the backslash (as mawk does), so `"\."` reaches a
dynamic regex as a literal dot.

### Built-in functions

| Kind | Functions |
|---|---|
| String | `length` (with or without parentheses; also array length), `substr`, `index`, `split`, `sub`, `gsub`, `match`, `sprintf`, `tolower`, `toupper` |
| Arithmetic | `int`, `sqrt`, `exp`, `log`, `sin`, `cos`, `atan2`, `rand`, `srand` (returns the previous seed) |
| I/O | `system`, `close`, `fflush` |

Argument counts are checked when the program is parsed, and calls to
undefined functions are reported before anything runs.

### Built-in variables

`NR`, `NF`, `FNR`, `FS`, `OFS`, `ORS`, `RS`, `SUBSEP`, `CONVFMT`, `OFMT`,
`RSTART`, `RLENGTH`, `FILENAME`, `ARGC`, `ARGV`, `ENVIRON`.

- **`FS`:** `" "` (default) splits on runs of whitespace; any other single
  character splits on that character; longer values are regular expressions;
  `""` splits the record into individual characters.
- **`RS`:** a single character, `""` for paragraph mode (records separated by
  blank lines), or a regular expression when longer than one character.
- Assigning to `NF` truncates or extends the record; assigning to a field
  beyond `NF` extends it.

### `getline`

All forms are supported: `getline`, `getline var`, `getline < file`,
`getline var < file`, `cmd | getline`, `cmd | getline var`.
They return 1 for a record, 0 at end of input and -1 on error.

### printf

Conversions `%d %i %o %x %X %u %c %s %e %E %f %F %g %G %%`, flags
`- + space # 0`, width and precision including `*` (`%*d`, `%.*f`).
C length modifiers (`%ld`, `%lld`, `%hd`) are accepted and ignored.
`%c` prints the character with that code for numbers (including numeric
input such as a field holding `65`) and the first character for strings.

### Special file names

`"/dev/stdout"` and `"-"` (for output), and `"/dev/stderr"` are handled
internally, so they work on Windows too. Output to them keeps its order
relative to normal `print` output.

### Numbers

Values are 64-bit floats. Integral values print as integers up to 2^64
(`2^53` prints `9007199254740992`); other values use `OFMT` for output and
`CONVFMT` for string conversion (default `%.6g`). `inf` and `nan` are
recognised in input, as with C's `strtod`.

---

## Differences from mawk

Deliberate choices where mawk_rs does something different from mawk 1.3.4:

| Case | mawk_rs | mawk |
|---|---|---|
| `substr("hello", 0, 2)` | `h` (the POSIX result) | `he` |
| `printf "%5s"` with non-ASCII text | pads by characters | pads by bytes |
| `FS=""` on `é` | 1 field (characters) | 2 fields (bytes) |
| Too few `printf` arguments | missing values print as empty / 0 | run-time error |
| Non-UTF-8 program file | decoded as Latin-1 and printed as UTF-8 | bytes passed through unchanged |
| Error after output | earlier output is flushed before the message | message may appear first |

---

## Known limitations

These are known gaps, roughly in order of how likely you are to hit them.

**Regular expressions** (`regex_engine.rs`)

- `^` and `$` are only recognised at the very start and end of the whole
  pattern. `/^a|^b/` and `/a$|b/` do not behave as they should, and `^` in the
  middle of a pattern is taken literally.
- The first matching alternative wins, rather than the longest match that
  POSIX requires: `match("abc", /ab|abc/)` sets `RLENGTH` to 2.
- No bracket classes such as `[[:digit:]]` and no interval counts such as
  `a{2,3}`.
- Malformed patterns are not reported: `/a)b/` silently ignores everything
  after the `)`.
- Matching is backtracking and can be quadratic: `/.*x/` against a
  20,000-character line takes several seconds.
- Compiled patterns are cached with no size limit, so programs that build a
  new dynamic regex per record keep growing in memory.

**Text encoding**

- Input is converted to UTF-8 as it is read; bytes that are not valid UTF-8
  are replaced with U+FFFD. mawk passes bytes through unchanged.
- A trailing `\r` is stripped from each line when `RS` is newline, so CRLF
  files read cleanly but the carriage returns are not preserved.

**Parsing**

- `"x" in a == 1` and `1 < 2 < 3` are syntax errors (mawk accepts them; add
  parentheses).
- A variable with the same name as a function is not rejected.

**Performance**

About 10–20 times slower than mawk on simple per-record programs
(for example 0.74 s against 0.03 s for `{x=1}` over 2 million lines). Every
variable is looked up by name at run time, each record is split into fields
even when no field is used, and regex patterns copy `$0` before matching.

**Other**

- When a function turns an untyped argument into an array, the caller sees
  the array only after the function returns.
- `for (key in array)` order changes from run to run.
- `getline < "-"` and `"/dev/stdin"` are not special-cased (they work on
  Linux through the real device file, not on Windows).
- Not supported: gawk extensions such as `IGNORECASE`, `RT`, `BEGINFILE`,
  `gensub`, arrays of arrays, and time functions.

---

## How it works

```text
 program text ──► lexer ──► parser ──► AST ──► interpreter ──► output
                  tokens    recursive   rules,     tree-walking
                            descent     functions  evaluator
                                                       │
                                               regex_engine
                                          (backtracking, cached)
```

1. **Lexer** (`lexer.rs`) turns the program into tokens. Whether `/` starts a
   regex or means division depends on the previous token. Built-in function
   names are reserved words.
2. **Parser** (`parser.rs`) is a recursive-descent parser that builds the
   AST and checks the program before it runs: undefined functions, argument
   counts, `break`/`continue` outside loops, `return` outside functions,
   `next` in `BEGIN`/`END`, and missing statement terminators. Errors carry
   line numbers.
3. **Interpreter** (`interp.rs`) walks the AST. It runs the `BEGIN` rules,
   reads records from the input files named in `ARGV` (or standard input),
   runs the pattern rules on each record, then runs `END` and closes all
   files and pipes.
4. **Regex engine** (`regex_engine.rs`) compiles patterns into
   alternations of atom sequences and matches them with a
   continuation-passing backtracking matcher. Compiled patterns are cached
   by their text.

Some implementation details worth knowing when changing the code:

- **Values** (`value.rs`) carry a number, an optional string, and two flags:
  `is_strnum` (a string from input that compares numerically if it looks
  like a number) and `uninit` (never assigned; equal to both 0 and "").
- **Variables** are `Rc<RefCell<VarSlot>>` slots held in a global map and a
  stack of per-call frames. Scalars are passed to functions by value and
  arrays by reference.
- **Assignment targets** are resolved once into an `LRef`, so `a[i++] += 1`
  and `sub(/x/, "y", a[i++])` evaluate `i++` exactly once.
- **Output** to standard output is block-buffered when it is not a terminal
  and line-buffered when it is. It is flushed before `system()` and before
  starting a command pipe, so output stays in order.
- The interpreter runs on a **512 MB thread stack**. Runaway recursion in a
  user function is stopped with an error before the stack is exhausted.
- On Unix, `SIGPIPE` is restored to its default so `mawk_rs ... | head`
  ends quietly, as a C program would.

### Limits

| Limit | Value |
|---|---|
| Fields per record / `NF` | 1,000,000 |
| `printf` width or precision | 1,000,000 |
| Interpreter stack | 512 MB (roughly 90,000–470,000 nested function calls, depending on the function) |

`--version` prints these. Exceeding a limit is a run-time error, not a crash.

## Source layout

| File | Lines | Contents |
|---|---:|---|
| `src/main.rs` | 65 | Version constants, stack-size thread, SIGPIPE handling, exit code |
| `src/lexer.rs` | 372 | Tokens, escapes, regex literals, built-in function table |
| `src/parser.rs` | 908 | Recursive-descent parser and program checks |
| `src/ast.rs` | 87 | `Node`, `Stmt`, `Rule`, `FuncDef`, `Program` |
| `src/interp.rs` | 2,489 | Evaluator, fields and records, I/O streams, built-ins, printf, command line |
| `src/regex_engine.rs` | 327 | Regex compiler, matcher and cache |
| `src/value.rs` | 169 | Value type, number parsing and formatting, truthiness |

The version appears in two places in `main.rs`: the header comment and the
`VERSION` / `RELEASE_DATE` constants that `--version` prints. Keep them (and
`version` in `Cargo.toml`, if you use it) in step.

---

## Testing

Two regression suites cover every fixed bug plus common one-liners.

**Linux, macOS, Git Bash, WSL** — `tests.sh` (132 checks):

```sh
./tests.sh ./target/release/mawk_rs
```

It needs bash and the GNU tools `timeout`, `mktemp` and `realpath`. If a
reference `mawk` is on the `PATH`, it also compares a set of one-liners
against it.

**Windows (cmd.exe)** — `test.bat` with the `tests\` folder (105 cases):

```bat
test.bat target\release\mawk_rs.exe
```

Each case is a set of files in `tests\`: `NNN_name.awk` (program), `.out`
(expected output, byte for byte), and optionally `.in` (stdin), `.rc` (exit
code), `.args` (options before `-f`) and `.post` (operands after it). To add a
test, add a new `NNN_name.awk` and `.out` pair. `tests\.gitattributes` stops
Git from converting their line endings.

Both scripts print `passed: N  failed: N` and exit non-zero on failure.

## Windows notes

- `system()`, `print | "cmd"` and `"cmd" | getline` run commands through
  `cmd /C` (through `sh -c` elsewhere).
- `/dev/stdout` and `/dev/stderr` work as output names; `/dev/stdin` does not.
- `ENVIRON` keys are case-sensitive, and Windows stores some variables in
  mixed case: use `ENVIRON["Path"]`, not `ENVIRON["PATH"]`.
- Output lines end in `\n`, not `\r\n`.
- Error messages include the Windows wording, for example
  `cannot open "x.txt" (The system cannot find the file specified.)`.

## Exit status

| Status | Meaning |
|---|---|
| 0 | Success, or the value given to `exit` |
| *n* | `exit n` (taken modulo 256; `exit -1` gives 255) |
| 2 | Syntax error, run-time error, missing or unreadable input or program file, bad option |
| 141 | Output pipe closed by the reader (for example `mawk_rs ... \| head`); on Unix the process is ended by `SIGPIPE`, which shells report as 141 |
