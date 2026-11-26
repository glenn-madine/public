# Clippi – Clipboard Buttons

**Version 1.2.1** · Glenn Madine · November 26, 2026 · Pure Win32 / C++17 · Windows 10 and 11

Clippi is a small Windows tool that keeps canned support text one click away. Each button copies a prepared message to the clipboard: a chat reply, a ticket note, a meeting-request body or a command line. Before copying, it can ask for a value such as the application name and fill it in. **Clippi Editor** is the companion program used to create and edit those buttons, including formatted text, tables, links and pictures.

Clippi only copies text to the clipboard. It never runs the commands it holds; the Paste button only presses Ctrl+V in the window you were using.

---

## 1. Contents of the package

| File | Lines | Purpose |
|---|---|---|
| `ClipBoardButtons.cpp` | 1,448 | Clippi, the main program |
| `ClippiEditor.cpp` | 1,579 | Clippi Editor, the helper program for editing the buttons |
| `ClippiCommon.h` | 410 | Shared by both: JSON parser, file and text helpers, Base64, PNG size, in-memory dialog template |
| `ClippiRichText.h` | 1,667 | Shared by both: the formatted-text model and its RTF, HTML and plain-text converters, picture helpers |
| `resource.h` | 15 | Shared version number, author, date and resource IDs |
| `ClipBoardButtons.rc` | 37 | Clippi resources: icon, embedded default button file, version info |
| `ClippiEditor.rc` | 33 | Clippi Editor resources: icon, version info |
| `ClipBoardButtonDefinitions.json` | 121 | The default buttons (19 buttons in 7 groups), embedded in Clippi's .exe |
| `Clippi.ico` | – | Program icon for both programs (9 sizes, 16 to 96 pixels) |

---

## 2. Building

### Visual Studio 2019 / 2022

Create two projects (**Windows Desktop Application**, empty) in one solution, for example `ClipBoardButtons` and `ClippiEditor`. Keep all files in one folder.

| Project | Add these files |
|---|---|
| ClipBoardButtons | `ClipBoardButtons.cpp`, `ClippiCommon.h`, `ClippiRichText.h`, `resource.h`, `ClipBoardButtons.rc`, `Clippi.ico`, `ClipBoardButtonDefinitions.json` |
| ClippiEditor | `ClippiEditor.cpp`, `ClippiCommon.h`, `ClippiRichText.h`, `resource.h`, `ClippiEditor.rc`, `Clippi.ico` |

Project settings, for both projects and all configurations:

- **C/C++ › Language › C++ Language Standard:** ISO C++17 (or later)
- **Advanced › Character Set:** Use Unicode Character Set (the code also defines `UNICODE` itself)
- **Linker › System › SubSystem:** Windows (`/SUBSYSTEM:WINDOWS`); the entry point is `wWinMain`
- **Libraries:** nothing to add. The sources link what they need with `#pragma comment(lib, …)`: comctl32, comdlg32, shell32, advapi32, ole32, gdi32, windowscodecs and uuid. They also request Common Controls 6 through a `/manifestdependency` pragma.

The two headers are header-only (`inline`), so they need no settings; adding them to the projects only makes them visible in Solution Explorer.

Put **ClippiEditor.exe in the same folder as ClipBoardButtons.exe**. Clippi's *Edit Clips* button looks for the editor there and falls back to Notepad if it isn't found.

### MinGW-w64 (optional; used for the test builds)

```
x86_64-w64-mingw32-windres ClipBoardButtons.rc -O coff -o ClipBoardButtons.res.o
x86_64-w64-mingw32-g++ -std=c++17 -municode -mwindows -O2 ClipBoardButtons.cpp ClipBoardButtons.res.o -o ClipBoardButtons.exe -lcomctl32 -lshell32 -ladvapi32 -lole32 -lwindowscodecs -luuid -lgdi32
x86_64-w64-mingw32-windres ClippiEditor.rc -O coff -o ClippiEditor.res.o
x86_64-w64-mingw32-g++ -std=c++17 -municode -mwindows -O2 ClippiEditor.cpp ClippiEditor.res.o -o ClippiEditor.exe -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lwindowscodecs -luuid -lgdi32
```

Both programs compile with `-Wall -Wextra` without errors or warnings.

### Changing the version

Edit `resource.h`. Explorer shows the three numbers (Properties › Details); the title bar and About box show the strings, so keep them in step:

```
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 2
#define APP_VERSION_PATCH 1
#define APP_VERSION_STR   "1.2.1"
#define APP_VERSION_WSTR  L"1.2.1"
```

---

## 3. First run and where files live

| Item | Location |
|---|---|
| Button file | `%USERPROFILE%\Clippi_Storage\ClipBoardButtonDefinitions.json` |
| Backup made by the editor | same folder, `ClipBoardButtonDefinitions.json.bak` |
| Settings | `HKCU\Software\ClipBoardButtons`: `AlwaysOnTop` (DWORD), `LastGroup` (string) |

