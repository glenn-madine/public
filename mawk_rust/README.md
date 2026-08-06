# mawk_rs

A feature-rich AWK interpreter, written in Rust as a line-for-line port of
a ~2,400-line C implementation (`mawk.c`). It supports the vast majority
of the AWK language: patterns and actions, the full expression grammar,
user-defined functions, associative arrays, a real (hand-rolled) regex
engine, `printf`-style formatting, and all the usual I/O forms.

## Features

- **Patterns**: `BEGIN`, `END`, `/regex/`, expression patterns, range
  patterns (`pat1, pat2`), and bare `{ action }` blocks.
- **Fields**: `$0`, `$1..$NF`, `NF`, `NR`, `FNR`, `FILENAME`; assigning to
  a field, to `$0`, or to `NF` correctly rebuilds the record using `OFS`.
- **Built-in variables**: `FS`, `OFS`, `ORS`, `RS`, `SUBSEP`, `CONVFMT`,
  `OFMT`, `RSTART`, `RLENGTH`, `ARGC`/`ARGV`, `ENVIRON`.
- **Expressions**: assignment operators (`= += -= *= /= %= ^=`), the `?:`
  ternary, `|| && !`, relational and match operators (`~ !~`),
  string concatenation, arithmetic (`+ - * / % ^`), unary `+`/`-`,
  `++`/`--` (prefix and postfix), `(expr) in array` / `(a, b) in array`,
  and parenthesized grouping.
- **Statements**: `if`/`else`, `while`, `do`/`while`, `for(;;)`,
  `for (k in arr)`, `break`, `continue`, `next`, `nextfile`, `exit`,
  `delete arr` / `delete arr[k]`, `print`/`printf` with `>`, `>>`, and
  `|` redirection, and `getline` in every form (plain, into a variable,
  `< file`, `var < file`, `cmd | getline`, `cmd | getline var`).
- **User-defined functions**: `function name(params) { ... }`, recursion,
  arrays passed by reference, scalars passed by value, extra parameters
  usable as local scalars/arrays, `return`.
- **Built-ins**: `length`, `substr`, `index`, `split`, `sub`, `gsub`,
  `match`, `sprintf`, `toupper`, `tolower`, `sin`, `cos`, `atan2`, `sqrt`,
  `exp`, `log`, `int`, `rand`, `srand`, `system`, `close`, `fflush`.
- **Regex engine**: a real backtracking engine supporting literals, `.`,
  `^`, `$`, `*`, `+`, `?`, `[...]`/`[^...]` character classes, grouping
  `(...)`, and alternation `a|b` — used for `/ere/` patterns, `~`/`!~`,
  `FS`, `RS`, `split()`, `sub()`/`gsub()`, and `match()`.
- **Command line**: `-v var=value`, `-F fs`, `-f progfile` (repeatable),
  `var=value` interspersed with filenames on the argument list, and
  runtime `ARGV`/`ARGC` mutation is honored.

**Not implemented** (also out of scope in the original C version): true
POSIX locale collation in bracket expressions, regex backreferences,
coprocesses (`|&`), `printf` `%*d` dynamic width/precision, and
`[:alpha:]`-style POSIX character classes.

## Building

Requires a recent stable Rust toolchain (edition 2021).

```bash
cargo build --release
```

The binary is produced at `target/release/mawk_rs`. To install it onto
your `PATH`:

```bash
cargo install --path .
```

## Usage

```
mawk_rs [-F fs] [-v var=val ...] 'program text' [file|var=val ...]
mawk_rs [-F fs] [-v var=val ...] -f prog.awk [-f prog2.awk ...] [file|var=val ...]
```

| Option | Meaning |
|---|---|
| `-F fs` | Set the field separator (`FS`) before `BEGIN` runs. |
| `-v var=val` | Assign a variable before `BEGIN` runs (repeatable). |
| `-f progfile` | Read the program from a file instead of argv (repeatable; files are concatenated). |
| `var=value` (as a positional arg) | Assigned as a variable when reached in the input file list, in order. |
| `-` (as a positional arg) | Read from stdin at that point in the file list. |
| `--` | Stop option parsing; everything after is a program/file argument. |

### Examples

```bash
# Print the first and third whitespace-separated fields
mawk_rs '{print $1, $3}' file.txt

# Sum a column
mawk_rs '{sum += $2} END {print sum}' file.txt

# Use a custom field separator
mawk_rs -F: '{print $1}' /etc/passwd

# Pre-assign a variable
mawk_rs -v threshold=10 '$2 > threshold {print}' file.txt

# Program from a file
mawk_rs -f report.awk data1.txt data2.txt

# Read from stdin
some_command | mawk_rs '{print NR, $0}'
```

## Project layout

| File | Contents |
|---|---|
| `src/main.rs` | Entry point; dispatches to `interp::run`. |
| `src/regex_engine.rs` | The backtracking regex engine (a continuation-frame matcher). |
| `src/value.rs` | The AWK scalar `Value` type (number / string / "strnum") and its coercion rules. |
| `src/lexer.rs` | Hand-written tokenizer, including the `/`-as-division-vs-regex disambiguation. |
| `src/ast.rs` | AST node types: `Node` (expressions), `Stmt` (statements), `Rule`, `FuncDef`. |
| `src/parser.rs` | Recursive-descent parser with the same operator precedence/associativity as the grammar in `mawk.c`. |
| `src/interp.rs` | The tree-walking evaluator: variable/array storage, field splitting & rebuilding, the I/O stream cache (files, append, pipes), `RS`-aware record reading (line / single-char / paragraph / regex modes), `printf`/`sprintf` formatting, all built-ins, and the CLI/driver logic equivalent to `mawk.c`'s `main()`. |

## Design notes

- **Variables and arrays** are stored as `Rc<RefCell<VarSlot>>`. This is
  what makes AWK's "arrays are passed by reference, scalars by value"
  function-call semantics fall out naturally: passing an array argument
  just clones the `Rc`, so the callee mutates the same underlying map the
  caller sees. The original C code has to track this explicitly with an
  alias-flag array that it frees or leaves alone on return; the Rust
  version doesn't need that bookkeeping at all.
- **Byte-oriented, like the source.** The regex engine, field splitting,
  and `sub`/`gsub` operate on raw bytes (matching the original's
  `char`-based approach) rather than assuming valid multi-byte UTF-8
  throughout, converting to/from Rust `String`s with lossy UTF-8 decoding
  only at the boundaries where a `&str` is required.
- **`rand`/`srand`** use a small xorshift PRNG reseeded on `srand()` the
  same way the C version reseeds `rand()`. The exact output sequence
  won't match glibc's `rand()`, but the seeding/determinism contract
  (same seed ⇒ same sequence) holds.
- **AST sharing.** Parsed rule/function bodies are wrapped in `Rc` so the
  evaluator can hold a cheap handle to a function body or rule list while
  still mutating interpreter state (`&mut self`) during recursive
  evaluation, without fighting the borrow checker.

## Testing

There's no bundled test suite in this directory, but the port was
validated by building the original C program alongside this one and
diffing output across ~60 test programs — arithmetic, string ops, regex
(alternation, quantifiers, character classes, anchors), arrays
(including multi-subscript keys), every `getline` form, pipes,
redirection, custom `RS` (single-char, multi-char/regex, paragraph mode),
`printf` formatting edge cases, recursion, `NF`/field mutation, and
mixed string/number comparisons — plus a 100,000-line throughput run.
All outputs matched the C build byte-for-byte.

## License

No license specified; adapt to your needs.
