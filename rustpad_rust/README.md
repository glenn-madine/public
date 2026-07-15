# rustpad — Code Analysis

## Overview

**rustpad** is a small, Notepad-style desktop text editor written in Rust. It uses
[`eframe`/`egui`](https://github.com/emilk/egui) for windowing and UI (which in turn uses
`winit` for cross-platform window/event handling on Windows, Linux (X11 + Wayland), and
macOS), [`rfd`](https://crates.io/crates/rfd) for native file dialogs, and
[`arboard`](https://crates.io/crates/arboard) for clipboard access. The project ships its
own dependency-free syntax highlighter rather than pulling in a heavyweight crate like
`syntect`.

The codebase is small and split into four files:

| File | Role |
|---|---|
| `Cargo.toml` | Crate manifest — dependencies and release profile |
| `build.rs` | Build script — embeds a Windows `.ico` resource on Windows targets only |
| `main.rs` | Application entry point, UI/state (`RustpadApp`), menus, file I/O, clipboard, find |
| `highlight.rs` | `Language` enum, language detection, and the single-pass syntax highlighter |

## Architecture

### `main.rs` — application shell and state

The app is a single `RustpadApp` struct implementing `eframe::App`, holding all editor
state: the text buffer, current file path, dirty flag, word-wrap toggle, detected
`Language`, the current text selection, a status-bar message, and some UI toggle flags
(unsaved-changes dialog, About dialog, Find bar).

Notable behaviors:

- **New/Open/Save/Save As** — standard file operations via `rfd::FileDialog`, with file
  filters per supported language plus an "All Files" catch-all. Opening or saving a file
  calls `highlight::detect_language()` so the editor automatically switches syntax
  highlighting based on the file's extension.
- **Unsaved-changes guard** — a `PendingAction` enum (`New`, `Open`, `Exit`) combined with
  a `guard()`/`run_action()` pair defers a destructive action until the user responds to a
  "Save changes?" prompt, mirroring Notepad's behavior. This also intercepts the OS window
  close event via `ViewportCommand::CancelClose`.
- **Clipboard** — `copy_selection`, `cut_selection`, and `paste_clipboard` operate on a
  tracked `Option<Range<usize>>` selection (refreshed every frame from the text widget's
  cursor range) using `arboard`.
- **Menu bar** — File / Edit / Format / Help menus, including a `Format → Language` submenu
  that iterates `Language::ALL` as radio buttons, letting the user override auto-detection.
- **Find bar** — a minimal, non-incremental substring search (`str::find`) reporting a byte
  offset in the status bar; it does not currently scroll to or select the match.
- **Keyboard shortcuts** — Ctrl+N/O/S/Shift+S/F are wired directly into `update()` via
  `ctx.input()`.
- **Custom layouter** — the text editor widget is given a `layouter` closure that calls into
  `highlight::highlight()` on every frame, so highlighting is fully recomputed each redraw
  rather than cached/incremental.

### `highlight.rs` — language detection and the tokenizer

This is the more algorithmically interesting file. Two public entry points:

- `detect_language(path: &Path) -> Language` — a simple extension → `Language` lookup
  table (case-insensitive on the extension).
- `highlight(ui, text, language, wrap_width) -> Arc<egui::Galley>` — builds a colored
  `egui::text::LayoutJob` by walking the input **once, character by character**, and
  returns a laid-out `Galley` ready for the UI to draw.

**Supported languages** (10, including Plain Text):

| Language | Extensions | Comments | Notes |
|---|---|---|---|
| Plain Text | — | — | No tokenizing; single default-colored run |
| C | `.c` `.h` | `/* */`, `#include`-style directives colored as macros | |
| C++ | `.cpp` `.cc` `.cxx` `.hpp` `.hh` `.hxx` | same as C | C keyword set + C++-only keywords |
| C# | `.cs` | `//`, `/* */` |  |
| Rust | `.rs` | `//`, `/* */` | |
| Python | `.py` `.pyw` | `#` | No block comments |
| Visual Basic / VBScript | `.vb` `.vbs` | `'` to end of line | Case-insensitive keywords |
| Batch | `.bat` `.cmd` | `::` to end of line | Case-insensitive keywords; `%VAR%` highlighted |
| HTML | `.htm` `.html` | `<!-- -->` | Tags colored as opaque units, not attribute-parsed |
| ASP.NET | `.aspx` | HTML rules + `<% %>` code blocks colored as macros | |

The tokenizer recognizes, in priority order per character: language-specific comment/tag/
directive forms, block comments, line comments, string/char literals, numeric literals,
and identifiers (colored as keywords, `Type`-like if capitalized, or default). Everything
else (whitespace, punctuation, operators) is emitted one character at a time in the
default color.

A `Palette` struct centralizes seven colors (keyword, string, comment, number, type, macro,
default) reused across all languages, keeping the visual style consistent.

## Key design decisions

- **Single-pass tokenizer, not a real parser.** The comment at the top of the file is
  explicit about this trade-off: it's "enough to color keywords, strings, numbers,
  comments... but not a real parser." This keeps the implementation small and
  dependency-free, at the cost of nested/contextual correctness (see Limitations).
- **Per-language boolean flags rather than a trait/strategy object.** `has_block_comments`,
  `is_python`, `is_c_family`, `is_vb`, `is_batch`, `is_html`, `is_aspx` are computed once
  per call and checked inline in the loop. This is easy to read and fast, but each new
  language adds a few more `if` branches to the shared loop rather than being isolated in
  its own function.
- **Case sensitivity handled per-language.** Rust/C/C++/C#/Python are case-sensitive
  languages, so keywords are matched verbatim. VB and Batch are case-insensitive in real
  life, so their keyword tables are stored upper-case and looked up via
  `word.to_uppercase()`, correctly highlighting `Dim`, `DIM`, or `dim` alike.
- **Markup handled at the tag level, not the attribute level.** For HTML/ASPX, an entire
  `<tag attr="value">` is consumed and colored as one `Type`-colored run instead of
  separately coloring the tag name, attribute names, and quoted attribute values. This is
  a deliberate simplification consistent with the "not a real parser" goal.
- **No caching/incremental re-highlighting.** `highlight()` re-tokenizes the entire buffer
  from scratch on every frame via the `layouter` callback. Fine for the file sizes a
  Notepad-style editor typically handles; would not scale well to very large files.

## Strengths

- Zero highlighting dependencies — pure Rust, no `syntect`/tree-sitter grammars to ship.
- Consistent, centralized color palette across all ten languages.
- Correct handling of escaped characters inside string/char literals (`\"`, `\\`, etc.).
- Sensible fallbacks: unrecognized extensions degrade gracefully to `Language::Plain`.
- Auto re-detection of language on both Open and Save (so "Save As `foo.py`" immediately
  starts highlighting Python).

## Limitations / trade-offs

- **No nested/contextual awareness.** Because it's a flat character scan with ordered
  `if` checks, constructs that require context (e.g., a `'` inside a VB string literal, or
  a `%` sign used as a modulo operator vs. a Batch variable delimiter) can be
  misinterpreted; there's no lexer state machine or lookahead beyond a few characters.
- **HTML/ASPX attribute values aren't independently colored** — an entire tag is one run,
  so a string inside `href="..."` won't get the `STRING` color.
- **The `Type`-if-capitalized heuristic** (`word.chars().next()...is_uppercase()`) applies
  uniformly to every language, including HTML/Batch, where "capitalized word" doesn't
  correspond to a real type concept — a relatively harmless but slightly arbitrary
  cosmetic side effect.
- **Full-buffer re-tokenization every frame** has no incremental/dirty-region
  optimization, which is fine at Notepad-scale files but would become a bottleneck on
  large documents.
- **Find is minimal** — single-direction, non-highlighting, byte-offset-only feedback, with
  no "wrap around" or case-insensitive option currently.
- **Likely copy/paste artifact**: `"cs" | "hs" => Language::Cs` in `detect_language` maps
  `.hs` (conventionally Haskell) to C#, which is probably not intentional.

## Suggested follow-ups

1. Fix or remove the `.hs` → C# extension mapping.
2. Color attribute values inside HTML/ASPX tags rather than treating the whole tag as one run.
3. Make Find select/scroll to the match and support wrap-around + case-insensitive search.
4. Consider caching the last `LayoutJob` and only re-tokenizing on text change, if large
   files become a use case.