On first run, Clippi creates `Clippi_Storage` and writes the default button file embedded in the .exe. An existing file is **never overwritten** (`CREATE_NEW`), so your edits survive upgrades. Rebuilding Clippi with a new default file therefore doesn't change a copy already in `Clippi_Storage`; replace that copy yourself if you want it. Both programs read button files of up to 32 MB.

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
- **Labels:** an `&` in a button label is shown as `&`.
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
| Ctrl+V | Paste with formatting (see below) |
| Ctrl+Shift+V | Paste plain text only |
| Right-click | Undo, Cut, Copy, Paste, Paste as plain text, Select All. *Paste* is greyed out when there is nothing it can paste |

**What pasting keeps**

- **From Word and Outlook:** their RTF is pasted as it is.
- **From Teams, browsers and web mail:** the HTML is converted. Bold, italic, underline, strikeout, fonts, sizes, colors, highlights, links, bulleted and numbered lists, **tables**, alignment, **paragraph spacing** (CSS margins) and **indentation** (margins, block quotes, nested lists) come through.
- **Pictures:** embedded pictures and pictures in **local files** are kept: `file:` links, and pictures next to an .html file, such as the `_files` folder Word's "Save as Web Page" makes. Pictures that are only a web address are **not downloaded**; the status line says how many were left out.
- **Screenshots** and other copied pictures are added as PNG.

**How the editor saves**

- **Plain text:** a button with no formatting is saved as plain text, exactly as before.
- **Formatted buttons** are saved as plain text (for Notepad and other programs; links written as `text <url>`) plus one `%RTF:…%` token. Clippi makes the HTML version for Teams and browsers from that RTF when the button is clicked, so **each picture is stored once**.
- **Unchanged buttons:** a button whose text you didn't change is saved byte-for-byte as it was loaded.
- **Older buttons** with `%IMG%` pictures, `%HTML%` only, or `%RTF%` plus `%HTML%` open formatted and are saved as text + `%RTF%` once their text is edited.
- **Backup:** the previous file is copied to `.bak` before every save, and the save goes through a temporary file, so a failed save never damages the original.
- **Placeholder warning:** if `%XXX%` or `%YYY%` has mixed formatting inside it (for example half bold), Clippi can't fill it in the formatted version, so the status line warns after saving.

---

## 6. The button file (JSON)

The file is a JSON array of objects, one per button, in UTF-8 (UTF-16LE with a BOM and ANSI are also read). The parser follows JSON strictly for numbers and limits nesting to 100 levels; raw tabs inside strings are tolerated. Fields Clippi doesn't know are kept by the editor.

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
| `%RTF:<base64>%` | RTF document for Outlook and Word. Without `%HTML%`, Clippi also makes the HTML version from it |
| `%HTML:<base64>%` | Ready-made UTF-8 HTML for Teams and browsers. Optional; older files may have it, and it takes priority |
| `%IMG:<base64 PNG>%` | Picture; Clippi builds RTF and HTML around it (Calibri 11 pt). Older format, still supported |

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
| `Rich Text Format` | Outlook, Word | The `%RTF%` token, or built from text + `%IMG%` |
| `HTML Format` (CF_HTML) | Teams, browsers, web mail | The `%HTML%` token; otherwise made from the RTF, or built from text + `%IMG%` |

---

## 7. How the code is organized

Both programs are single-file, pure Win32 applications with no external libraries. They share `resource.h` and two header-only libraries in `namespace clippi`.

### ClippiCommon.h (shared)

| Section | Contents |
|---|---|
| JSON | `JsonValue`, `JsonError`, `JsonParser`: strict number grammar, nesting limit (100), errors report line and column |
| Text | `ReplaceAll`, `Trim`, `ToUtf8`, `FromBytes` |
| Files | `kJsonFileName`, `StorageFolder`, `FileExists`, `ReadFileBytes` (32 MB limit), `BytesToText` (UTF-8, UTF-16LE, ANSI) |
| Data | `Base64Encode`, `Base64Decode`, `IsPng`, `PngSize` |
| Dialogs | `DialogTemplate`: builds dialog templates in memory, so no dialog resources are needed |

### ClippiRichText.h (shared)

| Section | Contents |
|---|---|
| Pictures | WIC: any picture file or bytes → PNG; GDI: metafiles (EMF/WMF) and DIBs drawn into a PNG; PNG/JPEG size |
| Document model | `Fmt`, `Run` (text, line break, picture, cell end), `Para` (alignment, spacing before/after, indent, table row), `Doc` |
| Writers | `DocToRtf` (tables as `\trowd…\row`, `\sb`/`\sa`/`\li`), `DocToHtml` (one `<div>` per paragraph, bordered `<table>`), `DocToPlain` (cells as tabs), `IsPlainDoc` |
| Readers | `RtfToDoc` (what Rich Edit, Word and Outlook write, including tables and paragraph spacing), `HtmlToDoc` (browser-style whitespace, entities, CSS, lists, tables, margins, embedded and local pictures) |

