# DDLaunch+ Data-Driven Launcher

**DDLaunch+ v2.7.1 and DDLaunch+ Editor v1.0.2**

Author: Glenn Madine · Release date: 11/26/2026 · Platform: Windows 7 to Windows 11 (x64), Win32 C++

## Contents

1. Overview
2. What's new in 2.7.1 / Editor 1.0.2
3. Installing
4. The data folder
5. actionDefinitions.json: actions
6. connections.json: connections
7. How a launch is built
8. Using DDLaunch+
9. Using the DDLaunch+ Editor
10. Settings files
11. Messages and troubleshooting
12. Building from source
13. Source code structure
14. Version history

## 1. Overview

DDLaunch+ is a small Windows launcher for the hosts, web consoles, drives, folders and commands you open every day. What to open, and how, is not built into the program: it comes from two JSON files that you edit.

- **connections.json** lists what you launch: a host name, URL, drive, shell folder or command, a **type** and a description. Connections can be organised into named groups, which DDLaunch+ shows as a tree.
- **actionDefinitions.json** says what each type does: which program to run (or which URL prefix to open) and which arguments to put before the host.

For example, a connection `dc1.mydomain.corp` of type `RDP`, together with the `RDP` action `C:\Windows\System32\MSTSC.exe` with arguments ` /v:`, runs `MSTSC.exe /v:dc1.mydomain.corp`.

The package has two programs:

| Program | File | Purpose |
|---|---|---|
| DDLaunch+ | DDLaunch.exe | The launcher: lists your connections and launches them. |
| DDLaunch+ Editor | DDLaunchEdit.exe | A form-based editor for connections.json and actionDefinitions.json, so the files never need to be edited by hand. Optional. |

Main features:

- Launch any connection by double-clicking it or pressing Enter.
- Group tree for connections.json files that use named groups.
- Sortable columns (ascending, descending, back to file order).
- Quick-launch row: type any host, pick an action and press Go. Hosts launched this way are remembered in a history list.
- Any program or any URL scheme (https, ftp, ssh, ms-settings, shell folders ...) can be an action.
- Environment variables such as `%OneDrive%` work in hosts, commands and arguments.
- Edits to the JSON files are picked up as soon as you switch back to DDLaunch+, without restarting.
- Clear error messages that name the file, entry, command and Windows error involved.
- Per-monitor DPI aware: sharp text and correctly sized windows on every monitor and scaling level.
- Window size, position, Host history, group tree width and selected group are remembered.
- Single instance: starting it again brings the running window to the front.

## 2. What's new in 2.7.1 / Editor 1.0.2

DDLaunch+ 2.7.1:

- Environment variables (`%OneDrive%`, `%USERPROFILE%` ...) in a host, command or arguments are expanded before launching.
- An action without `"args"` (or `"command"`, `"action"`) no longer makes the whole actionDefinitions.json fail. Entries that aren't objects, or whose values aren't text, are skipped and listed once in a warning.
- connections.json whose top level is neither an array nor an object is reported as an error (the previous list is kept). Entries that aren't objects are listed in a warning instead of disappearing silently.
- Hosts are trimmed before launching, so a stray space no longer breaks a launch.
- A full URL typed as the host of an InPrivate, SFTP or SHELL-style action is no longer doubled (`https://https://...`).
- Clicking a sorted column a third time returns the list to file order.
- The selected connection is remembered while you browse groups.
- The splitter between the tree and the list only drags from beside the list; keyboard focus is never left on a hidden tree.
- Less flicker when resizing; settings.json is read once at startup; paths longer than 260 characters work.
- The About box shows "·" correctly; the icon is sharp at every size from 16 to 256 pixels.

Editor 1.0.2:

- The "Launches:" preview is built by exactly the same code DDLaunch+ launches with, so it always matches.
- Groups that differ only in upper/lower case are saved as one group.
- In a grouped file, Move Up / Move Down move an entry within its group.
- A failed save no longer reorders the list.
- The "update connections?" question after renaming an action no longer appears on top of another message box.
- Browse... accepts long paths.

