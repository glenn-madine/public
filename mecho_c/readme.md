# MECHO — Colorized ECHO for MS-DOS

`MECHO` is a small MS-DOS command-line utility, written for Turbo C 2.01,
that behaves like the built-in DOS `ECHO` command but can display its
text in a chosen foreground and/or background color.

## Building

Requires Turbo C 2.01 (or a compatible Borland/Turbo C toolchain with
`conio.h`).

```
TCC MECHO.C
```

or, from the IDE: **Project → New Project → MECHO.C → Compile/Make**.

## Usage

```
MECHO [/FG<ColorName>] [/BG<ColorName>] text [more text ...]
MECHO /?
```

- `/FG<ColorName>` sets the foreground (text) color.
- `/BG<ColorName>` sets the background color.
- Either switch is optional, and they may appear in either order.
- If neither switch is given, `MECHO` prints plain text exactly like the
  standard DOS `ECHO` command.
- `MECHO /?` prints usage help and the full list of valid color names.
- Running `MECHO` with no arguments prints a blank line, matching bare
  `ECHO` behavior.

### Examples

```
MECHO Hello there
MECHO /FGRed "The message is displayed in red"
MECHO /FGRed /BGGreen "Red text on a green background"
MECHO /BGBlue /FGYellow Warning: disk almost full
```

## Available colors

**Foreground** (all 16 standard text-mode colors):

Black, Blue, Green, Cyan, Red, Magenta, Brown, LightGray, DarkGray,
LightBlue, LightGreen, LightCyan, LightRed, LightMagenta, Yellow, White

**Background** (8 colors only):

Black, Blue, Green, Cyan, Red, Magenta, Brown, LightGray

Only the 8 base colors are valid backgrounds because, on standard DOS
text-mode hardware, the high-intensity bit of the background attribute
nibble is repurposed to control blinking rather than color intensity —
so "bright" background colors aren't available.

Color name matching is case-insensitive (`/fgred`, `/FGRed`, and
`/FGRED` are all equivalent).

## Behavior notes

- An unrecognized color name after `/FG` or `/BG` causes `MECHO` to
  print an error and exit with status code `1`.
- The message text is reconstructed from all remaining command-line
  arguments, joined with single spaces — just like `ECHO` echoes its
  arguments back.
- The rebuilt message is capped at 256 bytes (`CMD_BUFSIZE`); text
  beyond that limit is dropped rather than overflowing the buffer.
- After printing colored text, `MECHO` restores the console to
  LightGray-on-Black before returning, so it doesn't leave the terminal
  in a colored state for the next command.

## Version

Current version: **1.22**

### Revision history

- Initial release: colorized `ECHO` replacement with `/color` /
  `/BGcolor`-style documented switches.
- Switch parsing internally used `/FC` / `/BG` prefixes, which did not
  match the documented `/color` switch — foreground color selection did
  not work as documented.
- Documentation and code were renamed to `/FG` / `/BG` for clarity, but
  the parsing code still checked for `/FC` (a one-character mismatch
  with the new `/FG` docs), so the same underlying issue persisted.
- Fixed: switch parsing now correctly checks for `/FG`, matching the
  documented interface.
- Cleaned up a stale internal comment that still referenced the old
  `/color` / `/BGcolor` switch names.

As of this version, the documented usage, the `/?` help output, and the
actual argument parsing are all consistent with one another.
