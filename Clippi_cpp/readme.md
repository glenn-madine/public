# Clippi – Clipboard Buttons

**Version 1.2.0** · Glenn Madine · November 23, 2026 · Pure Win32 / C++17 · Windows 10 and 11

Clippi is a small Windows tool that keeps canned support text one click away. Each button copies a prepared message to the clipboard: a chat reply, a ticket note, a meeting-request body or a command line. Before copying, it can ask for a value such as the application name and fill it in. **Clippi Editor** is the companion program used to create and edit those buttons, including formatted text, links and pictures.

Clippi only copies text to the clipboard. It never runs the commands it holds; the Paste button only presses Ctrl+V in the window you were using.

---

## 1. Contents of the package

| File | Purpose |
|---|---|
| `ClipBoardButtons.cpp` | Clippi, the main program (1,746 lines) |
| `ClippiEditor.cpp` | Clippi Editor, the helper program for editing the buttons (3,231 lines) |
| `resource.h` | Shared version number, author, date and resource IDs |
| `ClipBoardButtons.rc` | Clippi resources: icon, embedded default button file, version info |
| `ClippiEditor.rc` | Clippi Editor resources: icon, version info |
| `ClipBoardButtonDefinitions.json` | The default buttons (19 buttons in 7 groups), embedded in Clippi's .exe |
| `Clippi.ico` | Program icon used by both programs. **Not in this upload; required to build.** |

---

## 2. Building

### Visual Studio 2019 / 2022

Create two projects (**Windows Desktop Application**, empty) in one solution, for example `ClipBoardButtons` and `ClippiEditor`.

| Project | Add these files |
|---|---|
| ClipBoardButtons | `ClipBoardButtons.cpp`, `ClipBoardButtons.rc`, `resource.h`, `Clippi.ico`, `ClipBoardButtonDefinitions.json` |
| ClippiEditor | `ClippiEditor.cpp`, `ClippiEditor.rc`, `resource.h`, `Clippi.ico` |

Project settings, for both projects and all configurations:

- **C/C++ › Language › C++ Language Standard:** ISO C++17 (or later)
- **Advanced › Character Set:** Use Unicode Character Set (the code also defines `UNICODE` itself)
- **Linker › System › SubSystem:** Windows (`/SUBSYSTEM:WINDOWS`); the entry point is `wWinMain`
- **Libraries:** nothing to add. Each .cpp file links what it needs with `#pragma comment(lib, …)`: comctl32, comdlg32, shell32, advapi32, ole32, gdi32 and windowscodecs. It also requests Common Controls 6 through a `/manifestdependency` pragma.

Put **ClippiEditor.exe in the same folder as ClipBoardButtons.exe**. Clippi's *Edit Clips* button looks for the editor there and falls back to Notepad if it isn't found.

### MinGW-w64 (optional; used for test builds)

```
x86_64-w64-mingw32-g++ -std=c++17 -municode -mwindows -O2 ClipBoardButtons.cpp -o ClipBoardButtons.exe -lcomctl32 -lshell32 -ladvapi32
x86_64-w64-mingw32-g++ -std=c++17 -municode -mwindows -O2 ClippiEditor.cpp -o ClippiEditor.exe -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lwindowscodecs -luuid -lgdi32
```

Compile the resource files with `windres` and link them in to get the icon, version info and embedded default buttons. Both programs compile with `-Wall` without errors or warnings.

### Changing the version

Edit `resource.h`. The three numbers and the two strings must match (see finding 1 in section 8):

```
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 2
#define APP_VERSION_PATCH 0
#define APP_VERSION_STR   "1.2.0"
#define APP_VERSION_WSTR  L"1.2.0"
```

---

## 3. First run and where files live

| Item | Location |
|---|---|
| Button file | `%USERPROFILE%\Clippi_Storage\ClipBoardButtonDefinitions.json` |
| Backup made by the editor | same folder, `ClipBoardButtonDefinitions.json.bak` |
| Settings | `HKCU\Software\ClipBoardButtons`: `AlwaysOnTop` (DWORD), `LastGroup` (string) |

On first run, Clippi creates `Clippi_Storage` and writes the default button file embedded in the .exe. An existing file is **never overwritten** (`CREATE_NEW`), so your edits survive upgrades. The editor opens the same file, or the file given on its command line.