### ClipBoardButtons.cpp (Clippi)

| Section | Contents |
|---|---|
| Button definitions | `ButtonDef`, `LoadDefinitions`, `FindJsonPath`, `CreateDefaultJsonFile` (writes the embedded RCDATA once) |
| Text | `ReplacePlaceholders` (one pass, so typed text is never re-replaced), `PrepareText` (`\n`/`\t` conversion, CRLF, placeholders) |
| Clipboard | `SplitSegments` (finds the tokens), `BuildRtfDocument` / `BuildHtmlFragment` (for `%IMG%`), RTF → HTML via `ClippiRichText.h`, `WrapCfHtml` (CF_HTML byte offsets), `CopyToClipboard` |
| Window | Groups pane, button grid (`LayoutWindow`, `ShowGroupButtons`), Always on top, About, input dialog |
| Editor tracking | `OpenEditor`, a thread-pool wait on the editor process (tagged with a launch number), reload when the file's time or size changes |
| Paste | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` remembers the previous window; `PasteIntoTarget` waits for modifier keys to be released and sends Ctrl+V with `SendInput` |
| DPI | Per-monitor v2 via run-time lookup of the `…ForDpi` APIs; `WM_DPICHANGED` rebuilds fonts and layout |

### ClippiEditor.cpp (Clippi Editor)

| Section | Contents |
|---|---|
| JSON writer | Pretty-printing writer (4-space indent) |
| Clip model | `ClipFromJson`, `ClipToJson` (unchanged clips are written exactly as loaded; formatted clips as text + `%RTF%`; `rawText`; unknown fields kept) |
| Rich Edit box | Stream in/out, default Calibri 11 pt with plain paragraphs, `CommitBox` (decides plain or formatted), toolbar actions, special paste (HTML, pictures), context menu |
| Window | Layout, list and field handling, accelerators, save with temporary file + `.bak` |

### Data flow

```
 Clippi Editor                    JSON file              Clippi (button clicked)
 ─────────────                    ─────────              ───────────────────────
 Rich Edit ─ RTF ─┬─► plain text ─┐                     ┌─► CF_UNICODETEXT (plain)
                  │               ├─► pasteText ─► fill ┼─► Rich Text Format (RTF)
                  └─► %RTF:…%  ───┘       %XXX% / %YYY% └─► HTML Format (made from the RTF)
