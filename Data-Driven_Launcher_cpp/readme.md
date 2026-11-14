# DDLaunch+ — Data-Driven Launcher and Editor

**DDLaunch+ 2.6.1** · **DDLaunch+ Editor 1.0.1** · Glenn Madine · Released 11/14/2026
Win32 C++ (Windows SDK) · nlohmann/json 3.12.0

DDLaunch+ is a small Windows launcher for the machines and resources you connect to every day. It shows a list of resources (hosts, drives or shell items), each with a **type** such as `RDP`, `SSH` or `HTTPS`. Double-clicking an entry runs the program or URL handler defined for that type. For a host that isn't in the list, type it in the **Host** box, pick an **Action**, and click **Go**.

The app has no built-in protocols. The list and the launch rules come from two JSON files: `connections.json` and `actionDefinitions.json`. The **DDLaunch+ Editor** (`DDLaunchEdit.exe`) edits both files through forms, so nobody has to hand-edit JSON. DDLaunch+ picks up saved changes as soon as you switch back to it.

---

## Contents

1. [The two programs](#the-two-programs)
2. [Features](#features)
3. [Requirements](#requirements)
4. [Building and installing](#building-and-installing)
5. [First run and the data folder](#first-run-and-the-data-folder)
6. [Configuration files](#configuration-files)
7. [Using DDLaunch+](#using-ddlaunch)
8. [How a launch works](#how-a-launch-works)
9. [Using the DDLaunch+ Editor](#using-the-ddlaunch-editor)
10. [About boxes](#about-boxes)
11. [Messages and troubleshooting](#messages-and-troubleshooting)
12. [Code walkthrough: DDLaunch.cpp](#code-walkthrough-ddlaunchcpp)
13. [Code walkthrough: DDLaunchEdit.cpp](#code-walkthrough-ddlauncheditcpp)
14. [Design notes](#design-notes)
15. [Known limitations](#known-limitations)
16. [Version history](#version-history)

---

## The two programs

| Program | File | What it does |
|---|---|---|
| **DDLaunch+** 2.6.1 | `DDLaunch.exe` (`DDLaunch.cpp`, ~2,500 lines) | Shows your resources and launches them; quick-launch row with host history |
| **DDLaunch+ Editor** 1.0.1 | `DDLaunchEdit.exe` (`DDLaunchEdit.cpp`, ~2,150 lines) | Form-based editor for `connections.json` and `actionDefinitions.json` |

The two programs share the data folder, the JSON formats, the case rules, and the rules for building a launch. They work independently. When `DDLaunchEdit.exe` sits next to `DDLaunch.exe`, DDLaunch+'s **Edit Connections** and **Edit Actions** buttons open the editor on the matching tab. Without it, they open Notepad.

---

## Features

### DDLaunch+

- **Data-driven.** Resources come from `connections.json` and launch rules from `actionDefinitions.json`. A new protocol is a JSON entry, not a code change.
- **Programs or URLs.** An action's command is either an executable (run with arguments) or a URL prefix of any scheme: `https://`, `ssh://`, `file:///`, `mailto:`, `ms-settings:` and so on.
- **Quick launch.** A Host box, an Action dropdown (sorted A to Z) and a **Go** button run any action on any host. They use the same rules and error messages as a list entry.
- **Host history.** The Host box remembers up to 25 hosts launched with Go, most recent first, each with its action. Shift+Delete removes an entry. The history survives restarts.
- **Live reload.** Edits are picked up when the window is next activated. Only files whose timestamp or size changed are re-read.
- **Error-tolerant.** If a file is invalid, the error is shown once and the last good data stays loaded.
- **Clear messages** for a missing or unknown type, an empty command, a failed launch (with the Win32 error text) and duplicate action names.
- **Sortable columns.** Sorting ignores case, and the sort order and selected row are kept across reloads.
- **Keyboard support.** Tab moves between controls and Enter launches. Alt+H, Alt+A and Alt+G jump to Host, Action and Go. Esc does nothing.
- **Per-Monitor V2 DPI awareness**, a remembered window position (kept on screen), and a **single instance**.
- **Run dialog** (same as Win+R), an **ad-hoc Remote Desktop** button, and an **About** box.

### DDLaunch+ Editor

- **Two tabs, one window.** Connections and Actions, each with a list and a details form. Edits apply as you type, and **Save** writes both files.
- **Guided fields.** Type is a dropdown of the defined actions (or type any name). Group offers the groups in use. Command has **Browse...** for picking a program.
- **Launches line.** It shows exactly what DDLaunch+ will run or open for the selected entry, using the same rules as DDLaunch+.
- **Problem checks.** An unknown type, missing host, unnamed or duplicate action, or missing command shows in a **Check** column, in red. Save lists the problems first.
- **Safe saving.** The previous file is kept as `.bak`. The new file is written to a temporary file and swapped in.
- **Round trip.** Key order, extra fields and the grouped file format are kept.
- **Rename help.** Renaming an action offers to update the connections that use it.
- **Filter, reorder, duplicate.** A filter box narrows the connections list. Move Up / Move Down set the order. Duplicate copies an entry.
- **Watches for outside edits.** A file changed elsewhere is reloaded, or the editor asks first if you have unsaved changes.
- **Single instance** with tab selection from the command line, DPI awareness, a remembered window, and an **About** box.

---

## Requirements

| Item | Detail |
|---|---|
| OS | Windows 10/11. Starts on older versions with reduced DPI support. |
| Compiler | Microsoft C++ (developed with 19.51), `/std:c++17` |
| SDK | Windows SDK (`windows.h`, `commctrl.h`, `commdlg.h`, `shellapi.h`, `shlwapi.h`, `shldisp.h`, `shlobj.h`) |
| Library | nlohmann/json 3.12.0 single header, shipped as `json.hpp` |
| Common Controls | Version 6, through each program's embedded manifest |

---

## Building and installing

### Build files

| File | Used by | Purpose |
|---|---|---|
| `DDLaunch.cpp` | DDLaunch+ | The launcher |
| `DDLaunch.rc` | DDLaunch+ | Icon, manifest (RT_MANIFEST id 1), version info 2.6.1.0 |
| `DDLaunch.manifest` | DDLaunch+ | Common Controls v6, Per-Monitor V2 DPI, `asInvoker`, supported Windows versions |
| `DDLaunchEdit.cpp` | Editor | The editor |
| `DDLaunchEdit.rc` | Editor | Icon, manifest, version info 1.0.1.0 |
| `DDLaunchEdit.manifest` | Editor | Common Controls v6, Per-Monitor V2 DPI, `asInvoker`, Windows 10/11 |
| `resource.h` | Both | Defines `IDI_ICON1` (101) |
| `DDLaunch.ico` | Both | Application icon |
| `json.hpp` | Both | nlohmann/json 3.12.0 |

### Command lines (Developer Command Prompt)

DDLaunch+:

```bat
rc /nologo DDLaunch.rc
CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunch.cpp DDLaunch.res /Fe:DDLaunch.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib
```

DDLaunch+ Editor:

```bat
rc /nologo DDLaunchEdit.rc
CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunchEdit.cpp DDLaunchEdit.res /Fe:DDLaunchEdit.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib comdlg32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib
```

### Visual Studio projects

- Add the program's `.rc` file to its project so the manifest, icon and version info are embedded.
- Set **Linker › Manifest File › Generate Manifest = No**. Each manifest must come only from the `.rc`. A second, linker-generated one makes the link fail with CVT1100 / LNK1123.
- Set **C/C++ › Language › C++ Language Standard = ISO C++17** (or later), and Character Set = Unicode.

### Installing

Copy `DDLaunch.exe` and `DDLaunchEdit.exe` into the **same folder**. Neither needs installing or administrator rights.

**Build self-check.** At startup DDLaunch+ checks that Common Controls v6 is loaded. If it isn't, the manifest wasn't embedded: a message goes to the debugger output, and debug builds also show a message box.

---

## First run and the data folder

All user data lives in `%USERPROFILE%\DDLaunch\` (for example `C:\Users\glenn\DDLaunch`). If `%USERPROFILE%` isn't set, the programs use the Profile known folder instead. If that can't be found either, they use the folder containing the `.exe`.

| File | Written by | Purpose |
|---|---|---|
| `actionDefinitions.json` | Created by DDLaunch+ if missing (RDP / SSH / HTTPS / HTTP); edited by the editor | Launch rules, one per type |
| `connections.json` | Created by DDLaunch+ if missing (`[]`); edited by the editor | The resources shown in the list |
| `settings.json` | DDLaunch+, on exit and after every Host history change | Window placement and Host history |
| `editor.json` | The editor, on exit | Editor window placement, last tab, test host |
| `*.json.bak` | The editor, on each save | The previous version of a saved file |

- DDLaunch+ creates the folder and the two config files whenever they're missing: at startup, on reload, and before opening a file for editing. Existing files are never overwritten.
- All files are **UTF-8** (with or without a BOM). Notepad's default UTF-8 encoding is correct.

---

## Configuration files

### `actionDefinitions.json`

This file must be a **top-level JSON array**. Each entry needs all three string fields:

| Field | Meaning |
|---|---|
| `action` | The type name. It's matched against a connection's `type`, ignoring case (`rdp`, `Rdp` and `RDP` are the same), and it's the name shown in DDLaunch+'s Action dropdown. |
| `command` | An executable path, or a URL prefix (see [How a launch works](#how-a-launch-works)). |
| `args` | For an executable: text placed **directly** in front of the host. Include any space you need, e.g. `" /v:"`. Ignored for URLs. |

Default content:

```json
[
    { "action": "RDP",   "args": " /v:", "command": "C:\\Windows\\System32\\mstsc.exe" },
    { "action": "SSH",   "args": " ",    "command": "C:\\Windows\\System32\\OpenSSH\\ssh.exe" },
    { "action": "HTTPS", "args": "",     "command": "https://" },
    { "action": "HTTP",  "args": "",     "command": "http://" }
]
```

More examples:

```json
    { "action": "SHARE",    "args": "",  "command": "file:///" },
    { "action": "SETTINGS", "args": "",  "command": "ms-settings:" },
    { "action": "PS",       "args": " -NoExit -Command Enter-PSSession -ComputerName ",
                            "command": "powershell.exe" }
```

Rules:

- Backslashes in JSON strings must be doubled (`C:\\Windows\\...`). The editor handles this for you.
- In DDLaunch+, an entry with a missing or non-string field makes the **whole file be rejected**. An error is shown and the previously loaded actions are kept.
- **Duplicate names** (ignoring case) are reported once each time DDLaunch+ loads the file. Only the first entry with a given name is used.
- The **first action in the file** is DDLaunch+'s default Action at startup. The dropdown itself is sorted A to Z.

### `connections.json`

This file can be a **flat array**:

```json
[
    { "host": "dc01.ynext.corp",   "type": "RDP",   "description": "Domain controller" },
    { "host": "admin@pi01",        "type": "SSH",   "description": "Raspberry Pi 01" },
    { "host": "piweb.ynext.corp",  "type": "HTTPS", "description": "Internal web server" }
]
```

…or an **object of named groups**, where each value is one entry or an array of entries:

```json
{
    "Servers": [
        { "host": "dc01.ynext.corp", "type": "RDP", "description": "Domain controller" }
    ],
    "Web": { "host": "piweb.ynext.corp", "type": "HTTPS", "description": "Internal web server" }
}
```

| Field | Meaning |
|---|---|
| `host` | What gets launched: a host name, `user@host`, `host:port`, a path, or a full URL |
| `type` | Which action to use |
| `description` | Free text shown in the list |

- All fields are optional strings. A missing or non-string field is shown as blank. Other fields are ignored by DDLaunch+ and kept by the editor.
- Group names organize the file but aren't shown in DDLaunch+'s list. The editor shows and edits them.
- If the file can't be read or isn't valid JSON, DDLaunch+ shows an error once and keeps the current list until the file is fixed.

### `settings.json` (DDLaunch+)

```json
{
    "window": { "x": 120, "y": 80, "width": 640, "height": 420, "maximized": false },
    "hostHistory": [
        { "host": "fileserver02", "action": "RDP" },
        { "host": "admin@pi07",   "action": "SSH" }
    ]
}
```

- `x`/`y` are screen pixels. `width`/`height` are in 96-DPI units, so the window keeps the same apparent size on monitors with different scaling.
- `hostHistory` is most recent first, at most 25 entries. Entries without a `host`, and repeats of a host, are skipped when read. An `action` that no longer exists is kept but not applied.
- You can edit or clear `hostHistory` by hand while DDLaunch+ is closed. While it's running, its own copy overwrites the file on the next save.

### `editor.json` (Editor)

```json
{
    "window": { "x": 200, "y": 120, "width": 820, "height": 620, "maximized": false },
    "tab": 0,
    "testHost": "server01"
}
```

`tab` is 0 for Connections and 1 for Actions. `testHost` is the sample host on the Actions tab. A missing or corrupt file just means defaults.

---

## Using DDLaunch+

```
+-----------------------------------------------------------------------+
| DDLaunch+ v2.6.1 - double-click a row or press Enter to launch        |
+-----------------------------------------------------------------------+
| Resource (Host, Drive, or Shell Item) |  Type  | Description          |
| dc01.ynext.corp                       |  RDP   | Domain controller    |
| admin@pi01                            |  SSH   | Raspberry Pi 01      |
| ...                                                                   |
|                                                                       |
| Host: [host name, IP or URL      v]  Action: [RDP     v] [    Go    ] |
| [ Edit Actions                    ] [ Launch RDP connection for... ]  |
| [ Edit Connections                ] [ Run...      ] [ Exit         ]  |
| About                                                                 |
+-----------------------------------------------------------------------+
```

| Control | Action |
|---|---|
| Double-click a row / **Enter** on the list | Launch the selected entry |
| Click a column header | Sort by that column; click again to reverse the order |
| **Host** box | Type a host name, `user@host`, `host:port`, a path or a URL, or open the list (arrow, F4 or Alt+Down) to pick a recent host |
| **Action** dropdown | Choose an action, sorted A to Z. Typing a letter jumps to the first action starting with it. |
| **Go** / **Enter** in the Host box or dropdown | Run the chosen action on the typed host |
| **Edit Actions** / **Edit Connections** | Open the editor on the Actions or Connections tab, or the file in Notepad if the editor isn't installed |
| **Launch RDP connection for host not in this list** | Opens Remote Desktop Connection (`mstsc.exe`) empty |
| **Run...** | The Windows Run dialog (same as Win+R) |
| **Exit** | Saves the window position and closes |
| **About** | Version, author and release date |

**Keyboard**

- Tab and Shift+Tab move through the controls in screen order: the list, Host, Action, Go, Edit Actions, Launch RDP, Edit Connections, Run, Exit, About.
- Enter or Space presses the focused button. Enter on the list launches the selected entry. Enter in the Host box or Action dropdown runs **Go**. Enter on About opens it.
- **Alt+H** moves to the Host box and selects its text, **Alt+A** moves to the Action dropdown, and **Alt+G** runs Go.
- In the Host box, **F4** or **Alt+Down** opens the history. With the list open, **Shift+Delete** removes the highlighted entry.
- **Esc is ignored** on purpose.

**Host history**

- Each **successful** Go launch puts the host at the top of the history, with the action used. Failed launches, and ones cancelled at a UAC prompt, aren't added.
- Launching a host that's already in the history moves it to the top instead of adding it twice (ignoring case). The oldest entries drop off after 25.
- Picking a host from the history also selects its last action, if that action still exists.
- Hosts launched from the list aren't added, since they're already in the list.

**Live editing.** Save in the editor (or Notepad) and click back into DDLaunch+. The list and the Action dropdown update, and the selected row, sort order and chosen action are kept.

**Window.** There's a minimum size, so the buttons never overlap and some of the list always shows. If a shortcut is set to "Run: Minimized", that setting takes priority over a saved maximized state.

---

## How a launch works

**From the list**

1. The selected row's ID is used to look up the entry (not the row position or the cell text).
2. Its `type` is matched to an action, ignoring case.

**From the quick-launch row (Go)**

1. Spaces at the start and end of the Host box text are removed. If nothing is left, a *No host* message appears.
2. The action chosen in the dropdown is looked up by name.
3. If the launch succeeds, the host and action are added to the Host history, and `settings.json` is saved.

**Both paths** then call the same routine, `LaunchWithAction`. The action's `command` decides how the launch is done. The editor's **Launches** line follows exactly the same rules.

### Executable command

Any command without a URL scheme (`C:\...`, `ssh.exe`, `\\server\share\tool.exe`) is run as:

```
command   args + host
```

If the host contains spaces, tabs or quotes, it's quoted using standard Windows command-line rules, so it arrives as one argument. For example, with `args` = `" /v:"` and host `Lab PC 7`, the parameters are `/v:"Lab PC 7"`. Plain hosts are passed unchanged.

### URL command

A command starting with an RFC 3986 scheme of two or more characters is treated as a URL (`C:` is a drive letter, not a scheme). `args` is ignored.

| Host | Command | Opened |
|---|---|---|
| `piweb.ynext.corp` | `https://` | `https://piweb.ynext.corp` |
| `dc01:8443` | `https://` | `https://dc01:8443` (host:port is still appended) |
| `https://piweb/x?a=1#f` | `https://` | `https://piweb/x?a=1#f` (already a full URL; used as-is) |
| `http://old` | `https://` | `http://old` (full URL with a different scheme; used as-is) |
| `a@b` / `mailto:a@b` | `mailto:` | `mailto:a@b` |
| `server/My Page` | `https://` | `https://server/My%20Page` |
| `C:\Temp\a b.txt` | `file:///` | `file:///C:\Temp\a%20b.txt` |

- A host is treated as a full URL if it has a scheme followed by `//`, or if it uses the action's own scheme.
- Characters that are never valid in a URL (spaces, control characters, and `` " < > ^ ` { | } ``) are percent-encoded.
- `/ ? # : % \` and non-ASCII text are left as they are.

Every launch goes through `ShellExecuteExW`. If Windows can't open it, DDLaunch+ shows a single message with the command, the parameters and the Win32 error. Declining a UAC prompt is not treated as an error.

---

## Using the DDLaunch+ Editor

Start it with DDLaunch+'s **Edit Connections** / **Edit Actions** buttons, or run `DDLaunchEdit.exe` directly. On the command line, `/connections` or `/actions` (also `-connections`, `/c`, `/a`) chooses the tab. If the editor is already running, the running copy comes to the front and switches tab, and the new copy exits.

```
+------------------------------------------------------------------------------+
| DDLaunch+ Editor v1.0.1 - unsaved changes                                    |
+------------------------------------------------------------------------------+
| [ Connections (8) * ] [ Actions (3) ]                                        |
| Filter: [show only entries containing this text (Ctrl+F)]                    |
| Host              | Type   | Description        | Group   | Check            |
| dc01.ynext.corp   | RDP    | Domain controller  | Servers | OK               |
| vnc-lab           | VNC    | Lab VNC            | Misc    | Unknown type     |
| ...                                                                          |
| [ Add ] [ Duplicate ] [ Delete ] [ Move Up ] [ Move Down ]                   |
| --------------------------------------------------------------------------   |
| Host:         [dc01.ynext.corp                                          ]    |
| Type:         [RDP            v]                                             |
| Description:  [Domain controller                                        ]    |
| Group:        [Servers        v]                                             |
| Launches:     C:\Windows\System32\mstsc.exe  /v:dc01.ynext.corp              |
|                                                                              |
| About  Data folder: ...    [ Open in Notepad ] [ Reload ] [ Save ] [ Close ] |
+------------------------------------------------------------------------------+
```

### Connections tab

| Field | Notes |
|---|---|
| **Filter** | Shows only entries containing the text in Host, Type, Description or Group (ignoring case). Move Up / Move Down are off while filtering, so order can't change unseen. |
| **Host** | Host name, `user@host`, `host:port`, a path or a URL |
| **Type** | Dropdown of the defined actions (A to Z, first of any duplicates). You can also type any name. |
| **Description** | Optional; shown in DDLaunch+'s list |
| **Group** | Optional; the groups already in use are offered. Groups only organize the file. |
| **Launches** | What DDLaunch+ will run for this entry, or why it can't (no host, no type, no such action) |

The list columns are Host, Type, Description, Group and **Check**.

### Actions tab

| Field | Notes |
|---|---|
| **Action** | The name connections use as their type |
| **Command** | Program path or URL prefix. **Browse...** picks a program (`.exe`, `.com`, `.bat`, `.cmd`). |
| **Arguments** | Text placed directly before the host (not used for URLs) |
| **Test host** | A sample host for the line below, remembered between runs (default `server01`) |
| **Launches** | What DDLaunch+ would run for the test host |

The list columns are Action, Command, **Arguments** (in quotes, so leading spaces are visible), **Used by** (how many connections use the action) and **Check**.

### The Check column

Rows with a problem are drawn in red. Save lists the problems and asks before writing.

| Tab | Problem | Meaning |
|---|---|---|
| Connections | No host | The host is empty |
| Connections | No type | The type is empty |
| Connections | Unknown type | No action has that name (not checked if `actionDefinitions.json` couldn't be read) |
| Actions | No name | The action name is empty |
| Actions | Duplicate of entry *n* (ignored) | An earlier action has the same name, ignoring case. DDLaunch+ only uses the first, so Used by shows "-" for this one. |
| Actions | No command | The command is empty |

### Editing

| Command | Button | Keys | What happens |
|---|---|---|---|
| Add | Add | Ctrl+N | A new entry below the selected one. A new connection starts with the selected entry's type and group, or the file's first action. The filter is cleared so the new entry is visible. |
| Duplicate | Duplicate | Ctrl+D | A copy below the original. A copied action gets `_copy` added to its name. |
| Delete | Delete | Del (in the list) | Asks first. Deleting an action that connections use says how many will show "Unknown type". |
| Move | Move Up / Move Down | Ctrl+Up / Ctrl+Down | Changes the order in the file |
| Edit an entry | Double-click a row | Enter (in the list) | Jumps to the first field of the form |
| Switch tabs | Click a tab | Ctrl+Tab / Ctrl+Shift+Tab | |
| Filter | | Ctrl+F | Moves to the Filter box (Connections tab) |
| Save | Save | Ctrl+S | Checks for problems, then writes the changed file(s) |
| Reload | Reload | F5 | Reloads both files from disk (asks first if you have unsaved changes) |
| Open in Notepad | Open *file* in Notepad | | Opens the current tab's file in Notepad (offers to save first) |
| Close | Close | | Asks Save / Don't Save / Cancel if there are unsaved changes. Esc does nothing. |

Tab captions show the number of entries and a `*` when that file has unsaved changes. The title bar shows "unsaved changes", and Save is only enabled when there's something to save.

**Renaming an action.** When you leave the Action field after renaming, the editor checks whether connections still use the old name and no other action has it. If so, it offers to change those connections to the new name. Case-only renames don't ask, since matching ignores case.

### Saving

- **Check first.** Problems in both files are listed (up to 12, then "…and *n* more"), and you choose whether to save anyway.
- **Only changed files** are written. A file that couldn't be read is never written.
- **Backup.** The previous file is copied to `connections.json.bak` / `actionDefinitions.json.bak`.
- **Safe write.** The new text goes to `<file>.tmp`, which then replaces the file, so a crash can't leave it half-written.
- **Format kept.** Key order inside each entry, and any extra fields, are preserved. New entries are written as `host, type, description` and `action, args, command`. An empty description is only written if the entry already had one.
- **Groups.** A flat `connections.json` stays a flat array until any entry gets a group. A grouped file stays grouped: entries without a group go under `"Ungrouped"`, groups are written in order of first appearance, and each group becomes an array. The list is then reordered to match the file.
- **Values that aren't entries** (for example a number inside the array) are dropped on save. The status line warns about this when the file is loaded.

### Files that can't be read

If a file isn't valid JSON or has the wrong shape, the editor explains the error and turns editing off for that file, so it can't be overwritten by mistake. Fix it with **Open in Notepad**, then press **Reload**.

### Changes made outside the editor

Each time the editor window is activated, it compares each file's timestamp and size with the version it loaded.

- No unsaved changes to that file: it's reloaded silently, and the status line says so.
- Unsaved changes: the editor asks whether to reload. Choosing No keeps your version, and saving will then overwrite the other changes.

---

## About boxes

Each program has a blue **About** link: under the buttons in DDLaunch+, and at the left of the status line in the editor. Click it, or press Enter when it has focus.

| DDLaunch+ About | Editor About |
|---|---|
| DDLaunch+ v2.6.1, Data-Driven Launcher | DDLaunch+ Editor v1.0.1 |
| Author: Glenn Madine | Author: Glenn Madine |
| Release date: 11/14/2026 | Release date: 11/14/2026 |
| Version: 2.6.1 | Version: 1.0.1 |
| Editor: installed (Edit buttons open it) or not installed (Edit buttons open Notepad) | |
| Win32 C++ · nlohmann/json 3.12.0 | Win32 C++ · nlohmann/json 3.12.0 |

The author and date come from `APP_AUTHOR` and `APP_RELEASE_DATE` near the top of each `.cpp`. The version comes from `VERSION` (DDLaunch+) and `APP_VERSION` (editor).

The link is a SysLink control, which needs Common Controls v6. If an exe is built without its manifest, a plain "About" label that still responds to clicks is used instead.

---

## Messages and troubleshooting

### DDLaunch+

| Message | Cause / fix |
|---|---|
| **No type** | The entry has no `type`. Add one in the editor. |
| **Unknown type** | No action matches. The message lists the defined actions; add one, or fix the type. |
| **No command** | The matching action has an empty `command`. |
| **No host** (Go) | The Host box is empty. |
| **No action** (Go) | The dropdown is empty because `actionDefinitions.json` has no usable actions. |
| **Launch failed** (Error *n*) | Windows couldn't run it: wrong path, no handler for the URL scheme, etc. Check `command` and `args`. |
| **Duplicate actions** | Two actions have the same name (ignoring case). Only the first is used. |
| **connections.json is not valid JSON** | Shows the parser's message with its position. The old list is kept. |
| **Could not load actionDefinitions.json** | Invalid JSON, not an array, or an entry missing `action`/`args`/`command`. The old actions are kept. |
| **Could not create the DDLaunch data folder** | No permission in the profile folder, or the path is invalid. |
| **Common Controls … is loaded instead of 6.0** (debug build) | The manifest isn't embedded. Build `DDLaunch.rc`, link `DDLaunch.res`, and turn off the linker's own manifest. |

Each error is shown **once per save** of the file. A file saved as "UTF-16" or "ANSI" with non-ASCII characters won't parse. Save it as **UTF-8**.

### Editor

| Message | Cause / fix |
|---|---|
| **File error** (at startup or reload) | A file couldn't be read. Editing it is off until you fix it in Notepad and press Reload. |
| **Check before saving** | Problems were found (see the Check column). Yes saves anyway; No goes back to fix them. |
| **Action renamed** | Connections use the old name. Yes changes their type to the new name. |
| **Delete connection / Delete action** | Confirms the delete. For an action, it also says how many connections use it. |
| **…was changed outside the editor** | The file changed on disk while you have unsaved edits to it. Yes reloads; No keeps your version. |
| **You have unsaved changes** | Shown when closing. Yes saves, No discards, Cancel stays. |
| **Saving failed** | Lists what couldn't be written and why (folder, temporary file or replace step). Nothing is half-written. |

The editor's status line shows the data folder at startup, then the result of the last action (saved, reloaded, copy added, and so on).

---

## Code walkthrough: DDLaunch.cpp

| Area | Key functions / types | What it does |
|---|---|---|
| Text conversion | `Utf8ToWide`, `WideToUtf8`, `ExceptionText` | Converts between UTF-8 (on disk and in nlohmann/json) and UTF-16 (Win32) |
| Actions | `struct Action`, `from_json`, `loadActions`, `EqualsIgnoreCase`, `findAction` | Parses `actionDefinitions.json` and finds actions ignoring case (`CompareStringOrdinal`) |
| File reading | `ReadFileUtf8` | `CreateFileW`/`ReadFile` with full sharing, so non-ASCII paths and open editors are fine |
| Devices | `struct Device`, `LoadConnections` | Accepts an array or an object of groups. Returns `std::nullopt` on error so the old list is kept. |
| Paths and seeding | `GetUserProfileDir`, `GetDataDir`, `EnsureDataFiles`, `CreateFileWithDefaultContent` | Resolves `%USERPROFILE%\DDLaunch` and creates missing files (`CREATE_NEW`, never overwrites) |
| Editing | `OpenJsonFileForEditing` | Starts `DDLaunchEdit.exe /connections` or `/actions` when it sits next to the exe; otherwise runs `notepad.exe "<path>"` |
| Launching | `StartsWithUrlScheme`, `HostIsFullUrl`, `EncodeUrlUnsafeChars`, `QuoteArgIfNeeded`, `LaunchSomething`, `ShellOpen`, `ReportLaunchFailure`, `GetErrorText` | Detects URLs, builds the URL or quotes the argument, launches via `ShellExecuteExW` and reports errors |
| Run dialog | `ShowRunDialog` | `Shell.Application` → `IShellDispatch::FileRun` (documented API, not the shell32 ordinal 61) |
| DPI | `namespace Dpi` | Newer APIs are loaded at run time, so the exe still starts on older Windows |
| Placement and settings | `ClampToWorkArea`, `WindowSettings`, `LoadWindowSettings`, `SaveSettings`, `TrackNormalRect` | Restores the window inside the monitor's work area and writes `settings.json` atomically |
| Host history | `HostHistoryEntry`, `g_hostHistory`, `LoadHostHistory`, `RefreshHostHistoryCombo`, `OnHostHistoryPicked`, `AddHostHistory`, `HandleHostHistoryDelete` | Loads, shows, updates and saves the Host box history |
| Sorting | `ApplyCurrentSort`, `ToggleSortColumn`, `UpdateHeaderSortArrow` | `std::stable_sort` with `CompareStringOrdinal` (ignoring case), plus the header arrows |
| Identity and list | `AssignDeviceIds`, `FindDeviceIndexById`, `GetRowDeviceId`, `PopulateListView` | Stable IDs in each row's `lParam`; selection and focus restored after a refill |
| Action dropdown | `GetWindowTextString`, `GetSelectedActionName`, `PopulateActionCombo` | One entry per name, sorted A to Z; keeps the choice across reloads, else selects the file's first action |
| Reload | `FileStamp`, `GetFileStamp`, `ReloadAllData`, `WarnAboutDuplicateActions` | Reloads only changed files, with a re-entrancy guard and one error per save |
| Launch logic | `LaunchWithAction`, `OnItemActivated`, `ActivateSelection`, `OnGo` | One shared routine checks, launches, reports and returns success. Go adds successful launches to the history. |
| Layout | `namespace Layout`, `S()`, `MeasureButtonWidth`, `MeasureTextWidth`, `ButtonAreaHeight`, `ApplyDpi`, `RescaleForDpi`, `CreateButton`, `CreateLabel` | 96-DPI design values; quick-launch row, two button rows and the About row |
| About | `CreateAboutLink`, `ShowAboutBox` | SysLink (or fallback label) and an icon message box via `MessageBoxIndirectW` |
| Window | `WndProc` | `WM_CREATE`, `WM_SIZE`, `WM_GETMINMAXINFO`, `WM_MOVE`, `WM_DPICHANGED`, `WM_COMMAND`, `WM_NOTIFY`, `WM_ACTIVATE`, `WM_SETFOCUS`, `WM_ENDSESSION`, `WM_DESTROY` |
| Startup | `CheckCommonControlsVersion`, `AcquireSingleInstance`, `HandleQuickLaunchMnemonic`, `wWinMain` | Single instance → COM (STA) → DPI → common controls → window → placement → history → show → first load → message loop |

**Startup sequence.**

1. Single-instance check.
2. `CoInitializeEx` (STA).
3. DPI fallback, then `InitCommonControlsEx` (list view, standard and link classes) and the Common Controls v6 check.
4. Register the class and create the hidden window. `WM_CREATE` builds the controls in Tab order.
5. Place the window from `settings.json`, scaled and clamped, then rescale to the final DPI, and load the Host history.
6. Show the window and do the first data load, so error boxes appear over a visible window.
7. Message loop: `HandleQuickLaunchMnemonic`, `HandleHostHistoryDelete`, then `IsDialogMessageW`. `CoUninitialize` runs after the loop ends.

---

## Code walkthrough: DDLaunchEdit.cpp

| Area | Key functions / types | What it does |
|---|---|---|
| Text helpers | `Utf8ToWide`, `WideToUtf8`, `ExceptionText`, `EqualsIgnoreCase`, `LessIgnoreCase`, `ToUpper`, `ContainsIgnoreCase`, `Trim`, `GetText`, `GetErrorText` | Conversions, case rules (same as DDLaunch+), filter matching |
| Files | `ReadFileUtf8`, `GetDataDir`, `ConnectionsPath`, `ActionsPath`, `EditorSettingsPath`, `FileStamp`, `GetFileStamp`, `WriteFileSafely` | Same folder rules as DDLaunch+. `WriteFileSafely` does the `.bak` copy, `.tmp` write and `MoveFileExW` replace. |
| Launch preview | `StartsWithUrlScheme`, `UrlScheme`, `HostIsFullUrl`, `EncodeUrlUnsafeChars`, `QuoteArgIfNeeded`, `DescribeLaunch` | The same launch-building rules as DDLaunch+, turned into the Launches line |
| Data model | `ConnEntry`, `ActionEntry`, `DocState`, `g_conns`, `g_acts`, `g_connDoc`, `g_actDoc` | Each entry keeps its original JSON object (`ordered_json`) plus the edited fields and a stable ID |
| Lookups and checks | `StrField`, `FindAction`, `CountConnsUsing`, `ConnProblem`, `ActProblem`, `FindConnIndex`, `FindActIndex` | Resolve types like DDLaunch+, and produce the Check column text |
| Loading | `LoadConnectionsDoc`, `LoadActionsDoc` | Parse each file, remember whether connections were grouped, count skipped values, or record a load error |
| Saving | `ConnToJson`, `ActToJson`, `ConnsUseGroups`, `BuildConnectionsText`, `BuildActionsText` | Merge edits into the original objects; build a flat or grouped file |
| Title and tabs | `UpdateTitleAndTabs`, `MarkDirty` | Counts, `*` markers and the Save button state |
| Lists | `RowForId`, `SetCell`, `CheckText`, `SetConnRow`, `SetActRow`, `RefreshRows`, `ConnMatchesFilter`, `RebuildList`, `OnListCustomDraw` | Fill and refresh rows by ID; filter; red text for problem rows (`NM_CUSTOMDRAW`) |
| Forms | `SetComboChoices`, `RefreshConnChoices`, `UpdateConnPreview`, `UpdateActPreview`, `UpdateButtons`, `FillConnFields`, `FillActFields`, `OnSelectionChanged`, `OnConnFieldChanged`, `OnActFieldChanged`, `OnActionNameCommitted` | Keep the form, the data and the list in sync as you type; offer rename updates |
| Commands | `OnAdd`, `OnDuplicate`, `OnDelete`, `OnMove`, `OnBrowseCommand` | List editing and the program picker (`GetOpenFileNameW`) |
| Load/save flow | `ShowLoadErrors`, `LoadedStatus`, `LoadBoth`, `CollectProblems`, `SaveAll`, `ConfirmDiscardOrSave`, `OnReload`, `OnOpenInNotepad`, `CheckFilesChangedOnDisk` | Error reporting, the pre-save check, saving, and outside-change detection |
| Editor settings | `EditorSettings`, `LoadEditorSettings`, `SaveEditorSettings`, `TrackNormalRect` | `editor.json`: window, last tab, test host |
| Layout | `namespace Layout`, `MeasureText`, `ButtonWidth`, `PlaceCombo`, `DoLayout`, `ShowTab` | All positions computed from 96-DPI design values; one set of controls per tab, shown and hidden on switch |
| Creation | `MakeChild`, `MakeButton`, `MakeLabel`, `MakeEdit`, `MakeList`, `CreateControls`, `ApplyFont`, `RescaleForDpi` | Controls created in screen order (Tab order), with cue banners |
| About | `ShowAboutBox` | Icon message box via `MessageBoxIndirectW` |
| Window | `WndProc` | `WM_CREATE`, `WM_SIZE`, `WM_MOVE`, `WM_GETMINMAXINFO`, `WM_DPICHANGED`, `WM_COMMAND`, `WM_NOTIFY`, `WM_ACTIVATE`, `WM_APP_CHECKFILES`, `WM_APP_SHOWERRORS`, `WM_CLOSE`, `WM_ENDSESSION`, `WM_DESTROY`, plus the registered "show tab" message |
| Startup | `TabFromCommandLine`, `AcquireSingleInstance`, `CreateAccelerators`, `wWinMain` | Single instance → COM (STA) → DPI → common controls (list view, tab, standard, link) → window → placement → load → show → message loop (`TranslateAcceleratorW`, then `IsDialogMessageW`) |

---

## Design notes

- **Change detection.** Both programs record each file's timestamp and size (`FileStamp`). DDLaunch+ re-reads only changed files on activation, recording the stamp before parsing so a broken file reports its error once. The editor checks after activation via a posted message, so its questions never appear in the middle of `WM_ACTIVATE`.
- **Re-entrancy.** `MessageBoxW` runs its own message loop. DDLaunch+'s `g_inReload` and the editor's `g_checkingFiles` ignore nested calls. The editor's `g_filling` and `g_rebuilding` stop its own `SetWindowText` calls and list refills from being treated as user edits.
- **Stable IDs.** Both programs give every entry an ID stored in its list row's `lParam`. Selection, editing and launching never depend on row position, sorting or filtering.
- **Case rules.** Action matching, duplicate detection, the history's duplicate check and all sorting use `CompareStringOrdinal(…, TRUE)`, the invariant rule the file system uses, so results don't depend on locale.
- **One set of launch rules.** DDLaunch+'s list entries and Go share `LaunchWithAction`. The editor's `DescribeLaunch` is a copy of the same helper functions. If DDLaunch+'s launch rules ever change, update both.
- **Round-trip editing.** The editor parses with `nlohmann::ordered_json`, which keeps keys in file order, and writes edits back into each entry's original object. Fields it doesn't know about survive.
- **Safe writes.** `settings.json`, `editor.json` and the two config files saved by the editor are written to a temporary file and swapped in with `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.
- **Single instance per logon session.** Both use a mutex in the `Local\` namespace. DDLaunch+ brings the existing window forward. The editor also posts a registered message telling the running copy which tab to show.
- **DPI.** All sizes are 96-DPI design values scaled for the window's monitor. On `WM_DPICHANGED` the font and column widths are rebuilt, and the window takes the size Windows suggests.
- **Mnemonics.** DDLaunch+'s Alt+H/A/G are handled in its message loop, because the mnemonic support in `IsDialogMessageW` isn't dependable for a window that isn't a dialog. The editor uses an accelerator table instead (Ctrl+S, Ctrl+N and so on).
- **Single manifest per exe.** Each manifest comes only from its `.rc`, with `/MANIFEST:NO` on the link line.

---

## Known limitations

**DDLaunch+**

- Group names aren't shown in the list, and the list can't be filtered or searched.
- The sort column and the chosen action aren't saved between runs. The list opens in file order, and the first action in the file is selected.
- The Host history can't be cleared in one step. Remove entries with Shift+Delete, or edit `settings.json` while the app is closed.
- `args` is ignored for URL actions, and a URL can only be built as prefix + host. A literal `#` or `%` in a host is passed as-is.
- **Launch RDP…** runs `mstsc.exe` found through the search path and the App Paths registry key.

**Editor**

- There's no undo. Reload (F5) discards all unsaved changes, and the `.bak` file holds the version before the last save.
- One entry is selected at a time. There's no multi-select delete or move.
- Values in a file that aren't entries are dropped on save (with a warning at load). In a grouped file, a group holding a single object is saved as a one-item array; DDLaunch+ reads both the same way.
- If both files changed outside the editor while you have unsaved edits, each is asked about separately.
- **Browse...** only fills in the Command field. Arguments are always typed by hand.

---

## Version history

### DDLaunch+

| Version | Highlights |
|---|---|
| **2.6.1** | **About** link and box (version, author, release date, editor installed). Edit buttons renamed **Edit Actions** and **Edit Connections**. |
| 2.6.0 | Edit buttons open the DDLaunch+ Editor on the matching tab when it sits next to `DDLaunch.exe`; otherwise Notepad. |
| 2.5.0 | Host box history: up to 25 hosts launched with Go, each with its action; Shift+Delete removes; saved in `settings.json`. |
| 2.4.2 | Action dropdown sorted A to Z; the file's first action is the default. |
| 2.4.1 | Edit actionDefinitions.json and Launch RDP buttons swapped places. |
| 2.4.0 | Quick-launch row: Host box, Action dropdown and Go button; Enter and Alt+H / A / G. |
| 2.3.1 | Version resource fixed; COM initialized once; ordinal sort; full file sharing; full-URL hosts and percent-encoding; first load after the window is shown. |
| 2.3.0 | Hosts with spaces are quoted; duplicate action warning; stable device IDs; single instance. |
| 2.2.0 | Action names ignore case; selection kept across sort and reload; Unicode-safe messages. |
| 2.1.x | One manifest source; Common Controls v6 check; window position saved; keyboard navigation; Per-Monitor V2 DPI. |
| 2.0.x | Clear launch errors; URLs of any scheme; reload only on change; sort kept across reloads. |

### DDLaunch+ Editor

| Version | Highlights |
|---|---|
| **1.0.1** | **About** link and box (version, author, release date). |
| 1.0.0 | First release: Connections and Actions tabs, Launches line, Check column, safe saving with `.bak`, round-trip format, rename help, filter, outside-change detection, single instance with tab selection. |