Source code: the code both programs share is now in **DDCommon.h**, and all version numbers are in **version.h**. See section 13.

## 3. Installing

1. Copy **DDLaunch.exe** and **DDLaunchEdit.exe** to the same folder, for example `C:\Tools\DDLaunch`. The Editor is optional, but DDLaunch+ only finds it in its own folder.
2. Create a shortcut to DDLaunch.exe on the desktop, Start menu or taskbar. A shortcut set to "Run: Minimized" or "Maximized" is respected.
3. Start DDLaunch+. On the first run it creates the data folder `%USERPROFILE%\DDLaunch` with a starter actionDefinitions.json and an empty connections.json.
4. Click **Edit Connections** to add your connections.

No installer, registry entries or administrator rights are needed (both programs run "as invoker"). To uninstall, delete the two .exe files and, if you no longer want your settings, the `%USERPROFILE%\DDLaunch` folder.

## 4. The data folder

All configuration lives in a per-user folder, `%USERPROFILE%\DDLaunch` (for example `C:\Users\glenn\DDLaunch`). If the profile folder can't be found, the folder containing DDLaunch.exe is used instead.

| File | Written by | Contents |
|---|---|---|
| actionDefinitions.json | You / Editor | The actions (types). See section 5. |
| connections.json | You / Editor | The connections. See section 6. |
| settings.json | DDLaunch+ | Window placement, Host history, group tree. See section 10. |
| editor.json | Editor | Editor window placement, last tab, test host. See section 10. |
| *.json.bak | Editor | The previous version of a file, kept on every save. |
| *.json.tmp | Both | Temporary file while saving; normally gone a moment later. |

If actionDefinitions.json or connections.json is missing (even while DDLaunch+ is running) it is recreated with default content. Existing files are never overwritten by DDLaunch+.

The default actionDefinitions.json contains RDP (mstsc.exe), SSH (the Windows OpenSSH client), HTTPS and HTTP. The default connections.json is an empty list, `[]`.

All JSON files are UTF-8. Names with accents or other non-English characters work in any field and in the profile path.

## 5. actionDefinitions.json: actions

The file is a JSON array. Each entry is one action:

```json
[
    {
        "action": "RDP",
        "args": " /v:",
        "command": "C:\\Windows\\System32\\MSTSC.exe"
    },
    {
        "action": "HTTPS",
        "args": "",
        "command": "https://"
    }
]
```

| Field | Meaning |
|---|---|
| action | The name connections refer to in their "type". Matched ignoring upper/lower case: "RDP", "rdp" and "Rdp" are the same action. |
| command | Either the full path of a program to run, or a URL prefix such as `https://`, `ftp://`, `ms-settings:` that the host is appended to. |
| args | Text placed directly before the host on the program's command line. Include the leading space. Not used for URL prefixes. |

Rules:

- In JSON, a backslash is written twice: `C:\\Windows\\System32\\MSTSC.exe`.
- A missing `args`, `command` or `action` counts as empty. A URL action can leave out `args`.
- An entry that isn't an object `{ ... }`, or that has a value that isn't text (a number, true/false, a list), is skipped and listed in a warning. The other actions still load.
- If two actions have the same name (ignoring case), only the first is used and a warning lists the duplicates.
- Environment variables are allowed: `"command": "%ProgramFiles%\\PuTTY\\putty.exe"`.
- The first action in the file is the default choice in the quick-launch Action dropdown.

Example actions (the ones in the sample file):

