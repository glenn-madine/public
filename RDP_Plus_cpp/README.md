# RDP+

A lightweight Win32 desktop utility for launching remote sessions (RDP, SSH, VNC, HTTP/HTTPS, or any other command-line tool) against a list of hosts, without touching Windows' built-in Remote Desktop Connection manager. The host list and the launch behavior for each connection "type" are both driven by JSON config files sitting next to the executable, so the tool can be extended to new protocols without recompiling.

- **Version:** v1.6.1
- **Author:** Glenn Madine
- **Platform:** Windows (Win32 API, no .NET/MFC dependency)

## Features

- Sortable list view of hosts, grouped by **Host**, **Type**, and **Description** — click a column header to sort, click again to reverse the order.
- Double-click (or press Enter on) a host to launch its session using whatever command is registered for that host's `type` in `actionDefinitions.json`.
- **Launch RDP connection for host not in this list** button — a quick escape hatch that opens `mstsc.exe` directly for one-off connections that aren't worth adding to the config.
- **Edit connections.json** / **Edit actionDefinitions.json** buttons — open either config file directly in Notepad from within the app. The list is automatically reloaded from disk as soon as the main window regains focus, so saved changes show up immediately without restarting RDP+.
- Handles `http://`, `https://`, `ftp://`, `file://`, `mailto:`, and `tel:` prefixes as URLs (opened via `ShellExecute`) and treats everything else as a local command plus arguments (e.g. launching `mstsc.exe` or an SSH client with the host appended).
- All JSON is read/written as UTF-8 and converted to/from UTF-16 internally, so non-ASCII hostnames and descriptions are supported.

## Requirements

