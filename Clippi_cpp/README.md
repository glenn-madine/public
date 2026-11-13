# Clippi — Clipboard Buttons

**Version 1.0.0** · Win32 desktop application · C++17 · Visual Studio 2022

Clippi is a small Windows tool that shows a column of buttons, each holding a ready-made message. Click a button and its text is copied to the clipboard, ready to paste into a chat, ticket or email. Messages can ask for a value first (for example, a software name) and can include a picture such as a company logo.

The buttons are defined in a JSON file, so you can add, change or remove messages without rebuilding the program.

## Features

- **One click, one message.** Each button copies its text to the clipboard.
- **Fill-in values.** A message can contain `%XXX%`. Clippi asks for the value first and inserts it.
- **Pictures in messages.** A message can contain a PNG image (for example, a signature logo). It pastes with the picture into Outlook, Word and Teams, and as text only into plain-text apps.
- **Always on top.** Keeps the window above all other windows while you work. The choice is remembered.
- **Edit in place.** The **Edit Clips** button opens the button file in Notepad. When you close Notepad, Clippi reloads the buttons if you saved changes.
- **Reload without restarting.** Edit the JSON file and press F5.
- **Self-contained.** One .exe with no installer and no external libraries. The default button file is built into the .exe and written out on first run.

## Quick start

1. Open `ClipBoardButtons.sln` in Visual Studio 2022.
2. Make sure `Clippi.ico` is in the project folder.
3. Choose **Release | x64** and build (**Ctrl+Shift+B**).
4. Run `x64\Release\ClipBoardButtons.exe`.

On first run, Clippi creates `%USERPROFILE%\Clippi_Storage\ClipBoardButtonDefinitions.json` from its built-in defaults. To change your buttons, click **Edit Clips**, edit and save the file, and close Notepad. The buttons reload automatically.

## Requirements

| Item | Requirement |
|---|---|
| Operating system | Windows 10 or 11 (Windows 7 and later should work) |
| To build | Visual Studio 2022 with the "Desktop development with C++" workload (toolset v143, Windows 10/11 SDK) |
| Alternative build | MinGW-w64 (g++ and windres) |
| Libraries | Windows only: user32, gdi32, comctl32, shell32, advapi32 |

## Project files

| File | Purpose |
|---|---|
| `ClipBoardButtons.cpp` | The entire program. |
| `ClipBoardButtons.rc` | Resources: the icon, the default JSON (embedded in the .exe) and the version information. |
| `resource.h` | Resource IDs and the version number. |
| `ClipBoardButtonDefinitions.json` | The default buttons that are built into the .exe. |
| `Clippi.ico` | Application icon (supply your own). |
| `L3Harris_logo.png` | Source copy of the logo used in the Signature button. Not needed at run time. |
| `ClipBoardButtons.vcxproj` / `.sln` | Visual Studio project and solution. |

## Configuring the buttons

### Where the file lives

Clippi always reads:

```
%USERPROFILE%\Clippi_Storage\ClipBoardButtonDefinitions.json
```

The **Edit Clips** button opens this file in Notepad. If the folder or file does not exist, Clippi creates it from the copy built into the .exe. An existing file is **never overwritten**, so your edits are safe when you install a new version. To get a new version's default buttons, delete the file and restart Clippi.

### File format

The file is a JSON array. Each object in the array becomes one button, in order.

```json
[
    {
        "buttonText": "Are you ready",
        "pasteText": "Are you ready for me to remote into your PC and install %XXX%?\\n\\nNOTE: Please close any sensitive information first.",
        "inputBoxTitle": "Software Name"
    },
    {
        "buttonText": "Done",
        "pasteText": "I have now disconnected. Have a great day!",
        "inputBoxTitle": ""
    }
]
```

| Field | Required | Description |
|---|---|---|
| `buttonText` | Yes | The label on the button. If missing, the button shows "(unnamed)". |
| `pasteText` | Yes | The text copied to the clipboard. See the markup table below. |
| `inputBoxTitle` | No | If not empty, Clippi asks for a value before copying. This is the title of that prompt. |

Other fields are ignored. The file must be strict JSON: no comments and no trailing commas. UTF-8 (with or without a BOM) and UTF-16 with a BOM are accepted.