```

---

## 8. Final analysis

### What is done well

- **Safe file handling.** The editor writes to a temporary file, keeps a `.bak` and then replaces the original. Clippi never overwrites an existing button file and reloads only when the file's time or size has changed.
- **Correct clipboard formats.** CF_HTML offsets are counted in UTF-8 bytes; RTF pictures use `\pngblip` with pixel and twip sizes; clipboard memory is freed on failure; `OpenClipboard` is retried.
- **Placeholders done carefully.** One-pass replacement, filled in after the `\n` conversion so typed text is pasted verbatim, and escaped separately for RTF and HTML. The HTML version is made from the RTF after the values are filled in.
- **No surprises for existing data.** Unedited buttons are saved byte-for-byte; unknown JSON fields are kept; older `%IMG%` and `%HTML%` buttons still work.
- **One copy of shared code.** Both programs read the button file through the same parser and the same size limit, and use the same converters.
- **Defensive parsing.** Strict JSON numbers, a nesting limit instead of a stack overflow, a 32 MB file limit, and picture sizes capped before drawing.
- **No dependencies.** Pure Win32, no runtime installers, no dialog resources.

### Remaining findings

| # | Severity | Finding | Recommendation |
|---|---|---|---|
| 1 | Medium | **Not yet run on Windows.** Both programs compile and link with all resources, and the converters and file handling are tested, but the user interface has not run. That covers Clippi Editor's Rich Edit box, toolbar, formatted paste and dialogs, and Clippi building the HTML version from RTF when a button is clicked. | Run the checklist in section 10 once on a real PC. |
| 2 | Low | Clippi Editor is only system-DPI aware (`SetProcessDPIAware`), so it looks blurry on a second monitor with different scaling. Clippi itself is per-monitor aware. | Port Clippi's DPI helpers (`SetDpiAwareness`, `WM_DPICHANGED`) to the editor. |
| 3 | Low | `Clippi.ico` has sizes up to 96 × 96 but no 256 × 256 image, so Explorer's *Large* and *Extra large icons* views enlarge the 96-pixel image. | Add a 256 × 256 (PNG) image to the icon. |
| 4 | Info | The default button file is embedded in Clippi's .exe. The *Service Desk* link still carries user- and session-style parameters (`ovuser` with an organization ID and `Billy.Bob@Acme.com`, `TeamsCID`, `CT`, `clickparams`). The values have already been replaced with bogus ones. | Optional: end the link after `viewid=…`; it opens the same page. |
| 5 | Info | By design, pictures in pasted HTML that are only a web address are not downloaded. | Copy such pictures as pictures, or save them and use *Picture…*. |
| 6 | Info | HTML conversion is close but not exact. Tables get equal-width columns with thin grey borders, and merged cells, line spacing and background shading are not kept. | Expected; check complex content after pasting. |
| 7 | Info | Button data left as chosen: *Get IP Email* says "start with" (twice) and repeats the scroll-up line; *Close any confidential* has no group. Lines 70–71 of the JSON file are indented with tabs, which is harmless; the editor rewrites them with spaces on its next save. | None required. |

### Changes in this review cycle

**Version 1.2.0**

- **Clippi:**
  - The optional `rawText` field allows literal `\n` and `\t`.
  - The second prompt can be left empty.
  - Paste never targets the editor.
  - An `&` in a label displays correctly.
  - The HTML version keeps leading spaces and runs of spaces.
  - Trailing line breaks are not copied.
  - The About link is measured in the right font.
  - Focus stays in the groups list on reload.
  - The editor-closed race is fixed.
  - Clippi is per-monitor DPI aware.
- **Clippi Editor:**
  - The WYSIWYG Rich Edit box with a formatting toolbar.
  - Formatted paste.
  - Insert from and export to RTF and HTML files.
  - The placeholder warning.
- **resource.h:** the version numbers match the version strings.
- **Button data:** the typo, stray spaces and hidden U+202F characters are fixed.

**Version 1.2.1**

- **Shared code:** moved into `ClippiCommon.h` and `ClippiRichText.h`, so it exists once.
- **One file-size limit:** both programs read button files of up to 32 MB.
- **Strict JSON parser:** strict number grammar and a nesting limit of 100.
- **Paste menu item:** in the editor's right-click menu, *Paste* is enabled only when something can be pasted.
- **Pictures stored once:** formatted buttons are saved as text + `%RTF%`, and Clippi makes the HTML. Picture-heavy buttons are about 40% smaller, and ready-made RTF without HTML (such as a Word signature) now pastes formatted into Teams.
- **HTML conversion:** tables, paragraph spacing, indentation and pictures in local files are now kept.
- **Clippi:** initializes COM for picture conversion, and its own RTF escape is renamed `RtfEscapeLines`.

---

## 9. Testing performed

| Test | Result |
|---|---|
| Build both programs with MinGW-w64 g++ 13, `-std=c++17 -Wall -Wextra`, with resources compiled from the `.rc` files and the real `Clippi.ico` | No errors or warnings; both .exe files report file version 1.2.1.0; the default button file is embedded |
| Converter tests (RTF ↔ document ↔ HTML ↔ plain: formatting, links, lists, pictures, encodings, whitespace): 37 checks | All pass |
| New feature tests (tables, missing table tags, nested tables, Rich Edit table RTF, margins, indentation, local and `file:` pictures, RTF → HTML with filled-in placeholders): 28 checks | All pass |
| Button-file tests (current 19-button file round trip, older `%IMG%` and `%HTML%` buttons, RTF-only saving, `rawText`, split placeholders): 12 checks | All pass; the file is saved byte-for-byte unchanged |
| JSON parser tests (valid and invalid numbers, nesting limit, 200,000-deep input, raw tabs): 20 checks | All pass |
| All native tests run under AddressSanitizer and UndefinedBehaviorSanitizer | No memory or undefined-behavior errors |
| User interface on real Windows | **Not yet tested** (see finding 1) |

---

## 10. Checklist for the first run on Windows

1. Build both projects and put the two .exe files in one folder.
2. Start Clippi. The groups and buttons from your file should appear. Press a button with a prompt, then Paste into Notepad.
3. Press **Edit Clips**. Pick a button, select a word, press **B**, then **Save**. The list should show `[formatted]`.
4. Back in Clippi (it reloads automatically), press that button and paste into **Outlook** (bold, from the RTF), **Teams** (bold, from the HTML Clippi makes) and **Notepad** (plain).
5. In the editor, copy formatted text and a table from a web page or Teams and paste them with Ctrl+V. Then paste a screenshot (Win+Shift+S, then Ctrl+V). Right-click with an empty clipboard: *Paste* should be greyed out.
6. Insert a link (Link…) and a picture (Picture…), save, and paste the button into Outlook and Teams.
7. Move the Clippi window to a monitor with different scaling; it should re-lay itself out sharply.
8. Check Explorer › Properties › Details for both .exe files: File version and Product version should read 1.2.1.