| Action | Command | Args | Launches |
|---|---|---|---|
| RDP | C:\Windows\System32\MSTSC.exe | ` /v:` | Remote Desktop to the host |
| SSH | C:\Program Files\PuTTY\putty.exe | ` ` | PuTTY session to the host |
| VNC | ...\RealVNC\VNC Viewer\vncviewer.exe | ` ` | VNC Viewer to the host |
| HTTPS | https:// | (empty) | The host as an https:// address in the default browser |
| HTTP | http:// | (empty) | The host as an http:// address |
| FTP | ftp:// | (empty) | The host as an ftp:// address |
| SFTP | ...\WinSCP\WinSCP.exe | ` sftp://` | WinSCP SFTP session |
| START | C:\Windows\Explorer.exe | ` ` | A drive or folder in File Explorer |
| SHELL | C:\Windows\Explorer.exe | ` Shell:` | A shell folder (Desktop, Downloads, Control Panel ...) |
| CMD | C:\Windows\System32\CMD.exe | ` /k ` | A command prompt that runs the host as a command and stays open |
| PowerShell | ...\powershell.exe | ` -NoExit -command ` | PowerShell running the host as a command, staying open |
| InPrivate | ...\Edge\Application\msedge.exe | ` --inprivate --new-window --no-first-run https://` | The host in a new private Edge window |

## 6. connections.json: connections

Each connection is an object with three fields:

| Field | Meaning |
|---|---|
| host | What to launch: a host name, IP address, host:port, user@host, URL, drive, folder path, shell folder name or command, depending on the action. |
| type | The name of an action in actionDefinitions.json (ignoring case). |
| description | Optional text shown in the list. |

Any other fields are ignored by DDLaunch+ and kept by the Editor.

### Flat list

The file can be a plain array of connections. The group tree is then hidden.

```json
[
    { "host": "dc1.mydomain.corp", "type": "RDP", "description": "Domain controller" },
    { "host": "192.168.1.1", "type": "HTTP", "description": "Router" }
]
```

### Named groups

Or it can be an object whose keys are group names and whose values are arrays of connections. DDLaunch+ then shows a tree of groups, in the order they appear in the file.

```json
{
    "Active Directory": [
        { "host": "dc1.mydomain.corp", "type": "RDP", "description": "Windows 2012R2 DC" },
        { "host": "entra.microsoft.com", "type": "HTTPS", "description": "Entra/Azure" }
    ],
    "Drives": [
        { "host": "P:\\", "type": "START", "description": "P Drive" },
        { "host": "%OneDrive%", "type": "START", "description": "OneDrive" }
    ],
    "Windows Explorer": [
        { "host": "Downloads", "type": "SHELL", "description": "Downloads" },
        { "host": "::{ED7BA470-8E54-465E-825C-99712043E01C}", "type": "SHELL", "description": "God Mode" }
    ]
}
```

Rules:

- A group's value may also be a single connection object instead of an array.
- Values that aren't connections (a string, a number, a group that is neither an array nor an object) are skipped and listed once in a warning.
- If the top level is neither an array nor an object, or the file isn't valid JSON (for example a comma after the last entry), DDLaunch+ shows the error and keeps the list it already had until the file is fixed.
- A missing host or type is reported when you try to launch that entry.

## 7. How a launch is built

DDLaunch+ and the Editor's "Launches:" preview use the same rules (the BuildLaunch function in DDCommon.h):

1. **Environment variables** in the host, command and args are replaced by their values, for example `%OneDrive%` becomes `C:\Users\glenn\OneDrive`. Names that aren't defined are left as they are, so `%20` in a URL is not changed.
2. **The host is trimmed** of leading and trailing spaces.
3. **URL actions.** If the command starts with a URL scheme (two or more letters followed by a colon, such as `https:` or `ms-settings:`), the host is added to it and the result is opened with the program Windows has registered for that scheme. If the host is already a complete URL (for example `https://server/page`, or `mailto:a@b` for a `mailto:` action), it is used as it is. Spaces, control characters, double quotes, backticks and the characters < > ^ { | } are percent-encoded. `host:port` works as expected: `pihole:8080` with `https://` opens `https://pihole:8080`.
4. **Program actions.** Otherwise the command is run as a program with `args` followed by the host as its parameters.
   - A host containing spaces, tabs or quotes is put in quotes (Windows command-line rules), so it arrives as one argument: `/v:"Lab PC 7"`.
   - If the args end with a URL prefix (`... https://`, ` sftp://`, ` Shell:`) and the host already starts with a scheme, the prefix is dropped, so `https://vdi.somesite.com` with the InPrivate action is not doubled.