### pasteText markup

| Markup | Result |
|---|---|
| `\\n` | Line break. Written with two backslashes in the JSON file. |
| `\\t` | Tab. |
| `%XXX%` | Replaced by the value typed into the prompt. Every occurrence is replaced. |
| `%IMG:<base64>%` | A PNG picture, written as base64 text. See "Adding a picture" below. |

The standard JSON escapes (`\n`, `\t`, `\"`, `\\`, `\uXXXX`) also work.

### Adding a picture

To put a picture in a message, convert a PNG file to base64 and place it inside `%IMG:` and `%`. In PowerShell:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("C:\path\logo.png")) | Set-Clipboard
```

Then paste it into the JSON, for example:

```json
"pasteText": "Glenn Madine\\nSomewhere NY USA\\n%IMG:iVBORxxKGgo...%"
```

Keep pictures small (a few KB). Reducing the PNG to 64 or 256 colors usually shrinks it a lot with no visible difference.

When a message contains a picture, Clippi places three versions on the clipboard and the target app picks the best one it supports:

| Clipboard format | Used by | What pastes |
|---|---|---|
| Rich Text Format | Outlook, Word | Text and picture |
| HTML Format | Teams, browsers, web mail | Text and picture |
| Unicode text | Notepad, terminals, plain-text fields | Text only |

Text in the rich versions uses Calibri 11 pt. Messages without a picture copy plain text only.

## Using Clippi

### The window

- **Message buttons:** one per JSON entry, all the same width and stretched to fill the window. The window is wide enough for the longest label. If there are too many buttons to fit in 80% of the screen height, they wrap into additional columns.
- **Always on top:** checkbox, bottom left.
- **Edit Clips:** button, bottom right. Opens the button file in Notepad (see below).
- **Exit:** button, bottom right.
- **Status line:** shows how many buttons loaded, or what was last copied.
- **Title bar:** "Clippi" and the version number.

### Prompting for a value

For a button with an `inputBoxTitle`, a small box opens asking for the value. The box remembers the last value you typed for that prompt until Clippi is closed. OK is refused (with a beep) if the box is empty. Cancel copies nothing.

### Editing your clips

1. Click **Edit Clips**. The button file opens in Notepad.
2. Make your changes and save (**Ctrl+S**).
3. Close Notepad. If the file changed, Clippi reloads the buttons and shows "Clips updated: N buttons loaded."

Clippi also checks the file whenever you switch back to its window. This catches changes saved in another editor, or in Windows 11 Notepad when the file opens as a tab in a Notepad window that was already open. If the file has a mistake, an error box shows the line and column, and the previous buttons stay in place until you fix it and save again.

### Keyboard

| Key | Action |
|---|---|
| Tab / Shift+Tab | Move between buttons |
| Enter or Space | Press the selected button |
| F5 | Reload the JSON file |
| Alt+T | Toggle Always on top |
| Alt+E | Edit Clips |
| Alt+X | Exit |

The window's title-bar menu (click the icon or press Alt+Space) also has **Always on Top** and **Reload Buttons**.

### Status messages

| Message | Meaning |
|---|---|
| `N buttons loaded.  F5 = reload` | The file loaded successfully. |
| `Copied: <button>` | That button's text is on the clipboard. |
| `Cancelled.` | The prompt was cancelled; nothing was copied. |
| `Editing clips... buttons reload when Notepad is closed.` | Edit Clips opened Notepad. |
| `Clips updated: N buttons loaded.` | The file was changed and the buttons were reloaded. |
| `Editor closed. No changes.` | Notepad was closed without saving changes. |
| `Editing in Notepad... changes load when you return to Clippi.` | The file opened in a Notepad window that was already open; changes load when you switch back to Clippi. |
| `No buttons defined in the JSON file.` | The file is valid but the array is empty. |
| `Failed to load button definitions.` | An error box explained the problem; the previous buttons are kept. |

## Settings and storage

| What | Where |
|---|---|
| Button definitions | `%USERPROFILE%\Clippi_Storage\ClipBoardButtonDefinitions.json` |
| Always on top | Registry: `HKEY_CURRENT_USER\Software\ClipBoardButtons`, value `AlwaysOnTop` (DWORD, 1 = on) |
| Last values typed in prompts | Memory only; cleared when Clippi closes |

Clippi needs no administrator rights. It writes only to the user's profile folder and the user's registry hive.

## Building

### Visual Studio

Open `ClipBoardButtons.sln`, pick a configuration (Debug/Release, x64/x86) and build. The project compiles the .rc file automatically. Output goes to `x64\Release\` (or the matching folder).

### Command line (MSBuild)

From a **Developer Command Prompt for VS 2022**, in the project folder:

```bat
msbuild ClipBoardButtons.sln /p:Configuration=Release /p:Platform=x64
```

### Command line (cl.exe)

From an **x64 Native Tools Command Prompt for VS 2022**:

```bat
rc ClipBoardButtons.rc
cl /nologo /EHsc /std:c++17 /O2 /W4 /DUNICODE /D_UNICODE ClipBoardButtons.cpp ClipBoardButtons.res /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib advapi32.lib user32.lib gdi32.lib
```

### MinGW-w64

```bat
windres ClipBoardButtons.rc -O coff -o res.o
g++ -std=c++17 -O2 -municode -mwindows ClipBoardButtons.cpp res.o -o ClipBoardButtons.exe -lcomctl32 -lshell32 -ladvapi32 -lgdi32
```

MinGW ignores the `#pragma comment` lines, so the modern-control manifest is not applied and buttons use the classic Windows look.