- Windows with the Windows SDK (Common Controls v6 / `comctl32.lib`)
- Microsoft Visual C++ (built and tested with MSVC 19.51)
- [nlohmann/json](https://github.com/nlohmann/json) — single-header library, included in this project as `json.hpp`
- A `resource.h` / `.rc` resource file providing the application icon (`IDI_ICON1`)

## Building

RDP+ is a single translation unit with no build system beyond the compiler. From a Visual Studio "Developer Command Prompt":

```bat
CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 ^
   RDP_Plus.cpp RDP_Plus.res /Fe:RDP_Plus.exe /link ^
   /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib
```

This produces `RDP_Plus.exe`. Place it in a folder alongside `json.hpp`, `connections.json`, and `actionDefinitions.json` (see below) — RDP+ always looks for its config files in the same directory as the running executable.

## Configuration files

Both files live next to `RDP_Plus.exe`. Neither one has to exist ahead of time — if you click **Edit connections.json** or **Edit actionDefinitions.json** and the file is missing, RDP+ creates an empty `[]` for you before opening it in Notepad.

### `connections.json` — the host list

Accepts either a flat JSON array of device objects:

```json
[
  { "host": "srv01.ynext.corp", "type": "RDP", "description": "Primary domain controller" },
  { "host": "192.168.1.50",     "type": "SSH", "description": "Raspberry Pi - piweb" },
  { "host": "portal.ynext.corp","type": "HTTPS","description": "Internal web portal" }
]
```

...or an object whose values are groups (arrays or single objects) of the same device shape — useful for organizing hosts by site, role, etc. RDP+ flattens either shape into one list before display:

```json
{
  "Domain Controllers": [
    { "host": "dc1.ynext.corp", "type": "RDP", "description": "DC1" },
    { "host": "dc2.ynext.corp", "type": "RDP", "description": "DC2" }
  ],
  "Web Servers": {
    "host": "piweb.ynext.corp", "type": "HTTPS", "description": "Pi web server"
  }
}
```

Fields:

| Field | Required | Description |
|---|---|---|
| `host` | yes | Hostname, FQDN, or IP address to connect to. |
| `type` | yes | Connection type; matched (case-insensitively) against the `action` field in `actionDefinitions.json`. |
| `description` | no | Free-text note shown in the list view. |

### `actionDefinitions.json` — how each `type` launches

A top-level JSON array where each entry maps a `type` name to the command and arguments used to launch it:

```json
[
  { "action": "RDP", "args": " /v:", "command": "C:\\Windows\\System32\\mstsc.exe" },
  { "action": "SSH", "args": " ",    "command": "C:\\Program Files\\PuTTY\\putty.exe -ssh " },
  { "action": "VNC", "args": " ",    "command": "C:\\Program Files\\TightVNC\\tvnviewer.exe " }
]
```

Fields:

| Field | Description |
|---|---|
| `action` | The `type` value it matches (matching is case-insensitive; RDP+ upper-cases both sides before comparing). |
| `command` | The executable to launch, or a URL prefix such as `https://`, `mailto:`, `tel:`, etc. |
| `args` | Extra arguments inserted before the host, e.g. the leading space and `/v:` flag `mstsc.exe` expects. |

When the host's `type` doesn't match any entry, RDP+ falls back to the literal string `"Default"` for both command and args rather than failing outright.

If `actionDefinitions.json` is missing or contains invalid JSON, RDP+ shows a warning dialog and keeps whatever action list it last loaded successfully, instead of crashing.

## Usage

1. Launch `RDP_Plus.exe`.
2. The list populates from `connections.json`. Click a column header to sort by Host, Type, or Description.
3. Double-click a row (or select it and press Enter) to launch that connection using the command registered for its type.
4. Use **Launch RDP connection for host not in this list** for an ad-hoc `mstsc.exe` session.
5. Use **Edit connections.json** or **Edit actionDefinitions.json** to add, remove, or update entries. Save in Notepad, switch back to RDP+, and the list refreshes automatically.
6. **Exit** closes the application.

## Project layout

```
RDP_Plus.cpp          Application source (single file)
RDP_Plus.res           Compiled resource file (icon, etc.)
resource.h             Resource ID definitions
json.hpp                nlohmann/json single-header library
connections.json        Host list (user-maintained, not checked in with secrets)
actionDefinitions.json  Per-type launch commands (user-maintained)
```

## How it works (implementation notes)

- **UI**: A single top-level window built directly on the Win32 API — a `SysListView32` control in report mode plus four buttons, laid out by hand in the `WM_SIZE` handler (no dialog resource or XAML/WinForms layer).
- **JSON parsing**: [nlohmann/json](https://github.com/nlohmann/json) parses both config files; `from_json`/`to_json` overloads convert the `Action` struct to and from JSON, converting UTF-8 (on disk) to UTF-16 (`std::wstring`, used throughout the Win32 code) at the boundary.
- **File I/O**: Config files are read via raw Win32 `CreateFileW`/`ReadFile` calls rather than `std::ifstream`, so wide (Unicode) file paths work without relying on MSVC-specific `ifstream` extensions.
- **Launching**: `ShellExecuteW` is used for everything — recognized URL schemes (`http://`, `https://`, `ftp://`, `file://`, `mailto:`, `tel:`) are opened as URLs; anything else is treated as `command` + `args` + `host` and launched as a local process.
- **Live reload**: Both config files are reloaded from disk on `WM_ACTIVATE` (whenever the window regains focus), so edits made via the built-in Notepad shortcuts are picked up without restarting the app.

## Known limitations

- Windows-only; there is no cross-platform build path (`Win32`, `ShellExecuteW`, `SysListView32`, etc. are all Windows-specific).
- Editing is delegated entirely to Notepad — RDP+ does not validate JSON syntax before you save, so a malformed edit will surface as a warning dialog on next reload rather than being caught inline.
- Both config files must live in the same directory as the executable; there's currently no way to point RDP+ at a config location elsewhere (e.g. a shared network path).

## License

No license file is currently included in this repository. Add one (e.g. MIT, Apache-2.0) before publishing publicly if you intend to allow reuse.