5. The command is started with the Windows shell "open" verb. If Windows can't start it, DDLaunch+ shows the command, parameters and Windows error. Cancelling a UAC prompt is not treated as an error.

Examples with the sample files:

| Connection (type: host) | What runs |
|---|---|
| RDP: dc1.mydomain.corp | MSTSC.exe /v:dc1.mydomain.corp |
| HTTPS: entra.microsoft.com | https://entra.microsoft.com |
| HTTP: pihole04.mydomain.corp/admin | http://pihole04.mydomain.corp/admin |
| START: %OneDrive% | Explorer.exe C:\Users\glenn\OneDrive |
| SHELL: Downloads | Explorer.exe Shell:Downloads |
| SHELL: ::{21EC2020-3AEA-1069-A2DD-08002B30309D} | Explorer.exe Shell:::{21EC...} (Control Panel) |
| CMD: C:\PROGRA~2\...\vcvars64.bat | CMD.exe /k C:\PROGRA~2\...\vcvars64.bat |
| PowerShell: dir | powershell.exe -NoExit -command dir |
| InPrivate: vdi.somesite.com | msedge.exe --inprivate --new-window --no-first-run https://vdi.somesite.com |
| InPrivate: https://vdi.somesite.com | the same (the prefix isn't doubled) |

**Note:** CMD and PowerShell actions run the host as a command. Characters such as `&`, `|` and `;` are interpreted by the shell, exactly as if you typed them. Only put commands you trust in connections.json.

## 8. Using DDLaunch+

### The main window

The title bar reads "DDLaunch+ v2.7.1 - double-click a row or press Enter to launch".

- **Group tree** (left, only for a file of named groups): "All connections (N)" and each group with its number of entries. Selecting a group shows only its entries. Drag the bar between the tree and the list to change the tree's width.
- **List**: columns Resource (host, drive or shell item), Type and Description. Double-click a row, or select it and press Enter, to launch it.
- **Quick-launch row**: Host box, Action dropdown and Go button, to launch something that isn't in the list.
- **Buttons**:

| Button | What it does |
|---|---|
| Edit Actions | Opens the Editor on its Actions tab (or actionDefinitions.json in Notepad if the Editor isn't installed). |
| Launch RDP connection for host not in this list | Opens Remote Desktop Connection (mstsc.exe) so you can type a computer name. |
| Edit Connections | Opens the Editor on its Connections tab (or connections.json in Notepad). |
| Run... | Opens the Windows Run dialog, the same as Win+R. |
| Exit | Closes DDLaunch+. |
| About (link) | Shows the version, author, release date and whether the Editor is installed. |

### Sorting

Click a column heading to sort by it (A to Z). Click it again for Z to A, and a third time to return to the order of the file. The arrow in the heading shows the current sort. Sorting ignores upper/lower case and is kept when the files are reloaded.

### Groups

The tree lists groups in file order with their counts. The selected group and the tree width are saved when you close DDLaunch+ and restored next time. If you select a connection, switch to a group that doesn't contain it and then come back (or choose All connections), it is selected again.

### Quick launch and Host history

1. Type a host name, IP address, URL, path or command in the **Host** box.
2. Choose an action in the **Action** dropdown. It lists every action once, A to Z; type a letter to jump to it.
3. Click **Go** or press Enter.

Hosts launched successfully with Go are added to the Host box's dropdown list (most recent first, up to 25, no duplicates ignoring case). Picking a host from the list also selects the action it was last launched with. To remove an entry, open the list, highlight the entry and press **Shift+Delete**. The history is saved immediately.

### Keyboard

| Key | Action |
|---|---|
| Enter (list) | Launch the selected connection |
| Enter (tree) | Move to the list (selecting the first entry if none is selected) |
| Enter (Host box or Action dropdown) | Go |
| Alt+H / Alt+A / Alt+G | Host box / Action dropdown / Go |
| Tab, Shift+Tab | Move between the tree, list, quick-launch row, buttons and About link |
| Arrow keys (tree) | Choose a group |
| Shift+Delete (open Host list) | Remove the highlighted history entry |
| Esc | Ignored, so the launcher can't be closed by accident |

### Picking up changes

Whenever the DDLaunch+ window is activated, it checks whether connections.json or actionDefinitions.json has changed (by time and size) and reloads only what changed. The selection, sort order, selected group and chosen action are kept. A file with an error is reported once per save, not every time you switch windows.

### Single instance

Starting DDLaunch+ while it is already running brings the existing window to the front (restoring it if minimized). Each Windows user, including separate Remote Desktop sessions, gets their own copy.

## 9. Using the DDLaunch+ Editor

The Editor is started by the **Edit Connections** and **Edit Actions** buttons, or directly:

```
DDLaunchEdit.exe [/connections | /actions]
```

`/c` and `/a`, and `-` instead of `/`, also work. If the Editor is already running it comes to the front and switches to the requested tab.

### Window

- Two tabs, **Connections (N)** and **Actions (N)**. A `*` on a tab, and "unsaved changes" in the title, mean there are changes that haven't been saved.
- Each tab has a list, a row of buttons (Add, Duplicate, Delete, Move Up, Move Down) and a details form. Changes in the form apply as you type.
- Bottom bar: About link, status text, **Open ... in Notepad**, **Reload**, **Save** and **Close**.

### Connections tab

- **Filter**: shows only entries containing the text in any column (Ctrl+F).
- **List**: Host, Type, Description, Group and Check.
- **Host**, **Description**: free text.
- **Type**: pick a defined action from the dropdown or type a name.
- **Group**: pick an existing group or type a new one. Typing a group in a flat file switches the file to named groups when you save.
- **Launches**: exactly what DDLaunch+ will run for this entry, with environment variables expanded.

Add starts a new entry below the selected one with the same type and group (or the first action if nothing is selected). Duplicate copies the selected entry. Move Up / Move Down are turned off while the filter is in use; in a file of named groups they move an entry within its group.

### Actions tab

- **List**: Action, Command, Arguments (shown in quotes so leading spaces are visible), Used by (how many connections use it) and Check.
- **Action**: the name. If you rename an action that connections use, the Editor asks whether to change those connections too. The question appears when you move to another field, or when you save, add or close.
- **Command**: a program path (Browse... to pick one) or a URL prefix.
- **Arguments**: text placed before the host.
- **Test host**: a sample host for the Launches line (remembered between sessions).

Duplicate adds a copy named "<name>_copy". Deleting an action that connections use warns that they will show "Unknown type".

### Checks

Problems are shown in the Check column and the row is drawn in red. Before saving, the Editor lists them (up to 12) and asks whether to save anyway.

| Check | Meaning |
|---|---|
| No host | The connection has no host. |
| No type | The connection has no type. |
| Unknown type | No action has that name. |
| No name | The action has no name. |
| Duplicate of entry N (ignored) | An earlier action has the same name; DDLaunch+ uses the earlier one. |
| No command | The action has no command. |

### Saving

- **Save** (Ctrl+S) writes the files that have changes. The previous version is kept as `connections.json.bak` / `actionDefinitions.json.bak`, and the new file is written to a temporary file and then swapped in, so a failed save never leaves a damaged file.
- Key order and any extra fields in each entry are kept.
- A flat file stays flat; a grouped file stays grouped, with entries gathered by group in order of first appearance. Entries with no group are saved under "Ungrouped". Group names that differ only in upper/lower case are saved as one group, under the spelling that appears first.
- If saving fails, nothing in the Editor changes, so you can fix the problem and try again.
- DDLaunch+ picks up the saved files when you switch back to it.

### Files that can't be read

If a file isn't valid JSON, the Editor says why and turns off editing for that file, so it can't be overwritten by mistake. Fix it with **Open ... in Notepad**, then press **Reload** (F5). Values that aren't entries are reported in the status bar and dropped when you save.

### Changes made elsewhere

When the Editor is activated it checks both files. A file changed by another program is reloaded if you have no unsaved changes to it; otherwise the Editor asks whether to reload it (losing your changes) or keep your version.

### Keyboard

| Key | Action |
|---|---|
| Ctrl+S | Save |
| Ctrl+N | Add |
| Ctrl+D | Duplicate |
| Delete (list) | Delete the selected entry |
| Ctrl+Up / Ctrl+Down | Move Up / Move Down |
| F5 | Reload both files |
| Ctrl+F | Go to the Filter box |
| Ctrl+Tab / Ctrl+Shift+Tab | Switch tabs |
| Enter or double-click (list) | Go to the first field of the form |
| Esc | Ignored, so the Editor can't be closed by accident |

Closing with unsaved changes asks whether to save them (Yes / No / Cancel).

## 10. Settings files

Both settings files are optional. If one is missing or damaged, defaults are used without a message.

### settings.json (DDLaunch+)

Written when DDLaunch+ closes (including at Windows shutdown or sign-out) and whenever the Host history changes.

```json
{
    "window": { "x": 120, "y": 80, "width": 780, "height": 400, "maximized": false },
    "hostHistory": [ { "host": "dc01.mydomain.corp", "action": "RDP" } ],
    "treeWidth": 190,
    "group": "PCs"
}
```

| Key | Meaning |
|---|---|
| window | Position (screen pixels) and size (96-DPI units, so it looks the same size on any monitor) of the normal, not maximized, window, and whether it was maximized. |
| hostHistory | Up to 25 hosts launched with Go, most recent first, with the action used. |
| treeWidth | Width of the group tree in 96-DPI units (80 to 2000). |
| group | The selected group; absent means All connections. |

The window is always placed fully inside a monitor's work area, so it never opens off-screen after a monitor is disconnected.

### editor.json (Editor)

Window position and size (as above), the last tab (`"tab"`: 0 Connections, 1 Actions) and the Test host (`"testHost"`).

## 11. Messages and troubleshooting

| Message | Cause and fix |
|---|---|
| connections.json is not valid | The file isn't valid JSON (often a comma after the last entry, or a missing quote), or its top level is not an array or object. The message gives the position. The previous list is kept until you fix and save the file. |
| Entries skipped (connections.json) | Some values aren't connection objects. They are listed; fix or remove them. |
| Actions skipped | Some action entries aren't objects or have non-text values. The other actions are loaded. |
| Could not load actionDefinitions.json | The file isn't valid JSON or isn't an array. The previous actions are kept. |
| Duplicate actions | Two actions have the same name ignoring case; only the first is used. |
| No type / Unknown type | The connection has no type, or no action has that name. The message lists the defined actions. |
| No command | The action has an empty command. |
| Launch failed, Error N | Windows couldn't start the command. Common causes: the program path is wrong (Error 2), or no program is registered for a URL scheme (Error 1155). Check the command and args shown. |
| No host / No action is selected | The quick-launch Host box is empty, or no action is chosen. |
| Could not create the DDLaunch data folder | The profile folder isn't writable. |

Other problems:

- **OneDrive (or another folder using %...%) opens the wrong folder.** Check that the variable exists: run `echo %OneDrive%` in a command prompt.
- **A PowerShell window closes immediately.** Add `-NoExit` to the action's args.
- **No sort arrows and an old-fashioned look.** The manifest wasn't embedded when building. Build with DDLaunch.rc and link DDLaunch.res (section 12). A debugger shows a "Common Controls" message at startup in this case.
- **The Edit buttons open Notepad.** DDLaunchEdit.exe isn't in the same folder as DDLaunch.exe. The About box shows whether it was found.

## 12. Building from source

### Requirements

- Microsoft C++ compiler (Visual Studio 2022 or later, or the Build Tools; developed with MSVC 19.51), x64.
- Windows SDK.
- nlohmann/json 3.12.0, single-header version, saved as json.hpp.

### Build files

| File | Purpose |
|---|---|
| DDLaunch.cpp | DDLaunch+ source. |
| DDLaunchEdit.cpp | Editor source. |
| DDCommon.h | Code shared by both programs (see section 13). |
| version.h | Version numbers, release dates and author for both programs. |
| DDLaunch.rc, DDLaunchEdit.rc | Resources: icon, manifest, version information. |
| DDLaunch.manifest, DDLaunchEdit.manifest | Common Controls 6, Per-Monitor V2 DPI awareness, asInvoker, supported Windows versions. |
| resource.h | Resource IDs (the icon). |
| DDLaunch.ico | Icon, 16 to 256 pixels, used by both programs. |
| json.hpp | nlohmann/json. |
| Compile.cmd | Builds both programs. |

### Building with Compile.cmd

Open an **x64 Native Tools Command Prompt for VS** (or run vcvars64.bat), go to the folder with the build files and run `Compile.cmd`. It:

1. Checks that every build file is present and names any that are missing.
2. Compiles both resource files with `rc`.
3. Compiles and links DDLaunchEdit.exe, then DDLaunch.exe.
4. Stops at the first error, with "Build FAILED"; otherwise prints "Build succeeded".

The command lines it uses:

```
rc /nologo DDLaunchEdit.rc
rc /nologo DDLaunch.rc
CL /nologo /utf-8 /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunchEdit.cpp DDLaunchEdit.res /Fe:DDLaunchEdit.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib comdlg32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib
CL /nologo /utf-8 /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunch.cpp DDLaunch.res /Fe:DDLaunch.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib
```

Important options:

- `/utf-8`: the source files are UTF-8 without a byte-order mark.
- `/MANIFEST:NO`: each program's manifest comes only from its .rc file. Without this the linker produces a second manifest.

### Building in a Visual Studio project

Create one Windows Desktop project per program and set:

- C/C++ > Language > C++ Language Standard: ISO C++17.
- C/C++ > Command Line > Additional Options: `/utf-8`.
- Advanced > Character Set: Use Unicode Character Set.
- Linker > System > SubSystem: Windows.
- Linker > Manifest File > Generate Manifest: No.
- Add the program's .cpp, .rc, DDCommon.h, version.h and resource.h to the project.

The libraries are linked automatically by `#pragma comment(lib, ...)` lines in the sources.

### Releasing a new version

1. Change the numbers and dates in **version.h** only. The window titles, About boxes and Explorer's file version all come from it. (The manifests' assembly version is fixed and doesn't need changing.)
2. Add a "Changes in x.y.z" note at the top of the changed .cpp file.
3. Run Compile.cmd.

## 13. Source code structure

### DDCommon.h

Header-only code used by both programs, so they behave identically:

| Part | Functions |
|---|---|
| Text | Utf8ToWide, WideToUtf8, ExceptionText, CompareIgnoreCase / EqualsIgnoreCase / LessIgnoreCase (locale-independent CompareStringOrdinal), Trim, GetWindowTextString, GetErrorText |
| Files | ReadFileUtf8 (shared read, so files open in an editor can be read), FileExists, DirectoryExists, GetExeDir (long paths), GetUserProfileDir, GetDataDir, FileStamp / GetFileStamp (change detection) |
| Launch rules | StartsWithUrlScheme, UrlScheme, HostIsFullUrl, EncodeUrlUnsafeChars, QuoteArgIfNeeded, ExpandEnvVars and **BuildLaunch**, the single launch rule described in section 7 |
| DPI | Dpi::EnableDpiAwareness, ForWindow, ForMonitor, Scale, CreateUiFont, WindowSizeForClient; ClampToWorkArea |

### DDLaunch.cpp

| Area | Main functions |
|---|---|
| Loading | loadActions, LoadConnections, EnsureDataFiles, ReloadAllData (reloads only files whose FileStamp changed) |
| Launching | LaunchWithAction, LaunchSomething (BuildLaunch + ShellExecuteExW), ReportLaunchFailure, OnItemActivated, OnGo |
| List and tree | PopulateListView, ApplyCurrentSort, ToggleSortColumn, AssignDeviceIds (stable IDs keep the selection across reloads), RebuildGroupTree |
| Quick launch | PopulateActionCombo, Host history (AddHostHistory, LoadHostHistory, HandleHostHistoryDelete) |
| Window | WndProc, layout in WM_SIZE, the splitter, DPI changes, SaveSettings / LoadSettingsFile, single instance |

### DDLaunchEdit.cpp

| Area | Main functions |
|---|---|
| Data | LoadConnectionsDoc, LoadActionsDoc (ordered_json, keeping extra fields), BuildConnectionsText, BuildActionsText, WriteFileSafely (.bak, .tmp swap) |
| Checks | ConnProblem, ActProblem, CollectProblems |
| Editing | OnAdd, OnDuplicate, OnDelete, OnMove / ConnMoveTarget, OnActionNameCommitted (rename handling) |
| Preview | DescribeLaunch (BuildLaunch from DDCommon.h) |
| Window | CreateControls, DoLayout, ShowTab, CheckFilesChangedOnDisk, editor.json, single instance |

## 14. Version history

### DDLaunch+

| Version | Changes |
|---|---|
| 2.7.1 | Environment variables expanded; tolerant actionDefinitions.json loading; warnings for skipped entries; invalid top level reported; hosts trimmed; full-URL hosts not doubled; three-state column sort; selection remembered across groups; splitter and tree-focus fixes; less flicker; settings read once; long paths; About box "·" fixed; multi-size icon; shared DDCommon.h and version.h. |
| 2.7.0 | Group tree for files of named groups, with counts, resizable splitter, saved width and group; file order kept (ordered_json). |
| 2.6.1 | About link and About box. |
| 2.6.0 | Edit buttons open the DDLaunch+ Editor on the matching tab, Notepad if it isn't installed. |
| 2.5.0 | Host box history (25 entries, Shift+Delete to remove, remembers the action). |
| 2.4.2 | Action dropdown sorted A to Z. |
| 2.4.1 | Edit Actions and Launch RDP buttons swapped. |
| 2.4.0 | Quick-launch row: Host, Action, Go; Alt+H / Alt+A / Alt+G. |
| 2.3.1 | Version resource updated; COM initialized once; locale-independent sorting; shared file reading; full-URL hosts used as-is and unsafe URL characters encoded; first load after the window is visible. |
| 2.3.0 | Hosts with spaces quoted; duplicate actions reported; stable device IDs; single instance. |
| 2.2.0 | Case-insensitive action names; selection kept across sorts and reloads; previous list kept when connections.json is invalid; Unicode error messages; dead code removed. |
| 2.1.2 | Single manifest source (DDLaunch.manifest), Per-Monitor V2, build self-check. |
| 2.1.1 | Window position and size saved; window kept on-screen. |
| 2.1.0 | Keyboard navigation, visual styles, per-monitor DPI scaling. |
| 2.0.3 | Clear error messages for unknown types, empty commands and failed launches. |
| 2.0.2 | Any URL scheme recognised. |
| 2.0.1 | Files reloaded only when changed; sort kept after reload. |

### DDLaunch+ Editor

| Version | Changes |
|---|---|
| 1.0.2 | Preview uses the shared launch rule; groups merged ignoring case; moves stay within a group; failed saves change nothing; rename question no longer overlaps other message boxes; long paths in Browse...; About box "·" fixed; shared DDCommon.h and version.h. |
| 1.0.1 | About link and About box. |
| 1.0.0 | First release: Connections and Actions tabs, launch preview, checks, safe saving with .bak, round-trip of extra fields and groups, external change detection, single instance. |