### Changing the version

Edit the five `APP_VERSION_*` lines in `resource.h` (the three numbers and the two strings must match) and rebuild. The title bar and the .exe's Properties > Details tab both update.

## How the code works

### Code map

`ClipBoardButtons.cpp` is organized top to bottom as follows. Everything except `wWinMain` is inside an anonymous namespace.

| Section | Main functions / types | Responsibility |
|---|---|---|
| Constants | `kJsonFileName`, `kPlaceholder`, `kAppTitle`, control and menu IDs | Names, IDs and window styles used throughout. |
| JSON parser | `JsonValue`, `JsonParser`, `JsonError` | Small recursive-descent parser. Errors report line and column. |
| Button definitions | `ButtonDef`, `ReadTextFile`, `LoadDefinitions` | Reads the file (UTF-8/UTF-16) and builds the button list. |
| Storage | `StorageFolder`, `CreateDefaultJsonFile`, `FindJsonPath` | Locates the JSON file and creates it from the embedded resource on first run. |
| Text helpers | `ReplaceAll`, `PrepareText`, `Trim` | Turns `\\n`/`\\t` into real characters, normalizes line breaks to CRLF, inserts the prompt value. |
| Images and clipboard | `SplitSegments`, `Base64Decode`, `PngSize`, `BuildRtfClipboard`, `BuildHtmlClipboard`, `CopyToClipboard` | Separates text from `%IMG:%` pictures and places text, RTF and HTML on the clipboard. |
| Input box | `DialogTemplate`, `InputDlgProc`, `PromptForInput` | Builds the prompt dialog in memory (no dialog resource needed). |
| Main window | `AppState g`, `LayoutButtons`, `ReloadDefinitions`, `OnButtonClicked` | Creates and arranges controls; handles clicks. |
| Edit Clips | `OpenEditor`, `OnEditorExited`, `OnEditorClosed`, `StopWatchingEditor`, `GetFileStamp`, `FileChangedSinceLoad` | Opens the file in Notepad, waits for Notepad to close, and reloads if the file changed. |
| Always on top | `LoadTopmostSetting`, `SaveTopmostSetting`, `SetTopmost` | Keeps the checkbox, menu check mark and registry value in sync. |
| Window procedure | `WndProc` | Creates controls on `WM_CREATE`; routes commands and menu items; handles the editor-closed and file-check messages and `WM_ACTIVATEAPP`. |
| Entry point | `wWinMain` | DPI setup, font and icon, window creation, message loop. |

### Startup sequence

