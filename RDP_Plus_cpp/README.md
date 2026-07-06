# RDP+ v1.0.9

A lightweight Windows connection launcher for RDP, HTTP/HTTPS, and SSH targets — built in C++17 with the native Win32 API for zero-dependency deployment.

**Author:** Glenn Madine  
**Release:** 06/29/2026  
**Compiler:** MSVC 19.51 (Visual Studio 2022)

---

## Features

- Single-pane list view of all saved connections
- Supports RDP, HTTP, HTTPS, and SSH protocols
- Double-click or press Enter to launch any connection
- Click column headers to sort by Host, Type, or Description
- "New RDP Connection" button opens `mstsc.exe` directly
- SSH connections prompt for a username and launch in Windows Terminal (falls back to `cmd.exe`)
- JSON-based connection list — easy to edit, no database required

---

## Requirements

- Windows 10 or later
- Windows SDK (Common Controls v6)
- [`json.hpp`](https://github.com/nlohmann/json) — nlohmann/json single-header (included alongside source)
- `resource.h` + `RDP_Plus.res` — application icon

---

## Build

```
CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 RDP_Plus.cpp RDP_Plus.res ^
   /Fe:RDP_Plus.exe /link /SUBSYSTEM:WINDOWS ^
   comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib
```

| Flag | Purpose |
|------|---------|
| `/O2 /GL` | Speed optimisation + whole-program optimisation |
| `/DUNICODE /D_UNICODE` | Full Unicode (UTF-16) throughout |
| `/SUBSYSTEM:WINDOWS` | No console window |
| `/std:c++17` | Required for structured bindings in JSON parsing |

---

## connections.json

Place `connections.json` in the **same directory as `RDP_Plus.exe`**. Two layouts are accepted:

**Array format (recommended)**
```json
[
  { "host": "server1.local", "type": "RDP",   "description": "Domain controller"   },
  { "host": "192.168.1.10",  "type": "SSH",   "description": "Linux build server"  },
  { "host": "intranet.corp", "type": "HTTPS", "description": "Internal web portal" }
]
```

**Keyed-object format**
```json
{
  "dc": { "host": "server2.local", "type": "RDP",  "description": "File server" },
  "cameras": [
    { "host": "10.0.0.5", "type": "HTTP", "description": "Camera NVR" }
  ]
}
```

### Supported `type` values

| type | Behaviour |
|------|-----------|
| `RDP` | Launches `mstsc.exe /v:<host>` |
| `HTTP` | Opens `http://<host>` in default browser |
| `HTTPS` | Opens `https://<host>` in default browser |
| `SSH` | Prompts for username, runs `ssh -p 22 <user>@<host>` |

> Type matching is case-insensitive.

---

## Usage

1. Edit `connections.json` with your hosts.
2. Run `RDP_Plus.exe`.
3. Double-click a row (or select it and press **Enter**) to launch.
4. Click **New RDP Connection** to open the standard Windows Remote Desktop dialog.
5. Click **Exit** (or close the window) to quit.

Column headers are clickable — click once to sort ascending, again to reverse.

---

## Project Structure

```
RDP_Plus.cpp       # All application source
RDP_Plus.res       # Compiled resource file (icon)
resource.h         # Resource ID definitions
json.hpp           # nlohmann/json single-header library
connections.json   # Your connection list (not included — create your own)
```

---

## Notes

- **SSH port** is hardcoded to 22. Non-standard ports are not currently supported via `connections.json`.
- **Windows Terminal** (`wt.exe`) is tried first for SSH sessions; `cmd.exe` is the fallback.
- **`WM_LAUNCH`** (`WM_USER + 1`) is defined but reserved for a future async launch feature.
- The `UsernameDialog` struct in the source is superseded by the inline dialog proc in `GetUsernamePopup` and can safely be removed.

---

## License

Private / internal use. See repository or author for licensing terms.