---

## 4. Using Clippi

The window has a **groups list** on the left and the **buttons of the selected group** on the right. Below them are *Always on top*, *Paste*, *Edit Clips* and *Exit*, then a status line with an *About* link.

Clicking a button:

1. If the button has a prompt title, Clippi asks for a value. The first prompt is required; the second is optional and its box says "(optional)". The last answer for each prompt title is remembered for the session.
2. The text is copied to the clipboard as plain text, plus RTF and HTML versions when the button has formatting or pictures.
3. The status line shows *Copied: …*. Press **Paste** (Alt+P) to switch back to the window you were using and press Ctrl+V there.

| Key | Action |
|---|---|
| Ctrl+Tab / Ctrl+Shift+Tab (or Ctrl+PgDn / Ctrl+PgUp) | Next / previous group |
| Tab, arrow keys, Enter, Space | Move between buttons and press them |
| Alt+T | Always on top |
| Alt+P | Paste into the previous window |
| Alt+E | Edit Clips (opens Clippi Editor) |
| Alt+X | Exit |
| F5 | Reload the button file (also System menu › Reload Buttons) |

Other behavior:

- **Window size:** the window is sized for the largest group, so it doesn't jump when you switch groups. A group taller than about 80% of the screen wraps into extra columns.
- **Paste target:** Clippi remembers the last window you used before switching to it. Clippi Editor's window is never chosen as the target.
- **Automatic reload:** after *Edit Clips*, the buttons reload when the editor closes, or when you switch back to Clippi, if the file was saved.
- **High DPI:** Clippi is per-monitor DPI aware (Windows 10 1607 or later) and re-lays itself out when moved to a monitor with different scaling.

---

## 5. Using Clippi Editor

| Area | What it does |
|---|---|
| Buttons list (left) | All buttons as *Group: Label*, tagged `[formatted]`, `[asks]` or `[asks 2]`. Add, Duplicate, Delete, Move Up, Move Down (Ctrl+N adds) |
| Button label | Text on the button in Clippi |
| Group | Type a new name or pick an existing one; empty means "General". Groups appear in Clippi in the order they first occur |
| Prompt title / Second prompt title | If filled in, Clippi asks for a value and puts it where `%XXX%` / `%YYY%` is |
| Insert %XXX% / Insert %YYY% | Insert a placeholder at the cursor |
| Formatting toolbar | **B** **I** **U** **S** (they light up to match the selection), Font…, Color…, Link…, Picture…, Clear format |
| Text to paste | WYSIWYG rich-text box (Rich Edit). What you see is what pastes |
| Insert from HTML/RTF file… | Adds a file's contents at the cursor |
| Export RTF… / Export HTML… | Saves the current button's formatted version to a file |
| Save / Close | Ctrl+S saves; closing with unsaved changes asks first |

**Keyboard and paste in the text box**

| Key | Action |
|---|---|
| Ctrl+B / Ctrl+I / Ctrl+U | Bold / italic / underline |
| Ctrl+L / Ctrl+E / Ctrl+R / Ctrl+J | Align left / center / right / justify |
| Ctrl+V | Paste with formatting: RTF (Word, Outlook) as-is; HTML (Teams, browsers) converted; pictures (screenshots) as PNG |
| Ctrl+Shift+V | Paste plain text only |
| Right-click | Undo, Cut, Copy, Paste, Paste as plain text, Select All |

**How the editor saves**