1. `wWinMain` marks the process DPI-aware, reads the screen DPI and creates the UI font.
2. The window class is registered with the icon from the resources.
3. The main window is created. Its `WM_CREATE` handler creates the status line, Edit Clips and Exit buttons, checkbox and menu items.
4. `ReloadDefinitions` calls `FindJsonPath` (creating the file on first run), then `LoadDefinitions`, then `LayoutButtons`.
5. The saved Always-on-top setting is applied.
6. The message loop runs. It checks the F5 accelerator first, then `IsDialogMessage` for keyboard navigation.

### What happens when a button is clicked

1. `WndProc` receives `BN_CLICKED`. The button's control ID minus 1000 is its index in the list.
2. `OnButtonClicked` shows the prompt if the button has an `inputBoxTitle`.
3. `PrepareText` converts `\\n`/`\\t`, normalizes line breaks and inserts the value. The value goes in last, so it is pasted exactly as typed.
4. `CopyToClipboard` calls `SplitSegments` to find pictures. It always puts Unicode text on the clipboard; if there is a picture it also adds RTF (picture as a hex-encoded `\pngblip`) and HTML (picture as a `data:` URI with CF_HTML byte offsets).
5. The status line shows "Copied: …".

### Implementation notes

- **No external dependencies.** JSON parsing, base64 decoding and the dialog template are all hand-written.
- **Embedded default file.** `ClipBoardButtons.rc` includes the JSON as an `RCDATA` resource. `CreateDefaultJsonFile` writes it with `CREATE_NEW`, which guarantees an existing file is never overwritten.
- **DPI.** The process is system-DPI aware and all sizes are scaled from 96 DPI with `Scale()`.
- **Layout.** The window is not resizable; `LayoutButtons` computes the exact client size and resizes the window to fit after every load.
- **Detecting edits.** When the buttons load, Clippi records the file's last-write time and size. **Edit Clips** starts Notepad with `ShellExecuteEx` and registers a thread-pool wait (`RegisterWaitForSingleObject`) on the Notepad process. When the process exits, the wait callback posts `WM_APP_EDITOR_CLOSED` to the window, which compares the file with the recorded values and reloads if they differ. `WM_ACTIVATEAPP` posts `WM_APP_CHECK_FILE` to run the same check whenever Clippi is reactivated. A file that fails to load is recorded too, so its error is shown once per save rather than on every activation.
- **Clipboard.** Opening the clipboard is retried up to 10 times, 30 ms apart, in case another app is briefly holding it.
- **Visual styles.** A `#pragma comment(linker, ...)` manifest dependency enables Common Controls 6 (modern button look) under MSVC.

## Troubleshooting

| Problem | Fix |
|---|---|
| "JSON error … (line N, column M)" | The file has a syntax error at that position. Common causes are a missing comma between entries, a trailing comma, or an unescaped `"` inside text. |
| New buttons in a new build don't appear | Your existing file in `Clippi_Storage` is kept. Copy the new entries into it, or delete the file and restart. |
| Buttons didn't update after editing | Make sure the file was saved. Switch back to Clippi or press F5 to reload. |
| Picture doesn't paste | The target is a plain-text field, or the `%IMG:` data isn't a valid PNG (invalid pictures are silently dropped). |
| Picture pastes as an attachment in Teams | Teams sometimes uploads pasted pictures. This is Teams behavior. |
| Rich text looks different from the rest of my email | Pasted rich text uses Calibri 11 pt. Change `kRichFontCss`, `kRichFontRtf` and `kRichFontHalfPoints` in the code to match. |
| `&` in a button label shows as an underline | Windows treats `&` as a keyboard shortcut marker. Use `&&` in `buttonText` to show a literal `&`. |
| `msbuild` or `cl` is "not recognized" | Use a Developer Command Prompt, or run `vcvars64.bat` first. |

## Known limitations

- A literal backslash followed by `n` or `t` (for example, `C:\new`) is always converted to a line break or tab.
- Values typed into prompts are not saved between sessions.
- The window is scaled for the primary display's DPI; on a second monitor with different scaling it may look slightly blurry.
- Pictures must be PNG.
- Edit Clips always uses Notepad. The `%IMG:` picture data appears as one very long line there.
- Esc does not close the window (by design, to avoid accidental exits).