- **Plain text:** a button with no formatting is saved as plain text, exactly as before.
- **Formatted buttons** are saved as three versions:
  - plain text, for Notepad and other programs (links written as `text <url>`)
  - `%RTF:…%`, for Outlook and Word (the box's own RTF)
  - `%HTML:…%`, for Teams and browsers (generated from the same content)
- **Unchanged buttons:** a button whose text you didn't change is saved byte-for-byte as it was loaded.
- **Older buttons** with `%IMG%` pictures, or with `%HTML%` only, open formatted and are converted when their text is edited.
- **Backup:** the previous file is copied to `.bak` before every save, and the save goes through a temporary file, so a failed save never damages the original.
- **Placeholder warning:** if `%XXX%` or `%YYY%` has mixed formatting inside it (for example half bold), Clippi can't fill it in the formatted versions, so the status line warns after saving.

---

## 6. The button file (JSON)

The file is a JSON array of objects, one per button, in UTF-8 (UTF-16LE with a BOM is also read). Fields Clippi doesn't know are kept by the editor.

| Field | Required | Meaning |
|---|---|---|
| `buttonText` | yes | Label on the button |
| `pasteText` | yes | What is copied (markup below) |
| `group` | no | Group name; empty or missing = "General" |
| `inputBoxTitle` | no | Title of the first prompt; the answer replaces `%XXX%` |
| `inputBoxTitle2` | no | Title of the optional second prompt; the answer replaces `%YYY%` |
| `rawText` | no | `true` = take `pasteText` literally (see below). Written by the editor when needed |

**pasteText markup**

| Markup | Meaning |
|---|---|
| `\\n`, `\\t` (in the JSON file) | Line break, tab |
| `%XXX%`, `%YYY%` | Replaced by the prompt answers (also inside RTF and HTML, escaped for each format) |
| `%IMG:<base64 PNG>%` | Picture; Clippi builds RTF and HTML versions around it (Calibri 11 pt). Older format, still supported |
| `%RTF:<base64>%` | Ready-made RTF document for Outlook and Word |
| `%HTML:<base64>%` | Ready-made UTF-8 HTML fragment for Teams and browsers |

The text outside the tokens is always the plain-text version. Line breaks at the end are not copied.

**rawText.** Normally the two characters `\n` mean a line break, so text such as `\\server\tools\new` would turn into a tab and a line break. With `"rawText": true` the text is copied exactly as written, and line breaks are ordinary JSON line breaks. The editor sets this automatically when the text contains a literal `\n` or `\t`.

Example:

```json
{
    "group": "Chat",
    "buttonText": "Ask user: Are you ready for remote",
    "pasteText": "Are you ready for me to remote into your PC and install %XXX%?\\n\\nNOTE: ...",
    "inputBoxTitle": "Software Name"
}
```

**What goes on the clipboard**

| Clipboard format | Used by | Contents |
|---|---|---|
| `CF_UNICODETEXT` | Notepad, command prompt, most programs | Plain text, CRLF line breaks |
| `Rich Text Format` | Outlook, Word | `%RTF%` token, or built from text + `%IMG%` |
| `HTML Format` (CF_HTML) | Teams, browsers, web mail | `%HTML%` token, or built from text + `%IMG%` |

---

## 7. How the code is organized

Both programs are single-file, pure Win32 applications with no external libraries. They share `resource.h` and the JSON rules.

### ClipBoardButtons.cpp (Clippi)

| Section | Contents |
|---|---|
| JSON parser | `JsonValue`, `JsonParser`: objects, arrays, strings, numbers, literals; errors report line and column |
| Button definitions | `ButtonDef`, `ReadTextFile` (UTF-8/UTF-16/ANSI), `LoadDefinitions`, `FindJsonPath`, `CreateDefaultJsonFile` (writes the embedded RCDATA once) |
| Text helpers | `ReplaceAll`, `ReplacePlaceholders` (one pass, so typed text is never re-replaced), `PrepareText` (`\n`/`\t` conversion, CRLF, placeholders) |
| Rich content | `SplitSegments` (finds `%IMG%`/`%RTF%`/`%HTML%`), `BuildRtfDocument`, `BuildHtmlFragment`, `WrapCfHtml` (CF_HTML header with byte offsets), `CopyToClipboard` |
| Input box | `DialogTemplate` builds the prompt dialog in memory, so no dialog resource is needed |
| Window | Groups pane, button grid (`LayoutWindow`, `ShowGroupButtons`), Always on top, About |
| Editor tracking | `OpenEditor`, `RegisterWaitForSingleObject` on the editor process (tagged with a launch number), reload when the file's time or size changes |
| Paste | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` remembers the previous window; `PasteIntoTarget` waits for modifier keys to be released and sends Ctrl+V with `SendInput` |
| DPI | Per-monitor v2 via run-time lookup of `SetProcessDpiAwarenessContext`, `GetDpiForWindow`, `GetSystemMetricsForDpi`, `AdjustWindowRectExForDpi`; `WM_DPICHANGED` rebuilds fonts and layout |

### ClippiEditor.cpp (Clippi Editor)

| Section | Contents |
|---|---|
| JSON | Same parser as Clippi, plus a pretty-printing writer (4-space indent) |
| Helpers | Files, UTF-8, Base64, PNG/JPEG size, CF_HTML fragment extraction |
| Pictures | WIC: any picture file → PNG, PNG encoding; GDI: draws metafiles (EMF/WMF) and DIBs into a PNG |
| Document model | `Fmt`, `Run`, `Para`, `Doc`: text runs with bold/italic/underline/strike, font, size, color, highlight and link; line breaks; PNG/JPEG pictures; paragraph alignment |
| Converters | `RtfToDoc` (reads what Rich Edit, Word and Outlook write), `HtmlToDoc` (browser-style whitespace, entities, CSS, lists, `data:` images), `DocToRtf`, `DocToHtml`, `DocToPlain`, `IsPlainDoc` |
| Clip model | `ClipFromJson`, `ClipToJson` (unchanged clips are written exactly as loaded; `rawText` handling; unknown fields kept) |
| Rich Edit box | Stream in/out, default Calibri 11 pt, `CommitBox` (decides plain vs formatted), toolbar actions, special paste (HTML, pictures), context menu |
| Window | Layout, list and field handling, accelerators, save with temp file + `.bak` |

### Data flow

```
 Clippi Editor                           JSON file                      Clippi
 ─────────────                           ─────────                      ──────
 Rich Edit box ── RTF ──► RtfToDoc ─┬─► plain text ─┐
                                    ├─► %RTF:…%  ───┼─► pasteText ─► SplitSegments ─► CF_UNICODETEXT
                                    └─► %HTML:…% ───┘                               ├► Rich Text Format
                                                                                     └► HTML Format
```

---

## 8. Final analysis

### What is done well

- **Safe file handling.** The editor writes to a temporary file, keeps a `.bak` and then replaces the original. Clippi never overwrites an existing button file and reloads only when the file's time or size has changed.
- **Correct clipboard formats.** CF_HTML offsets are counted in UTF-8 bytes; RTF pictures use `\pngblip` with pixel and twip sizes; clipboard memory is freed on failure; `OpenClipboard` is retried.
- **Placeholders done carefully.** One-pass replacement, filled in after the `\n` conversion so typed text is pasted verbatim, and escaped separately for RTF and HTML.
- **No surprises for existing data.** Unedited buttons are saved byte-for-byte; unknown JSON fields are kept; older `%IMG%` buttons still work in Clippi.
- **No dependencies.** Pure Win32, no runtime installers, no resource dialogs (prompt dialogs are built in memory).

### Findings

| # | Severity | Finding | Recommendation |
|---|---|---|---|
| 1 | **Fix** | `resource.h` has `APP_VERSION_MINOR 1` but the strings say `"1.2.0"`. The title bar and About box show 1.2.0, but Explorer's numeric file version (`FILEVERSION`) shows 1.1.0.0. | Set `APP_VERSION_MINOR 2`. Optionally build the strings from the numbers with a stringize macro so they can't drift. |
| 2 | Medium | The Rich Edit parts of Clippi Editor (the box, toolbar, formatted paste, dialogs) compile but have not been run on Windows. The test environment's Wine would not start. The converters and JSON handling were tested (see section 9). | Run the checklist in section 10 once on a real PC. |
| 3 | Low | Clippi refuses button files over 16 MB; the editor accepts up to 32 MB. A file with many large pictures could save in the editor but fail to load in Clippi. | Use the same limit in both programs (32 MB). |
| 4 | Low | Clippi is per-monitor DPI aware; the editor is only system-DPI aware (`SetProcessDPIAware`), so it looks blurry on a second monitor with different scaling. | Port Clippi's DPI helpers to the editor. |
| 5 | Low | The JSON parser, Base64 and PNG helpers are duplicated in both .cpp files. | Move them to a shared header (e.g. `ClippiCommon.h`). |
| 6 | Info | Formatted buttons store pictures twice (in the RTF and in the HTML), so picture-heavy buttons make the file larger. The editor warns about pictures over 100 KB. | Keep pictures small (logos of a few KB). |
| 7 | Info | HTML pasted into the editor is converted to formatting. Text styles, links, lists, alignment and embedded pictures come through; complex layouts (tables, margins) are simplified; pictures that are only web addresses are left out, with a status message. | Expected behavior; mentioned so it isn't a surprise. |
| 8 | Info | The default button file is embedded in Clippi's .exe. The *Service Desk* URL contains session- and user-specific query parameters (`ovuser` with an encoded e-mail address, `TeamsCID`, `clickparams`). | Before sharing the .exe with others, shorten that URL to its path. |
| 9 | Info | Button data: typo "Tell user**t**"; labels with trailing or leading spaces ("Ask user: Are you ready for remote ", " RoboCopy (all purpose)", and others); "start with" should be "starts with" in *Get IP Email*, whose scroll-up hint also appears twice; *Close any confidential* has no group, so it is alone in "General". | Tidy in Clippi Editor. |

### Changes made in version 1.2.0 (this review cycle)

**Clippi**
- **Literal text:** the optional `rawText` field lets a button contain a literal `\n` or `\t`, such as a UNC path.
- **Optional second prompt:** it can be left empty, and its box is labeled "(optional)".
- **Paste target:** Paste never targets Clippi Editor (or the Notepad Clippi opened instead).
- **Labels:** an `&` in a button label is shown as `&` rather than an underline.
- **HTML spacing:** the HTML version keeps leading spaces and runs of spaces (`&nbsp;`).
- **Trailing line breaks** are not copied, in any of the three formats.
- **About link:** measured in the font it is drawn in.
- **Focus:** reloading keeps the focus in the groups list.
- **Editor tracking:** a late "editor closed" message from an older editor is ignored.
- **DPI and monitors:** per-monitor DPI support, and the wrap height uses the monitor the window is on.

**Clippi Editor**
- **WYSIWYG box:** "Text to paste" is now a Rich Edit box with a formatting toolbar.
- **Formatted paste:** HTML and screenshots paste with formatting, and Ctrl+Shift+V pastes plain text.
- **Files:** insert from an RTF/HTML file; export RTF/HTML.
- **Saving:** formatted buttons are saved as plain + `%RTF%` + `%HTML%`, and unedited buttons are saved exactly as loaded.
- **Attachments panel removed:** pictures now sit inline in the text instead of as `[[Picture N]]` tags.
- **Warnings and file names:** the status line warns about placeholders with mixed formatting, and export file names are made safe for Windows.

---

## 9. Testing performed

| Test | Result |
|---|---|
| Build both programs with MinGW-w64 g++ 13, `-std=c++17 -Wall` | No errors or warnings |
| Converter tests (RTF ↔ document ↔ HTML ↔ plain): 37 checks, run under AddressSanitizer and UndefinedBehaviorSanitizer | All pass |
| Button-file tests: 12 checks, covering the current file (19 buttons) loaded and saved unedited and re-saved after editing, an older `%IMG%` button, an `%HTML%`-only button, `rawText` and split-placeholder detection | All pass; your file is saved byte-for-byte unchanged |
| Rich Edit box, toolbar, clipboard paste, dialogs on real Windows | **Not yet tested** (see finding 2) |

---

## 10. Checklist for the first run on Windows

1. Build both projects and put the two .exe files in one folder.
2. Start Clippi. The groups and buttons from your file should appear. Press a button with a prompt, then Paste into Notepad.
3. Press **Edit Clips**. Pick a button, select a word, press **B**, then **Save**. The list should show `[formatted]`.
4. Back in Clippi (it reloads automatically), press that button and paste into **Outlook** (bold), **Teams** (bold) and **Notepad** (plain).
5. In the editor, copy some formatted text from Teams or a web page and paste it into the box with Ctrl+V. Then paste a screenshot (Win+Shift+S, then Ctrl+V).
6. Insert a link (Link…) and a picture (Picture…), save, and paste the button into Outlook and Teams.
7. Move the Clippi window to a monitor with different scaling; it should re-lay itself out sharply.
8. Check Explorer › Properties › Details for both .exe files after fixing finding 1.
