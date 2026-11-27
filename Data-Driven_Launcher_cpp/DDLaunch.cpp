// Data-Driven Launcher+ version 2.7.1
// C++ (Win32 API)
// Author: Glenn Madine
// Release_Date: 11/26/2026
// Changes in 2.7.1:
//   - R15: Code shared with the DDLaunch+ Editor (text, file, URL/quoting,
//          launch and DPI helpers) moved to DDCommon.h, so the Editor's
//          "Launches:" preview is built by the same code DDLaunch+ uses.
//   - R16: Version numbers, release dates and the author live only in
//          version.h, which this file and DDLaunch.rc both include.
//   - B20: actionDefinitions.json: an entry without "args" (or "command" /
//          "action") no longer makes the whole file fail -- missing values
//          count as empty. Entries that aren't objects, or whose values
//          aren't text, are skipped and listed once in a warning.
//   - B21: Environment variables (%OneDrive%, %USERPROFILE% ...) in a host,
//          command or args are expanded before launching.
//   - B22: connections.json whose top level is neither an array nor an object
//          of groups is reported as an error (the previous list is kept)
//          instead of silently emptying the list.
//   - B23: Entries in connections.json that aren't objects (and group values
//          that aren't arrays or objects) are listed once in a warning
//          instead of disappearing silently.
//   - B24: Hosts are trimmed before launching, so " dc1" isn't passed as
//          /v:" dc1".
//   - B25: For program actions whose args end in a URL prefix (InPrivate
//          "... https://", SFTP " sftp://", SHELL " Shell:"), a host that is
//          already a full URL is no longer doubled ("https://https://...").
//   - B26: The tree/list splitter only starts a drag inside the list area,
//          not from the strip beside the buttons.
//   - B27: When the group tree is hidden (the file became a flat array) while
//          it had keyboard focus, focus moves to the list.
//   - B28: The selected connection is remembered while browsing groups: going
//          back to a group that contains it (or to All) selects it again.
//   - B29: About box: the "·" is written as · and both programs are
//          compiled with /utf-8, so it no longer shows as "Â·".
//   - F8:  Clicking the sorted column a third time returns to file order.
//   - R17: WS_CLIPCHILDREN on the main window (less flicker when resizing).
//   - R18: settings.json is read once at startup instead of three times.
//   - R19: The .exe folder is found with a growing buffer (paths longer than
//          MAX_PATH work).
//   - R20: The "Unknown type" message lists each defined action once (no
//          blanks or duplicates), A-Z.
//   - R21: Unused WM_LAUNCH removed; C-style (HMENU) cast replaced; stale
//          button captions in comments and messages updated.
//   - R22: DDLaunch.ico now holds 16-256 px 32-bit images, so the window,
//          taskbar and About box icons are sharp at every DPI.
// Changes in 2.7.0:
//   - F7: Group tree. When connections.json uses named groups
//         ({ "RDP connections": [ ... ], "SSH connections": [ ... ] }), a tree
//         on the left lists "All connections" and each group with its count,
//         in file order. Selecting a group (click or arrow keys) shows only
//         its entries; sorting, selection and launching work as before. The
//         tree is hidden for a flat-array file. connections.json is now read
//         with ordered_json, so groups -- and the unsorted list -- follow the
//         file's order (before, groups came out A-Z). Drag the bar between the tree
//         and the list to resize it; the width and the selected group are
//         saved in settings.json ("treeWidth", "group"). Enter in the tree
//         moves to the list.
// Changes in 2.6.1:
//   - F6: "About" link under the buttons (bottom left). Clicking it, or
//         pressing Enter on it, opens an About box with the version, author,
//         release date and whether the DDLaunch+ Editor is
//         installed. It is a SysLink control (Common Controls v6); without
//         the manifest a plain clickable label is used instead.
// Changes in 2.6.0:
//   - F5: The "Edit connections.json" and "Edit actionDefinitions.json"
//         buttons open the DDLaunch+ Editor (DDLaunchEdit.exe) on the matching
//         tab when it is in the same folder as DDLaunch.exe. Without it (or
//         if it can't be started) they open Notepad as before. Changes saved
//         in the editor are picked up when you switch back to DDLaunch+.
//         (The buttons are now captioned "Edit Connections" / "Edit Actions".)
// Changes in 2.5.0:
//   - F4: Host box history. The Host box is now an editable dropdown that
//         remembers the hosts launched with Go (most recent first, up to 25,
//         no duplicates ignoring case). Only successful launches are added.
//         Picking a host from the history also selects the action it was last
//         launched with. Shift+Delete on a highlighted entry in the open list
//         removes it. The history is saved in settings.json ("hostHistory")
//         after every change, so it survives restarts.
// Changes in 2.4.2:
//   - F3: The Action dropdown is sorted A-Z, ignoring case (CompareStringOrdinal,
//         the same rule as the list columns). Until an action is chosen, the
//         first action in actionDefinitions.json is still the one selected.
//         Typing a letter in the dropdown jumps to the first action that
//         starts with it.
// Changes in 2.4.1:
//   - F2: "Edit actionDefinitions.json" and "Launch RDP connection for host
//         not in this list" swapped places: Edit actionDefinitions.json is
//         now on the left of the middle row (above Edit connections.json) and
//         Launch RDP on the right. Tab order follows the new layout.
//         (Since renamed "Edit Actions" and "Edit Connections".)
// Changes in 2.4.0:
//   - F1: Quick-launch row above the buttons: a "Host" text box, an "Action"
//         dropdown filled from actionDefinitions.json, and a "Go" button.
//         Go runs the chosen action's command/args on the typed host exactly
//         like a list entry (same quoting, URL rules and error messages).
//         Enter in the Host box or the dropdown also runs Go. Alt+H / Alt+A /
//         Alt+G jump to the box, the dropdown and the button.
//         The dropdown lists each action once (case-insensitive duplicates
//         are left out, as only the first is ever used), in file order, and
//         keeps the chosen action when actionDefinitions.json is reloaded.
//   - List entries and Go share one launch routine, LaunchWithAction().
// Changes in 2.3.1:
//   - R13: DDLaunch.rc version resource updated to 2.3.1 (it still said
//         2.1.2). resource.h now names DDLaunch.rc and drops the unused
//         IDI_ICON2 / IDI_ICON3 left over from RDP_Plus.
//   - B15: COM is initialized once (single-threaded apartment) on the UI
//         thread in wWinMain, as ShellExecuteEx requires for shell extensions
//         and some URL handlers. ShowRunDialog no longer initializes COM itself.
//   - B16: Column sorting compares with CompareStringOrdinal (ignoring case),
//         the same rule used for action names, instead of the locale-dependent
//         _wcsicmp.
//   - B17: Config files are opened with FILE_SHARE_READ | FILE_SHARE_WRITE |
//         FILE_SHARE_DELETE, so a reload succeeds even while an editor still
//         has the file open.
//   - B18: URL actions: a host that is already a full URL (e.g.
//         "https://server/page" for type HTTPS) is opened as-is instead of
//         becoming "https://https://...". Characters that are never valid in
//         a URL (spaces, control characters, " < > ^ ` { | }) are
//         percent-encoded. "host:port" still works as before.
//   - B19: The first data load runs after the window is shown, so any JSON
//         error box at startup has a visible owner window.
//   - R14: Stale "upper-cased" comment on DEFAULT_ACTIONS_JSON corrected;
//         user32.lib / gdi32.lib added to the #pragma comment(lib) list so
//         it matches the link line.
// Changes in 2.3.0:
//   - B11: A host containing spaces, tabs or quotes is quoted (Windows
//         command-line rules) before it is appended to "args", so it reaches
//         the program as one argument. Plain hosts are passed unchanged.
//   - B13: When actionDefinitions.json loads, duplicate action names
//         (ignoring case) are reported once; the first entry is the one used.
//   - B14: Each device gets a stable ID, stored in its list row's lParam.
//         Selection is tracked by ID instead of by reading back cell text,
//         and IDs carry over across reloads, so long cells no longer break it.
//         Launching looks the device up by ID rather than by row position.
//   - R12: Single instance: starting DDLaunch+ while it is already running
//         brings the existing window to the front (restoring it if it was
//         minimized) instead of opening a second copy.
//   - R9:  Window title reads "double-click a row or press Enter to launch".
//   - R5:  Updated the stale path-quoting comment in OpenJsonFileForEditing.
// Changes in 2.2.0:
//   - B5: Action names are matched case-insensitively (CompareStringOrdinal),
//         so "Rdp", "rdp" and "RDP" all work. findActionIgnoreCase() and the
//         "rename it to UPPER CASE" hint are no longer needed.
//   - B9: The selected row (and keyboard focus row) is kept across column
//         sorts and data reloads, and scrolled into view.
//   - B8: If connections.json can't be read or parsed, the previously loaded
//         list is kept (as actionDefinitions.json already did) instead of
//         being emptied.
//   - B10: JSON error messages use MessageBoxW with the UTF-8 text converted
//         to UTF-16, so non-ASCII paths (e.g. C:\Users\José) display correctly.
//   - Dead code removed: UsernameDialog / GetUsernamePopup (D1), Logger (D2),
//         getExeDirectory (D3), ToWide / WStringToString (D4, replaced by
//         Utf8ToWide), the unused PopulateListView parameter (D6),
//         to_json(Action) (D7), and #include <fstream> / <sstream> (D8).
// Changes in 2.1.2:
//   - R11: The application manifest now has a single source: the file
//         DDLaunch.manifest, embedded by DDLaunch.rc as RT_MANIFEST
//         resource 1. The linker is told not to generate one (/MANIFEST:NO)
//         and the #pragma manifestdependency is gone, so a duplicate-manifest
//         link error (CVT1100 / LNK1123) can no longer happen. The manifest
//         also declares Per-Monitor V2 DPI awareness, asInvoker and the
//         supported Windows versions.
//   - EnableDpiAwareness() leaves the process alone if the manifest already
//     set DPI awareness, instead of trying three APIs that would fail.
//   - Startup self-check: if Common Controls v6 is not active (manifest not
//     embedded), a message is written to the debugger output, and debug
//     builds also show a message box.
// Changes in 2.1.1:
//   - R1: UNICODE/_UNICODE are only defined if the command line hasn't
//         already defined them (no more warning C4005).
//   - R2: <cwctype> (towupper) and <cstdint> (uintptr_t) are included
//         explicitly instead of relying on transitive includes.
//   - R8: Window position, size and maximized state are saved to
//         %USERPROFILE%\DDLaunch\settings.json on exit and restored on the
//         next start. Size is stored in 96-DPI units so it is correct on
//         monitors with different scaling.
//   - R10: The startup window rectangle (default or restored) is clamped to
//         the work area of its monitor, so it never opens partly off-screen
//         or under the taskbar -- e.g. after a monitor is disconnected.
// Changes in 2.1.0 (UI polish):
//   - Keyboard: IsDialogMessageW in the message loop, so Tab / Shift+Tab move
//     between the list and the buttons, Enter/Space press the focused
//     button, and Enter on the list launches the selected entry. Tab order
//     follows the on-screen layout; focus is restored on re-activation.
//   - Common Controls v6 manifest dependency (#pragma comment(linker));
//     replaced by DDLaunch.manifest in 2.1.2.
//   - Per-Monitor V2 DPI awareness: fonts, column widths, button sizes and
//     margins scale with the monitor's DPI and update live on WM_DPICHANGED.
//     Buttons size to fit their text; the window has a minimum size.
// Changes in 2.0.3:
//   - B3: Unknown or missing types, empty commands and failed launches now
//         show a clear error message instead of failing silently. Launches
//         use ShellExecuteExW so the Win32 error is reported. Action names
//         that differ only by case are pointed out.
// Changes in 2.0.2:
//   - B4: URL commands are detected generically (any RFC 3986 scheme such
//         as ssh://, vnc://, ms-settings:) instead of a hard-coded list;
//         also fixes "file:///" never being recognised.
// Changes in 2.0.1:
//   - B1: JSON files are only reloaded on window activation when their
//         last-write time or size has changed, so an invalid file shows its
//         error once instead of looping, and startup no longer loads twice.
//   - B2: The current column sort (and header arrow) is re-applied after
//         every reload instead of reverting to file order.
// Requires: Windows SDK, nlohmann/json (single-header, included as json.hpp)
// Compiled using Microsoft C++ 19.51
// Config files: %USERPROFILE%\DDLaunch\actionDefinitions.json and
//               %USERPROFILE%\DDLaunch\connections.json
//               %USERPROFILE%\DDLaunch\settings.json (window placement, Host
//               box history, group tree; written on exit and whenever the
//               history changes)
//               (folder and files are created automatically if missing)
// Build files: DDLaunch.cpp, DDCommon.h, version.h, DDLaunch.rc,
//              DDLaunch.manifest, resource.h, DDLaunch.ico, json.hpp
// Compile and link command lines (Compile.cmd builds both programs):
//     rc /nologo DDLaunch.rc
//     CL /nologo /utf-8 /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunch.cpp DDLaunch.res /Fe:DDLaunch.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib
// /utf-8: the source files are UTF-8 without a byte-order mark (B29).
// The manifest comes only from DDLaunch.rc (see R11 above); /MANIFEST:NO
// stops the linker adding a second one. In a Visual Studio project, set
// Linker > Manifest File > Generate Manifest = No.

// Also passed as /DUNICODE /D_UNICODE on the command line; guard so the
// definitions don't clash (R1).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shldisp.h>  // IShellDispatch (Shell.Application) -- used for the Run dialog
#include <shlobj.h>   // SHGetKnownFolderPath / FOLDERID_Profile -- fallback for %USERPROFILE%
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <cwctype>    // towupper (R2)
#include <cstdint>    // uintptr_t (R2)
#include "json.hpp"   // nlohmann/json single-header
#include "resource.h"
#include "version.h"  // version, release date, author (R16)
#include "DDCommon.h" // helpers shared with the Editor (R15)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Common Controls v6 (visual styles, header sort arrows, themed buttons) and
// DPI awareness are declared in DDLaunch.manifest, which DDLaunch.rc embeds.
// There is deliberately no #pragma comment(linker, "/manifestdependency:...")
// here: keeping a single manifest source avoids duplicate-manifest link
// errors (R11). CheckCommonControlsVersion() verifies it at startup.

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define VERSION                 L"v" DDLAUNCH_VERSION_SHORT   // from version.h (R16)
#define IDC_LISTVIEW            1001
#define IDC_BTN_RDP             1002
#define IDC_BTN_EXIT            1003
#define IDC_BTN_EDIT_CONN       1004
#define IDC_BTN_EDIT_ACTIONS    1005
#define IDC_BTN_RUN             1006
#define IDC_LBL_HOST            1007
#define IDC_HOST_BOX            1008
#define IDC_LBL_ACTION          1009
#define IDC_COMBO_ACTION        1010
#define IDC_BTN_GO              1011
#define IDC_LINK_ABOUT          1012
#define IDC_GROUP_TREE          1013

// Shown in the About box (F6); defined once in version.h (R16).
#define APP_AUTHOR              L"" DDLAUNCH_AUTHOR
#define APP_RELEASE_DATE        L"" DDLAUNCH_RELEASE_DATE

// Column indices
#define COL_HOST		0
#define COL_TYPE		1
#define COL_DESCRIPTION	2

using json = nlohmann::json;

// Text, file, URL and DPI helpers (Utf8ToWide, ReadFileUtf8, FileStamp,
// BuildLaunch, Dpi::...) are in DDCommon.h, shared with the Editor (R15).

// ---------------------------------------------------------------------------
// Actions (actionDefinitions.json)
// ---------------------------------------------------------------------------
struct Action {
    std::wstring action;   // e.g. L"RDP"
    std::wstring args;     // e.g. L" /v:"  note the space before /v:
    std::wstring command;  // e.g. L"C:\\Windows\\System32\\MSTSC.exe"
};

// B20: read tolerantly. A missing (or null) "action", "args" or "command"
// counts as empty -- a URL action has no use for "args". An entry that isn't
// an object, or has a value that isn't text, is skipped and described in
// `warnings` (one bullet line each), so one bad entry no longer rejects the
// whole file. Throws only if the file can't be read, isn't valid JSON, or
// isn't a JSON array.
std::vector<Action> loadActions(const std::wstring& filePath, std::wstring& warnings) {
    const json j = json::parse(ReadFileUtf8(filePath));
    if (!j.is_array()) {
        throw std::runtime_error("Expected a top-level JSON array [ ... ] in " + WideToUtf8(filePath));
    }

    std::vector<Action> actions;
    warnings.clear();
    size_t entry = 0;
    for (const json& e : j) {
        ++entry;
        const std::wstring where = L"\n  \u2022 Entry " + std::to_wstring(entry);
        if (!e.is_object()) {
            warnings += where + L" is not an object { ... }";
            continue;
        }
        Action a;
        bool ok = true;
        auto field = [&](const char* key, std::wstring& out) {
            const auto it = e.find(key);
            if (it == e.end() || it->is_null()) return;  // missing: empty
            if (!it->is_string()) {
                warnings += where + L": \"" + Utf8ToWide(key) + L"\" is not text (put it in quotes)";
                ok = false;
                return;
            }
            out = Utf8ToWide(it->get<std::string>());
        };
        field("action", a.action);
        field("args", a.args);
        field("command", a.command);
        if (ok) actions.push_back(std::move(a));
    }
    return actions;
}

// First action whose name matches actionName ignoring case (B5), or nullptr.
const Action* findAction(const std::vector<Action>& actions, const std::wstring& actionName) {
    auto it = std::find_if(actions.begin(), actions.end(), [&actionName](const Action& a) {
        return EqualsIgnoreCase(a.action, actionName);
    });
    return it == actions.end() ? nullptr : &*it;
}

// Each defined action name once (no blanks, first of any case-insensitive
// duplicates), sorted A-Z ignoring case. Used by the Action dropdown and the
// "Unknown type" message (R20).
static std::vector<std::wstring> UniqueActionNames(const std::vector<Action>& actions) {
    std::vector<std::wstring> names;
    for (const Action& a : actions) {
        if (!Trim(a.action).empty() && findAction(actions, a.action) == &a) {
            names.push_back(a.action);
        }
    }
    std::stable_sort(names.begin(), names.end(), LessIgnoreCase);
    return names;
}

// ---------------------------------------------------------------------------
// Device record
// ---------------------------------------------------------------------------
struct Device {
    std::wstring host;
    std::wstring type;
    std::wstring description;
    std::wstring group;         // group name (object key) in a grouped file, else L"" (F7)
    int          fileIndex = 0; // position in the file, to list groups in file order (F7)
    UINT_PTR     id = 0;  // stable ID, stored in the list row's lParam (B14)
};

// ---------------------------------------------------------------------------
// Helper: wide -> upper
// ---------------------------------------------------------------------------
static std::wstring ToUpper(std::wstring s) {
    for (auto& c : s) c = towupper(c);
    return s;
}

// ---------------------------------------------------------------------------
// Load connections.json
// ---------------------------------------------------------------------------
// hOwner parents the message boxes so they are modal to the main window.
// Returns std::nullopt if the file can't be read, isn't valid JSON, or its top
// level is neither an array of connections nor an object of named groups
// (B22), so the caller can keep the list it already has (B8). Values that
// aren't connection objects are skipped and listed once in a warning (B23).
static std::optional<std::vector<Device>> LoadConnections(HWND hOwner, const std::wstring& jsonPath) {
    std::vector<Device> devices;

    std::string contents;
    try {
        contents = ReadFileUtf8(jsonPath);
    } catch (const std::exception&) {
        MessageBoxW(hOwner,
            (L"Could not open " + jsonPath +
             L"\n\nThe previously loaded list is kept.").c_str(),
            L"DDLaunch+", MB_ICONERROR | MB_OK);
        return std::nullopt;
    }

    // B23: skipped values, one bullet line each (the first 10 are listed).
    std::wstring skipped;
    int skippedCount = 0;
    auto skip = [&](const std::wstring& what) {
        if (++skippedCount <= 10) skipped += L"\n  • " + what;
    };

    try {
        // F7: ordered_json keeps the groups in file order (nlohmann::json
        // would sort the object keys A-Z), for the tree and the unsorted list.
        const nlohmann::ordered_json data = nlohmann::ordered_json::parse(contents);
        if (!data.is_object() && !data.is_array()) {  // B22
            throw std::runtime_error(
                "The top level must be an array [ ... ] of connections, or an object "
                "{ \"Group name\": [ ... ] } of named groups.");
        }

        // `pos` is the entry's 1-based position in its array, for messages.
        auto parseDevice = [&](const nlohmann::ordered_json& obj, const std::wstring& group, size_t pos) {
            if (!obj.is_object()) {
                skip((group.empty() ? std::wstring(L"Entry ") : L"\"" + group + L"\" entry ") +
                     std::to_wstring(pos) + L" is not an object { ... }");
                return;
            }
            Device d;
            d.group = group;                                   // F7
            d.fileIndex = static_cast<int>(devices.size());
            if (obj.contains("host")    && obj["host"].is_string())
                d.host    = Utf8ToWide(obj["host"].get<std::string>());
            if (obj.contains("type")    && obj["type"].is_string())
                d.type    = Utf8ToWide(obj["type"].get<std::string>());
            if (obj.contains("description") && obj["description"].is_string())
                d.description = Utf8ToWide(obj["description"].get<std::string>());
            devices.push_back(d);
        };

        if (data.is_object()) {
            // Each key is a group name (F7), e.g. "RDP connections".
            for (auto& [key, val] : data.items()) {
                const std::wstring group = Utf8ToWide(key);
                if (val.is_object()) {
                    parseDevice(val, group, 1);
                } else if (val.is_array()) {
                    size_t pos = 0;
                    for (auto& item : val) parseDevice(item, group, ++pos);
                } else {
                    skip(L"Group \"" + group + L"\" is not an array [ ... ] of connections");
                }
            }
        } else {
            size_t pos = 0;
            for (auto& item : data) parseDevice(item, std::wstring(), ++pos);
        }

    } catch (const std::exception& e) {
        MessageBoxW(hOwner,
            (L"connections.json is not valid:\n\n" + ExceptionText(e) +
             L"\n\nThe previously loaded list is kept until the file is fixed.").c_str(),
            L"DDLaunch+ JSON Error", MB_ICONERROR | MB_OK);
        return std::nullopt;
    }

    if (skippedCount > 0) {
        if (skippedCount > 10) {
            skipped += L"\n  …and " + std::to_wstring(skippedCount - 10) + L" more";
        }
        MessageBoxW(hOwner,
            (L"connections.json: " + std::to_wstring(skippedCount) +
             (skippedCount == 1 ? L" value was" : L" values were") +
             L" skipped because they aren't connections:" + skipped +
             L"\n\nEach connection must be an object such as\n"
             L"{ \"host\": \"server01\", \"type\": \"RDP\", \"description\": \"...\" }").c_str(),
            L"DDLaunch+ - Entries skipped", MB_ICONWARNING | MB_OK);
    }
    return devices;
}

// ---------------------------------------------------------------------------
// Config file paths
//
// Both configs live in a per-user data folder: %USERPROFILE%\DDLaunch
// (e.g. C:\Users\glenn\DDLaunch). The folder and both files are created on
// demand by EnsureDataFiles() if they don't exist. GetDataDir() is in
// DDCommon.h.
// ---------------------------------------------------------------------------

static std::wstring GetConnectionsJsonPath() {
    return GetDataDir() + L"\\connections.json";
}

static std::wstring GetActionsJsonPath() {
    return GetDataDir() + L"\\actionDefinitions.json";
}

// ---------------------------------------------------------------------------
// Default content written when a config file doesn't exist yet.
//
// actionDefinitions.json: must be a top-level JSON array (see loadActions()).
// Seeded with a few common actions so a fresh install works out of the box.
// "action" is matched against the connection's "type" ignoring case (B5).
//
// connections.json: either a JSON array of device objects, or an object of
// named groups. Seeded with an empty array -- valid either way.
// ---------------------------------------------------------------------------
static const char* const DEFAULT_ACTIONS_JSON =
    "[\n"
    "    {\n"
    "        \"action\": \"RDP\",\n"
    "        \"args\": \" /v:\",\n"
    "        \"command\": \"C:\\\\Windows\\\\System32\\\\mstsc.exe\"\n"
    "    },\n"
    "    {\n"
    "        \"action\": \"SSH\",\n"
    "        \"args\": \" \",\n"
    "        \"command\": \"C:\\\\Windows\\\\System32\\\\OpenSSH\\\\ssh.exe\"\n"
    "    },\n"
    "    {\n"
    "        \"action\": \"HTTPS\",\n"
    "        \"args\": \"\",\n"
    "        \"command\": \"https://\"\n"
    "    },\n"
    "    {\n"
    "        \"action\": \"HTTP\",\n"
    "        \"args\": \"\",\n"
    "        \"command\": \"http://\"\n"
    "    }\n"
    "]\n";

static const char* const DEFAULT_CONNECTIONS_JSON = "[]\n";

// ---------------------------------------------------------------------------
// File / folder helpers (FileExists / DirectoryExists are in DDCommon.h)
// ---------------------------------------------------------------------------

static bool CreateFileWithDefaultContent(const std::wstring& path, const std::string& utf8Content) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        // ERROR_FILE_EXISTS means someone else created it first -- that's fine.
        return GetLastError() == ERROR_FILE_EXISTS;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(hFile, utf8Content.data(), static_cast<DWORD>(utf8Content.size()), &written, nullptr);
    CloseHandle(hFile);
    return ok && written == utf8Content.size();
}

// Make sure %USERPROFILE%\DDLaunch exists, and that actionDefinitions.json and
// connections.json exist inside it (creating each with default content if
// missing). Existing files are never overwritten. Shows one error message
// describing whatever couldn't be created; returns false in that case.
static bool EnsureDataFiles(HWND hWnd) {
    const std::wstring dataDir = GetDataDir();
    std::wstring errors;

    if (!DirectoryExists(dataDir)) {
        DWORD err = CreateDirectoryW(dataDir.c_str(), nullptr) ? ERROR_SUCCESS : GetLastError();
        if (err != ERROR_SUCCESS && err != ERROR_ALREADY_EXISTS) {
            wchar_t code[32];
            swprintf_s(code, L" (error %lu)", err);
            MessageBoxW(hWnd,
                (L"Could not create the DDLaunch data folder:\n" + dataDir + code).c_str(),
                L"DDLaunch+", MB_ICONERROR | MB_OK);
            return false;
        }
    }

    const std::wstring actionsPath = GetActionsJsonPath();
    if (!FileExists(actionsPath) && !CreateFileWithDefaultContent(actionsPath, DEFAULT_ACTIONS_JSON)) {
        errors += L"\n" + actionsPath;
    }

    const std::wstring connectionsPath = GetConnectionsJsonPath();
    if (!FileExists(connectionsPath) && !CreateFileWithDefaultContent(connectionsPath, DEFAULT_CONNECTIONS_JSON)) {
        errors += L"\n" + connectionsPath;
    }

    if (!errors.empty()) {
        MessageBoxW(hWnd, (L"Could not create:" + errors).c_str(),
                    L"DDLaunch+", MB_ICONERROR | MB_OK);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Open a config file for editing.
//
// F5: if DDLaunchEdit.exe (the DDLaunch+ Editor) is in the same folder as
// DDLaunch.exe, it is started on the matching tab (`editorTab` is
// L"/connections" or L"/actions"); a running editor just switches to that tab.
// Otherwise -- or if it can't be started -- the file opens in Notepad as
// before. Notepad is launched directly (rather than ShellExecute's "open"
// verb on the .json) so this works even if .json has no file association or
// is associated with something other than a text editor. EnsureDataFiles()
// runs first so the folder and file exist (with valid default JSON) even if
// the user deleted them while the app was running.
// ---------------------------------------------------------------------------
static void OpenJsonFileForEditing(HWND hWnd, const std::wstring& path, const wchar_t* editorTab) {
    EnsureDataFiles(hWnd);

    const std::wstring exeDir = GetExeDir();
    const std::wstring editor = exeDir + L"\\DDLaunchEdit.exe";
    if (FileExists(editor)) {
        HINSTANCE r = ShellExecuteW(hWnd, L"open", editor.c_str(), editorTab, exeDir.c_str(), SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) > 32) {
            return;
        }
        // Couldn't start the editor: fall back to Notepad below.
    }

    // Quote the path: it is under %USERPROFILE%, which often contains spaces
    // (e.g. C:\Users\First Last\DDLaunch\connections.json).
    std::wstring quotedPath = L"\"" + path + L"\"";
    HINSTANCE result = ShellExecuteW(hWnd, L"open", L"notepad.exe", quotedPath.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hWnd, (L"Could not open Notepad for:\n" + path).c_str(),
                    L"DDLaunch+", MB_ICONERROR | MB_OK);
    }
}

// ---------------------------------------------------------------------------
// Launching with error reporting (B3)
//
// Uses ShellExecuteExW instead of ShellExecuteW so failures come back as a
// Win32 error code. SEE_MASK_FLAG_NO_UI suppresses the shell's own error
// dialogs so DDLaunch+ can show one consistent message that names the host,
// type, command and parameters involved.
// ---------------------------------------------------------------------------

// Runs `file` (an executable or a URL) with optional parameters.
// Returns ERROR_SUCCESS, or the Win32 error code if the launch failed.
static DWORD ShellOpen(HWND hOwner, const std::wstring& file, const std::wstring& params) {
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_FLAG_NO_UI;
    sei.hwnd         = hOwner;
    sei.lpVerb       = L"open";
    sei.lpFile       = file.c_str();
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {
        return ERROR_SUCCESS;
    }
    DWORD err = GetLastError();
    return err != ERROR_SUCCESS ? err : ERROR_GEN_FAILURE;
}

// Shows the standard "launch failed" message. ERROR_CANCELLED (e.g. the
// user declined a UAC prompt) is not an error and is ignored.
static void ReportLaunchFailure(HWND hOwner, DWORD err, const std::wstring& what,
                                const std::wstring& file, const std::wstring& params,
                                const std::wstring& hint) {
    if (err == ERROR_SUCCESS || err == ERROR_CANCELLED) {
        return;
    }
    wchar_t code[32];
    swprintf_s(code, L"Error %lu: ", err);

    std::wstring msg = L"Could not launch " + what + L".\n\n";
    msg += L"Command:\t" + file + L"\n";
    if (!params.empty()) {
        msg += L"Parameters:\t" + params + L"\n";
    }
    msg += L"\n" + std::wstring(code) + GetErrorText(err);
    if (!hint.empty()) {
        msg += L"\n\n" + hint;
    }
    MessageBoxW(hOwner, msg.c_str(), L"DDLaunch+ - Launch failed", MB_ICONERROR | MB_OK);
}

// command is either an executable (run with args + host as parameters) or
// a URL prefix (command + host is opened with its registered handler). The
// rules -- environment variables, trimming, quoting, URL encoding -- are in
// BuildLaunch() in DDCommon.h, which the Editor's preview also uses (R15).
// Returns ERROR_SUCCESS or a Win32 error code; `file`/`params` receive what
// was actually passed to the shell so the caller can report it.
static DWORD LaunchSomething(HWND hOwner, const std::wstring& command, const std::wstring& args,
                             const std::wstring& host, std::wstring& file, std::wstring& params) {
    const LaunchSpec spec = BuildLaunch(command, args, host);
    file   = spec.file;
    params = spec.params;
    return ShellOpen(hOwner, file, params);
}

// ---------------------------------------------------------------------------
// Show the standard Windows "Run" dialog (same as Win+R).
//
// Uses the documented Shell.Application automation object
// (IShellDispatch::FileRun) rather than the undocumented shell32 ordinal 61
// (RunFileDlg). __uuidof() is used so no extra GUID library is needed.
// COM is already initialized for the UI thread by wWinMain (B15).
// ---------------------------------------------------------------------------
static void ShowRunDialog(HWND hWnd) {
    IShellDispatch* pShell = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(Shell), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(IShellDispatch), reinterpret_cast<void**>(&pShell));
    if (SUCCEEDED(hr) && pShell) {
        hr = pShell->FileRun();
        pShell->Release();
    }

    if (FAILED(hr)) {
        wchar_t msg[128];
        swprintf_s(msg, L"Could not open the Run dialog (HRESULT 0x%08X).", static_cast<unsigned>(hr));
        MessageBoxW(hWnd, msg, L"DDLaunch+", MB_ICONERROR | MB_OK);
    }
}

// ---------------------------------------------------------------------------
// Main Window
// ---------------------------------------------------------------------------
static HWND      g_hListView       = nullptr;
static HWND      g_hBtnRDP         = nullptr;
static HWND      g_hBtnExit        = nullptr;
static HWND      g_hBtnEditConn    = nullptr;
static HWND      g_hBtnEditActions = nullptr;
static HWND      g_hBtnRun         = nullptr;
static HWND      g_hLblHost        = nullptr;  // quick-launch row (F1)
static HWND      g_hHostBox       = nullptr;
static HWND      g_hLblAction      = nullptr;
static HWND      g_hComboAction    = nullptr;
static HWND      g_hBtnGo          = nullptr;
static HWND      g_hLinkAbout      = nullptr;  // "About" link (F6)

// Group tree (F7)
static HWND      g_hGroupTree      = nullptr;
static std::optional<std::wstring> g_selGroup;  // group shown in the list; nullopt = all
static int       g_treeW96         = 190;      // tree width in 96-DPI units (saved)
static int       g_treePx          = 0;        // current tree width in pixels (0 = hidden)
static bool      g_buildingTree    = false;    // ignore selection notifications while refilling
static bool      g_splitDrag       = false;    // dragging the bar between tree and list
static int       g_lblHostW        = 0;        // label widths in pixels (fit text)
static int       g_lblActionW      = 0;
static HWND      g_hLastFocus      = nullptr;  // restored when re-activated
static HFONT     g_hFont           = nullptr;  // UI font for the current DPI
static UINT      g_dpi             = USER_DEFAULT_SCREEN_DPI;
static int       g_btnW            = 250;      // button width in pixels (fits text)
static RECT      g_normalRect      = {};       // last restored (not min/max) window rect
static UINT      g_normalRectDpi   = USER_DEFAULT_SCREEN_DPI;  // DPI when it was recorded
static bool      g_placingWindow   = false;    // true while wWinMain positions the window
static std::vector<Device> g_devices;
static std::vector<Action> g_actions;

// ---------------------------------------------------------------------------
// Sort state
// ---------------------------------------------------------------------------
static int  g_sortCol = -1;      // last sorted column (-1 = none)
static bool g_sortAsc = true;    // true = ascending, false = descending

// Sort g_devices using the current sort state (g_sortCol / g_sortAsc) without
// changing it. With no sorted column, g_devices is put back in file order
// (F8). Called after a column click and after every reload, so freshly
// loaded data keeps the order the header arrow shows (B2). std::stable_sort
// keeps rows with equal keys in file order, so reloads never shuffle them.
static void ApplyCurrentSort() {
    const int col = g_sortCol;
    if (col < COL_HOST || col > COL_DESCRIPTION) {
        std::stable_sort(g_devices.begin(), g_devices.end(),
            [](const Device& a, const Device& b) { return a.fileIndex < b.fileIndex; });
        return;
    }

    const bool ascending = g_sortAsc;
    std::stable_sort(g_devices.begin(), g_devices.end(),
        [col, ascending](const Device& a, const Device& b) -> bool {
            const std::wstring* pa = nullptr;
            const std::wstring* pb = nullptr;
            switch (col) {
            case COL_HOST:        pa = &a.host;        pb = &b.host;        break;
            case COL_TYPE:        pa = &a.type;        pb = &b.type;        break;
            case COL_DESCRIPTION: pa = &a.description; pb = &b.description; break;
            default:              return false;
            }
            // B16: same case-insensitive rule as action-name matching, so the
            // order doesn't depend on the locale.
            const int cmp = CompareIgnoreCase(*pa, *pb);
            return ascending ? (cmp < 0) : (cmp > 0);
        });
}

// Column header click: a new column sorts ascending, a second click on the
// same column sorts descending, and a third goes back to file order (F8).
static void ToggleSortColumn(int col) {
    if (g_sortCol != col) {
        g_sortCol = col;
        g_sortAsc = true;        // new column: ascending
    } else if (g_sortAsc) {
        g_sortAsc = false;       // second click: descending
    } else {
        g_sortCol = -1;          // third click: file order
        g_sortAsc = true;
    }
    ApplyCurrentSort();

}

// Update header arrows to reflect the current sort column and direction.
static void UpdateHeaderSortArrow(HWND hLV, int sortedCol, bool ascending) {
    HWND hHeader = ListView_GetHeader(hLV);
    if (!hHeader) return;

    int colCount = Header_GetItemCount(hHeader);
    for (int i = 0; i < colCount; ++i) {
        HDITEMW hdi = {};
        hdi.mask = HDI_FORMAT;
        Header_GetItem(hHeader, i, &hdi);

        // Clear both arrow flags first
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);

        if (i == sortedCol) {
            hdi.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(hHeader, i, &hdi);
    }
}

// ---------------------------------------------------------------------------
// Device IDs (B14)
//
// Every device gets an ID that is stored in its list row's lParam. The
// selection is tracked by that ID, so it survives sorting without reading
// cell text back from the control. On reload, IDs are carried over from the
// previous list: first to devices with the same host+type+description, then
// to devices with the same host+type (e.g. only the description was edited).
// Anything else gets a new ID.
// ---------------------------------------------------------------------------
static UINT_PTR g_nextDeviceId = 1;

static void AssignDeviceIds(std::vector<Device>& fresh, const std::vector<Device>& previous) {
    std::vector<bool> taken(previous.size(), false);

    auto claim = [&](Device& d, bool matchDescription) {
        for (size_t i = 0; i < previous.size(); ++i) {
            const Device& p = previous[i];
            if (!taken[i] && p.id != 0 && p.host == d.host && p.type == d.type &&
                (!matchDescription || p.description == d.description)) {
                taken[i] = true;
                d.id = p.id;
                return;
            }
        }
    };

    for (Device& d : fresh) claim(d, true);            // pass 1: exact match
    for (Device& d : fresh) if (!d.id) claim(d, false); // pass 2: host+type
    for (Device& d : fresh) if (!d.id) d.id = g_nextDeviceId++;
}

// Index in g_devices of the device with this ID, or -1.
static int FindDeviceIndexById(UINT_PTR id) {
    for (int i = 0; i < (int)g_devices.size(); ++i) {
        if (g_devices[i].id == id) return i;
    }
    return -1;
}

// ID stored in a list row (0 if none).
static UINT_PTR GetRowDeviceId(HWND hLV, int row) {
    LVITEMW lvi = {};
    lvi.mask  = LVIF_PARAM;
    lvi.iItem = row;
    return ListView_GetItem(hLV, &lvi) ? static_cast<UINT_PTR>(lvi.lParam) : 0;
}

// Device most recently selected in the list (0 = none). Kept while the
// selected group hides it, so it is selected again when it comes back into
// view (B28).
static UINT_PTR g_lastSelId = 0;

// Refill the list from g_devices, keeping the selected device selected (B9),
// identified by the ID in its row's lParam (B14). The row keeps keyboard
// focus and is scrolled into view. groupChanged: a different group was picked
// in the tree; if no row is selected, the device remembered in g_lastSelId is
// selected again when the new group contains it (B28).
static void PopulateListView(HWND hLV, bool groupChanged = false) {
    // 1. Remember which device was selected.
    const int oldSel = ListView_GetNextItem(hLV, -1, LVNI_SELECTED);
    UINT_PTR selId = oldSel >= 0 ? GetRowDeviceId(hLV, oldSel) : 0;
    if (selId != 0) {
        g_lastSelId = selId;
    } else if (groupChanged) {
        selId = g_lastSelId;
    }

    // 2. Refill without repainting each row.
    SendMessageW(hLV, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hLV);

    int newSel = -1;
    int row = 0;
    for (int i = 0; i < (int)g_devices.size(); ++i) {
        const Device& d = g_devices[i];
        if (g_selGroup && d.group != *g_selGroup) continue;  // F7: only the selected group

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem    = row;
        lvi.iSubItem = 0;
        lvi.pszText  = const_cast<LPWSTR>(d.host.c_str());
        lvi.lParam   = static_cast<LPARAM>(d.id);
        ListView_InsertItem(hLV, &lvi);

        ListView_SetItemText(hLV, row, COL_TYPE,        const_cast<LPWSTR>(d.type.c_str()));
        ListView_SetItemText(hLV, row, COL_DESCRIPTION, const_cast<LPWSTR>(d.description.c_str()));

        if (selId != 0 && d.id == selId) newSel = row;
        ++row;
    }

    // 3. Restore the selection.
    if (newSel >= 0) {
        ListView_SetItemState(hLV, newSel, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(hLV, newSel, FALSE);
    }

    SendMessageW(hLV, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hLV, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// Group tree (F7)
//
// "All connections (N)" with one child per group, "<group> (count)", in the
// order the groups appear in connections.json. Item lParam: 0 = all, k = the
// k-th group (1-based) in g_groupNames. Shown only if any entry has a group.
// ---------------------------------------------------------------------------
static std::vector<std::wstring> g_groupNames;

static void RebuildGroupTree() {
    if (!g_hGroupTree) return;

    // Groups in file order, with counts.
    struct G { std::wstring name; int first; int count; };
    std::vector<G> groups;
    for (const Device& d : g_devices) {
        if (d.group.empty()) continue;
        auto it = std::find_if(groups.begin(), groups.end(), [&](const G& g) { return g.name == d.group; });
        if (it == groups.end()) groups.push_back({ d.group, d.fileIndex, 1 });
        else { ++it->count; it->first = (std::min)(it->first, d.fileIndex); }
    }
    std::sort(groups.begin(), groups.end(), [](const G& a, const G& b) { return a.first < b.first; });

    const bool wasShown = g_treePx > 0 || IsWindowVisible(g_hGroupTree);
    g_buildingTree = true;
    SendMessageW(g_hGroupTree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_hGroupTree);
    g_groupNames.clear();

    if (groups.empty()) {
        // Flat file: no tree, the list shows everything.
        g_selGroup.reset();
        SendMessageW(g_hGroupTree, WM_SETREDRAW, TRUE, 0);
        g_buildingTree = false;
        // B27: a hidden control can't keep the keyboard focus -- hand it
        // (and the "restore focus here" bookmark) to the list.
        if (g_hLastFocus == g_hGroupTree) g_hLastFocus = g_hListView;
        if (GetFocus() == g_hGroupTree) SetFocus(g_hListView);
        ShowWindow(g_hGroupTree, SW_HIDE);
        if (wasShown) SendMessageW(GetParent(g_hGroupTree), WM_SIZE, 0, 0);
        return;
    }

    auto insert = [&](HTREEITEM parent, const std::wstring& text, LPARAM param) {
        TVINSERTSTRUCTW tvi = {};
        tvi.hParent = parent;
        tvi.hInsertAfter = TVI_LAST;
        tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
        tvi.item.pszText = const_cast<LPWSTR>(text.c_str());
        tvi.item.lParam = param;
        return TreeView_InsertItem(g_hGroupTree, &tvi);
    };

    HTREEITEM all = insert(TVI_ROOT, L"All connections (" + std::to_wstring(g_devices.size()) + L")", 0);
    HTREEITEM toSelect = all;
    for (size_t k = 0; k < groups.size(); ++k) {
        g_groupNames.push_back(groups[k].name);
        HTREEITEM h = insert(all, groups[k].name + L" (" + std::to_wstring(groups[k].count) + L")",
                             static_cast<LPARAM>(k + 1));
        if (g_selGroup && *g_selGroup == groups[k].name) toSelect = h;
    }
    if (toSelect == all) g_selGroup.reset();  // the remembered group no longer exists
    TreeView_Expand(g_hGroupTree, all, TVE_EXPAND);
    TreeView_SelectItem(g_hGroupTree, toSelect);
    TreeView_EnsureVisible(g_hGroupTree, toSelect);

    SendMessageW(g_hGroupTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hGroupTree, nullptr, TRUE);
    g_buildingTree = false;

    if (!IsWindowVisible(g_hGroupTree)) {
        ShowWindow(g_hGroupTree, SW_SHOW);
        SendMessageW(GetParent(g_hGroupTree), WM_SIZE, 0, 0);  // make room for it
    }
}

// The user picked a tree item: show that group (or all) in the list.
static void OnGroupTreeSelChanged(const NMTREEVIEWW* nm) {
    if (g_buildingTree) return;
    const LPARAM k = nm->itemNew.lParam;
    if (k >= 1 && static_cast<size_t>(k) <= g_groupNames.size()) {
        g_selGroup = g_groupNames[static_cast<size_t>(k - 1)];
    } else {
        g_selGroup.reset();
    }
    PopulateListView(g_hListView, true);  // B28
}

// ---------------------------------------------------------------------------
// File change detection (B1)
//
// A FileStamp records a file's last-write time and size. The main window
// reloads a JSON file only when its stamp differs from the one recorded at the
// last load. The stamp is recorded BEFORE the file is parsed, so an invalid
// file reports its error once per save. Before this change every WM_ACTIVATE
// reloaded, so closing the error box re-activated the window, which reloaded,
// failed and showed the box again in an endless loop.
// ---------------------------------------------------------------------------
// (FileStamp and GetFileStamp are in DDCommon.h.)

// B13: action names are matched ignoring case, so "RDP" and "rdp" are the
// same action and only the first one is ever used. Report any such
// duplicates once per load (ReloadAllData only loads a file after it
// changes, so the warning is not repeated on every activation).
static void WarnAboutDuplicateActions(HWND hWnd, const std::vector<Action>& actions) {
    std::wstring list;
    for (size_t i = 1; i < actions.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            const std::wstring& a = actions[i].action;
            const std::wstring& b = actions[j].action;
            if (CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                     b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL) {
                list += L"\n  \u2022 \"" + a + L"\" (entry " + std::to_wstring(i + 1) +
                        L") duplicates \"" + b + L"\" (entry " + std::to_wstring(j + 1) + L")";
                break;  // report each later entry once
            }
        }
    }
    if (!list.empty()) {
        MessageBoxW(hWnd,
            (L"actionDefinitions.json contains duplicate action names "
             L"(names are matched ignoring upper/lower case):" + list +
             L"\n\nOnly the first entry with each name is used. "
             L"Remove or rename the duplicates.").c_str(),
            L"DDLaunch+ - Duplicate actions", MB_ICONWARNING | MB_OK);
    }
}

// ---------------------------------------------------------------------------
// Quick-launch row: Action dropdown (F1)
// ---------------------------------------------------------------------------

// (GetWindowTextString is in DDCommon.h.)

// Name of the action currently chosen in the dropdown, or L"" if none.
static std::wstring GetSelectedActionName() {
    if (!g_hComboAction) {
        return std::wstring();
    }
    const int sel = static_cast<int>(SendMessageW(g_hComboAction, CB_GETCURSEL, 0, 0));
    if (sel < 0) {
        return std::wstring();
    }
    const int len = static_cast<int>(SendMessageW(g_hComboAction, CB_GETLBTEXTLEN, sel, 0));
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring name(static_cast<size_t>(len) + 1, L'\0');
    SendMessageW(g_hComboAction, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(&name[0]));
    name.resize(static_cast<size_t>(len));
    return name;
}

// Fill the dropdown from g_actions, sorted A-Z (F3). Each name appears once:
// blank names and later case-insensitive duplicates are skipped, since
// findAction() only ever uses the first (B13). Sorting uses the same rule as
// the list columns and action matching -- CompareStringOrdinal ignoring case
// (B16) -- so the order doesn't depend on the user's locale.
// Selection: the previously chosen action if it still exists; otherwise the
// first action in the file (so the file's first entry stays the default even
// though it may not be first alphabetically).
static void PopulateActionCombo() {
    if (!g_hComboAction) {
        return;
    }
    const std::wstring previous = GetSelectedActionName();

    // Unique, non-blank names, A-Z (R20: same list as the "Unknown type"
    // message). The default is the first named action in the file.
    const std::vector<std::wstring> names = UniqueActionNames(g_actions);
    std::wstring fileDefault;
    for (const Action& a : g_actions) {
        if (!Trim(a.action).empty()) {
            fileDefault = a.action;
            break;
        }
    }

    SendMessageW(g_hComboAction, CB_RESETCONTENT, 0, 0);
    int prevIndex = -1, defaultIndex = -1;
    for (const std::wstring& name : names) {
        // CB_ADDSTRING appends (no CBS_SORT), so the order above is kept.
        const int idx = static_cast<int>(SendMessageW(g_hComboAction, CB_ADDSTRING, 0,
                                                      reinterpret_cast<LPARAM>(name.c_str())));
        if (idx < 0) continue;
        if (prevIndex < 0 && !previous.empty() && EqualsIgnoreCase(name, previous)) prevIndex = idx;
        if (defaultIndex < 0 && name == fileDefault) defaultIndex = idx;
    }

    if (!names.empty()) {
        const int sel = prevIndex >= 0 ? prevIndex : (defaultIndex >= 0 ? defaultIndex : 0);
        SendMessageW(g_hComboAction, CB_SETCURSEL, sel, 0);
    }
}

static FileStamp g_connStamp;          // connections.json at last load
static FileStamp g_actionsStamp;       // actionDefinitions.json at last load
static bool      g_dataLoaded = false; // false until the first successful pass
static bool      g_inReload   = false; // re-entrancy guard (see below)

// Reload connections.json and/or actionDefinitions.json from disk and refresh
// the list view.
//
//   force == true  : load both files unconditionally (startup).
//   force == false : load only the file(s) whose stamp changed since the last
//                    load (window activation). If nothing changed, return
//                    immediately without touching the disk or the UI.
//
// Called at startup and whenever the window regains focus, so changes made
// with the "Edit Connections" / "Edit Actions" buttons (in the DDLaunch+
// Editor or Notepad, separate processes) show up without a restart.
static void ReloadAllData(HWND hWnd, bool force) {
    // MessageBox runs a modal loop that can deliver WM_ACTIVATE to this
    // window while a reload is still in progress; ignore those nested calls.
    if (g_inReload) {
        return;
    }

    const std::wstring connPath    = GetConnectionsJsonPath();
    const std::wstring actionsPath = GetActionsJsonPath();

    // Cheap early-out on activation: nothing on disk changed.
    if (!force && g_dataLoaded &&
        GetFileStamp(connPath) == g_connStamp &&
        GetFileStamp(actionsPath) == g_actionsStamp) {
        return;
    }

    // RAII so the guard is cleared on every exit path.
    struct ReloadGuard {
        ReloadGuard()  { g_inReload = true; }
        ~ReloadGuard() { g_inReload = false; }
    } guard;

    // Create %USERPROFILE%\DDLaunch and/or either JSON file if missing
    // (e.g. the user deleted one while the app was running).
    EnsureDataFiles(hWnd);

    // Stamps are taken after EnsureDataFiles so a freshly seeded file is not
    // seen as "changed" on the next activation. Recording them before parsing
    // means a broken file is not re-parsed (and re-reported) until it is saved
    // again.
    const FileStamp connNow    = GetFileStamp(connPath);
    const FileStamp actionsNow = GetFileStamp(actionsPath);
    const bool reloadConn    = force || !g_dataLoaded || connNow    != g_connStamp;
    const bool reloadActions = force || !g_dataLoaded || actionsNow != g_actionsStamp;
    g_connStamp    = connNow;
    g_actionsStamp = actionsNow;
    g_dataLoaded   = true;

    bool devicesChanged = false;
    if (reloadConn) {
        // B8: on a read/parse error keep the current list (the error has
        // already been shown once for this version of the file).
        if (auto loaded = LoadConnections(hWnd, connPath)) {
            AssignDeviceIds(*loaded, g_devices);  // B14: keep IDs of unchanged devices
            g_devices = std::move(*loaded);
            // B2: keep the user's chosen sort order (and the header arrow,
            // which is left unchanged) instead of reverting to file order.
            ApplyCurrentSort();
            devicesChanged = true;
        }
    }

    if (reloadActions) {
        try {
            std::wstring skipped;
            g_actions = loadActions(actionsPath, skipped);  // B20
            PopulateActionCombo();                          // F1
            if (!skipped.empty()) {
                MessageBoxW(hWnd,
                    (L"actionDefinitions.json: these entries were skipped:" + skipped +
                     L"\n\nEach action must be an object such as\n"
                     L"{ \"action\": \"RDP\", \"args\": \" /v:\", \"command\": \"C:\\\\Windows\\\\System32\\\\mstsc.exe\" }\n"
                     L"The other actions were loaded.").c_str(),
                    L"DDLaunch+ - Actions skipped", MB_ICONWARNING | MB_OK);
            }
            WarnAboutDuplicateActions(hWnd, g_actions);     // B13
        } catch (const std::exception& e) {
            // actionDefinitions.json is missing or not valid JSON (e.g.
            // mid-edit). Keep whatever actions we already had rather than
            // crashing the app. Shown once per save thanks to the stamp.
            MessageBoxW(hWnd,
                (ExceptionText(e) + L"\n\nThe previously loaded actions are kept until the file is fixed.").c_str(),
                L"DDLaunch+ - Could not load actionDefinitions.json", MB_ICONWARNING | MB_OK);
        }
    }

    // Actions don't appear in the list, so only repaint when devices changed.
    if (devicesChanged && g_hListView) {
        RebuildGroupTree();  // F7: groups and counts; may reset a vanished group
        PopulateListView(g_hListView);
    }
}

// Run `host` with an action's command and args, and report any failure.
// `what` names the target in the error message. Shared by list entries
// (OnItemActivated) and the Go button (OnGo), so both behave identically:
// same host quoting (B11), URL rules (B18) and messages (B3).
// Returns true if the launch succeeded (F4: only those go into the history).
static bool LaunchWithAction(HWND hWnd, const Action& action, const std::wstring& host,
                             const std::wstring& what) {
    const std::wstring name = ToUpper(action.action);
    if (Trim(action.command).empty()) {  // same test as the Editor's preview
        MessageBoxW(hWnd,
            (L"The \"" + name + L"\" action in actionDefinitions.json has an empty \"command\".").c_str(),
            L"DDLaunch+ - No command", MB_ICONWARNING | MB_OK);
        return false;
    }

    std::wstring file, params;
    const DWORD err = LaunchSomething(hWnd, action.command, action.args, host, file, params);
    ReportLaunchFailure(hWnd, err, what, file, params,
        L"Check the \"command\" and \"args\" for " + name + L" in actionDefinitions.json.");
    return err == ERROR_SUCCESS;
}

// Host box history (F4) -- defined with the settings code further down.
static void AddHostHistory(HWND hWnd, const std::wstring& host, const std::wstring& action);

static void OnItemActivated(HWND hWnd, int index, const std::vector<Action>& actions) {
    if (index < 0 || index >= (int)g_devices.size()) return;
    const Device& dev = g_devices[index];
    const std::wstring type = ToUpper(dev.type);
    const std::wstring hostLabel = dev.host.empty() ? std::wstring(L"(no host)") : dev.host;

    // B3: previously an unknown type fell back to cmd = args = L"Default",
    // so ShellExecute tried to run a program called "Default" and failed
    // silently. Now each problem gets a specific message.
    if (type.empty()) {
        MessageBoxW(hWnd,
            (L"\"" + hostLabel + L"\" has no type.\n\n"
             L"Add a \"type\" (for example \"RDP\", \"SSH\" or \"HTTPS\") to this entry "
             L"in connections.json.").c_str(),
            L"DDLaunch+ - No type", MB_ICONWARNING | MB_OK);
        return;
    }

    // B5: case-insensitive, so "ssh", "Ssh" and "SSH" all find the action.
    const Action* action = findAction(actions, dev.type);
    if (!action) {
        std::wstring msg = L"No action is defined for type \"" + dev.type + L"\" (host \"" + hostLabel + L"\").\n\n";
        msg += L"Add an entry with \"action\": \"" + type + L"\" to actionDefinitions.json, "
               L"or change this connection's type in connections.json.";
        const std::vector<std::wstring> names = UniqueActionNames(actions);  // R20
        if (!names.empty()) {
            msg += L"\n\nDefined actions:";
            for (size_t i = 0; i < names.size(); ++i) {
                msg += (i ? L", " : L" ") + names[i];
            }
        }
        MessageBoxW(hWnd, msg.c_str(), L"DDLaunch+ - Unknown type", MB_ICONWARNING | MB_OK);
        return;
    }

    LaunchWithAction(hWnd, *action, dev.host, L"\"" + hostLabel + L"\" (type " + type + L")");
}

// Go button (F1): run the action chosen in the dropdown on the host typed in
// the Host box. Leading/trailing spaces are trimmed; spaces inside the host
// are handled by the normal quoting/URL rules.
static void OnGo(HWND hWnd) {
    const std::wstring host = Trim(GetWindowTextString(g_hHostBox));

    if (host.empty()) {
        MessageBoxW(hWnd,
            L"Type a host name, IP address or URL in the Host box, then click Go.",
            L"DDLaunch+ - No host", MB_ICONWARNING | MB_OK);
        SetFocus(g_hHostBox);
        return;
    }

    const std::wstring name = GetSelectedActionName();
    const Action* action = name.empty() ? nullptr : findAction(g_actions, name);
    if (!action) {
        MessageBoxW(hWnd,
            L"No action is selected.\n\n"
            L"The Action list is filled from actionDefinitions.json. Click "
            L"\"Edit Actions\" to add actions.",
            L"DDLaunch+ - No action", MB_ICONWARNING | MB_OK);
        SetFocus(g_hComboAction);
        return;
    }

    if (LaunchWithAction(hWnd, *action, host,
                         L"\"" + host + L"\" (action " + ToUpper(action->action) + L")")) {
        AddHostHistory(hWnd, host, action->action);  // F4
    }
}

// ---------------------------------------------------------------------------
// Layout (all values are 96-DPI design units, scaled with Dpi::Scale)
// ---------------------------------------------------------------------------
namespace Layout {
const int BTN_H       = 24;   // button height
const int BTN_MIN_W   = 250;  // minimum button width
const int BTN_PAD_X   = 24;   // horizontal padding added to measured text
const int GAP         = 10;   // gap between buttons in a row
const int MARGIN      = 6;    // margin around the button area
const int ROW_GAP     = 6;    // gap between the quick-launch row and the two button rows
const int MIN_LIST_H  = 80;   // smallest list height allowed by resizing
const int COL_W[3]    = { 200, 100, 300 };  // initial column widths
const int WINDOW_W    = 780;  // initial window size (wide enough for the group tree, F7)
const int WINDOW_H    = 400;
const int LINK_H      = 20;   // "About" link row under the buttons (F6)
const int SPLIT_W     = 5;    // draggable bar between the group tree and the list (F7)
const int TREE_MIN_W  = 100;  // narrowest group tree
const int LIST_MIN_W  = 240;  // the list keeps at least this much width
}

static int S(int designValue) { return Dpi::Scale(designValue, g_dpi); }

// ---------------------------------------------------------------------------
// Window placement persistence (R8)
//
// settings.json lives next to the other config files:
//   { "window": { "x": 120, "y": 80, "width": 640, "height": 420,
//                 "maximized": false },
//     "hostHistory": [ { "host": "dc01.ynext.corp", "action": "RDP" }, ... ],
//     "treeWidth": 190, "group": "PCs" }
// ("hostHistory" is the Host box history, F4; "treeWidth" and "group" are
// the group tree's width in 96-DPI units and selected group, F7.)
// x / y are screen coordinates in physical pixels (top-left of the restored
// window). width / height are in 96-DPI units so the window keeps the same
// apparent size when it reopens on a monitor with different scaling. The
// file is optional: if it is missing or unreadable the default size is used.
// ---------------------------------------------------------------------------
struct WindowSettings {
    int  x = 0, y = 0;
    int  width96 = 0, height96 = 0;  // 96-DPI units
    bool maximized = false;
};

static std::wstring GetSettingsJsonPath() {
    return GetDataDir() + L"\\settings.json";
}

// R18: settings.json is read and parsed once at startup; the three loaders
// below take the result. A missing or corrupt file gives a null json value,
// which every loader treats as "no saved settings" -- never an error box.
static json LoadSettingsFile() {
    try {
        const std::wstring path = GetSettingsJsonPath();
        if (FileExists(path)) {
            json j = json::parse(ReadFileUtf8(path));
            if (j.is_object()) return j;
        }
    } catch (const std::exception&) {
        // corrupt or hand-edited badly: use the defaults
    }
    return json();
}

static std::optional<WindowSettings> LoadWindowSettings(const json& j) {
    try {
        if (!j.is_object() || !j.contains("window")) {
            return std::nullopt;
        }
        const json& w = j.at("window");
        WindowSettings ws;
        ws.x         = w.at("x").get<int>();
        ws.y         = w.at("y").get<int>();
        ws.width96   = w.at("width").get<int>();
        ws.height96  = w.at("height").get<int>();
        ws.maximized = w.value("maximized", false);
        if (ws.width96 < 100 || ws.height96 < 100 || ws.width96 > 20000 || ws.height96 > 20000) {
            return std::nullopt;  // implausible values: ignore the file
        }
        return ws;
    } catch (const std::exception&) {
        return std::nullopt;  // corrupt or hand-edited badly: silently use defaults
    }
}

// ---------------------------------------------------------------------------
// Host box history (F4)
//
// Hosts launched successfully with Go, most recent first, each with the
// action it was launched with. Shown in the Host box's dropdown in the same
// order, so a dropdown index is also an index into g_hostHistory.
// ---------------------------------------------------------------------------
struct HostHistoryEntry {
    std::wstring host;
    std::wstring action;  // may name an action that no longer exists
};

static const size_t kMaxHostHistory = 25;
static std::vector<HostHistoryEntry> g_hostHistory;

// F7: group tree width ("treeWidth", 96-DPI units) and the selected group
// ("group"; absent = all) from settings.json. Bad values are ignored.
static void LoadTreeSettings(const json& j) {
    try {
        if (!j.is_object()) return;
        if (j.contains("treeWidth") && j["treeWidth"].is_number_integer()) {
            const int w = j["treeWidth"].get<int>();
            if (w >= 80 && w <= 2000) g_treeW96 = w;
        }
        if (j.contains("group") && j["group"].is_string()) {
            g_selGroup = Utf8ToWide(j["group"].get<std::string>());
        }
    } catch (const std::exception&) {
        // keep the defaults
    }
}

// Read "hostHistory" from settings.json. Bad entries are skipped and a
// missing or corrupt file just means no history -- never an error box.
static void LoadHostHistory(const json& j) {
    g_hostHistory.clear();
    try {
        if (!j.is_object()) {
            return;
        }
        const auto it = j.find("hostHistory");
        if (it == j.end() || !it->is_array()) {
            return;
        }
        for (const json& e : *it) {
            if (g_hostHistory.size() >= kMaxHostHistory) break;
            if (!e.is_object() || !e.contains("host") || !e["host"].is_string()) continue;
            HostHistoryEntry entry;
            entry.host = Utf8ToWide(e["host"].get<std::string>());
            if (e.contains("action") && e["action"].is_string()) {
                entry.action = Utf8ToWide(e["action"].get<std::string>());
            }
            const bool dup = std::any_of(g_hostHistory.begin(), g_hostHistory.end(),
                [&](const HostHistoryEntry& h) { return EqualsIgnoreCase(h.host, entry.host); });
            if (!entry.host.empty() && !dup) {
                g_hostHistory.push_back(std::move(entry));
            }
        }
    } catch (const std::exception&) {
        // keep whatever was read before the error
    }
}

// Refill the Host box's dropdown from g_hostHistory, keeping the typed text
// (CB_RESETCONTENT also clears the edit part of the box).
static void RefreshHostHistoryCombo() {
    if (!g_hHostBox) {
        return;
    }
    const std::wstring text = GetWindowTextString(g_hHostBox);
    SendMessageW(g_hHostBox, CB_RESETCONTENT, 0, 0);
    for (const HostHistoryEntry& e : g_hostHistory) {
        SendMessageW(g_hHostBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(e.host.c_str()));
    }
    SetWindowTextW(g_hHostBox, text.c_str());
}

// A history entry was picked (CBN_SELCHANGE): also select the action it was
// last launched with, if that action still exists.
static void OnHostHistoryPicked() {
    const int sel = static_cast<int>(SendMessageW(g_hHostBox, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(g_hostHistory.size())) {
        return;
    }
    const std::wstring& action = g_hostHistory[static_cast<size_t>(sel)].action;
    if (action.empty() || !g_hComboAction) {
        return;
    }
    // CB_FINDSTRINGEXACT ignores case, like action matching.
    const LRESULT idx = SendMessageW(g_hComboAction, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                     reinterpret_cast<LPARAM>(action.c_str()));
    if (idx != CB_ERR) {
        SendMessageW(g_hComboAction, CB_SETCURSEL, static_cast<WPARAM>(idx), 0);
    }
}

// Writes to a temporary file first, then swaps it in, so a crash or power
// loss mid-write never leaves a truncated settings.json behind. Saves the
// window placement (if the window has been shown) and the Host box history.
static void SaveSettings(HWND hWnd) {
    json j;

    if (!IsRectEmpty(&g_normalRect)) {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(wp);
        bool maximized = IsZoomed(hWnd) != FALSE;
        if (GetWindowPlacement(hWnd, &wp)) {
            maximized = wp.showCmd == SW_SHOWMAXIMIZED ||
                        (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED));
        }

        const UINT dpi = g_normalRectDpi ? g_normalRectDpi : Dpi::kBase;
        j["window"] = {
            { "x",         g_normalRect.left },
            { "y",         g_normalRect.top },
            { "width",     MulDiv(g_normalRect.right - g_normalRect.left, Dpi::kBase, dpi) },
            { "height",    MulDiv(g_normalRect.bottom - g_normalRect.top, Dpi::kBase, dpi) },
            { "maximized", maximized },
        };
    }

    std::string text;
    try {
        json history = json::array();
        for (const HostHistoryEntry& e : g_hostHistory) {
            history.push_back({ { "host", WideToUtf8(e.host) }, { "action", WideToUtf8(e.action) } });
        }
        j["hostHistory"] = std::move(history);
        j["treeWidth"] = g_treeW96;                         // F7
        if (g_selGroup) j["group"] = WideToUtf8(*g_selGroup);
        text = j.dump(4) + "\n";
    } catch (const std::exception&) {
        return;  // best effort: never bother the user about settings
    }

    if (!DirectoryExists(GetDataDir())) {
        return;
    }
    const std::wstring path = GetSettingsJsonPath();
    const std::wstring tmp  = path + L".tmp";
    HANDLE hFile = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;  // best effort: never bother the user about window placement
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(hFile, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(hFile);
    if (ok && written == text.size()) {
        MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } else {
        DeleteFileW(tmp.c_str());
    }
}

// Put `host` at the top of the history (removing any older entry for the
// same host, ignoring case), trim to kMaxHostHistory, refresh the dropdown
// and save at once, so the history survives even if the app is killed.
static void AddHostHistory(HWND hWnd, const std::wstring& host, const std::wstring& action) {
    g_hostHistory.erase(
        std::remove_if(g_hostHistory.begin(), g_hostHistory.end(),
                       [&](const HostHistoryEntry& e) { return EqualsIgnoreCase(e.host, host); }),
        g_hostHistory.end());
    g_hostHistory.insert(g_hostHistory.begin(), HostHistoryEntry{ host, action });
    if (g_hostHistory.size() > kMaxHostHistory) {
        g_hostHistory.resize(kMaxHostHistory);
    }
    RefreshHostHistoryCombo();
    SaveSettings(hWnd);
}

// Shift+Delete while the Host box's list is open removes the highlighted
// entry (the same gesture browsers use for their address-bar history).
// Called from the message loop; returns true if it handled the key.
static bool HandleHostHistoryDelete(HWND hWnd, const MSG& msg) {
    if (msg.message != WM_KEYDOWN || msg.wParam != VK_DELETE || !(GetKeyState(VK_SHIFT) & 0x8000) ||
        !g_hHostBox || (msg.hwnd != g_hHostBox && !IsChild(g_hHostBox, msg.hwnd)) ||
        !SendMessageW(g_hHostBox, CB_GETDROPPEDSTATE, 0, 0)) {
        return false;
    }
    const int sel = static_cast<int>(SendMessageW(g_hHostBox, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(g_hostHistory.size())) {
        return false;
    }
    const std::wstring text = GetWindowTextString(g_hHostBox);
    g_hostHistory.erase(g_hostHistory.begin() + sel);
    SendMessageW(g_hHostBox, CB_DELETESTRING, static_cast<WPARAM>(sel), 0);  // list stays open
    SendMessageW(g_hHostBox, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SetWindowTextW(g_hHostBox, text.c_str());
    SaveSettings(hWnd);
    return true;
}

// Remember the window rectangle whenever it is in the restored state, so the
// saved size is the "normal" one even if the app is closed maximized.
static void TrackNormalRect(HWND hWnd) {
    if (!IsWindowVisible(hWnd) || IsIconic(hWnd) || IsZoomed(hWnd) || g_placingWindow) {
        return;
    }
    RECT rc;
    if (GetWindowRect(hWnd, &rc)) {
        g_normalRect    = rc;
        g_normalRectDpi = g_dpi;
    }
}

// Width of the widest button caption in the current font, plus padding, but
// never less than BTN_MIN_W. Stops long captions (e.g. "Launch RDP
// connection for host not in this list") being clipped at larger fonts.
static int MeasureButtonWidth(HWND hWnd) {
    int widest = 0;
    HDC hdc = GetDC(hWnd);
    if (hdc) {
        HGDIOBJ old = SelectObject(hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
        const HWND buttons[] = { g_hBtnRDP, g_hBtnEditActions, g_hBtnEditConn };
        for (HWND b : buttons) {
            if (!b) continue;
            wchar_t text[256] = {};
            const int len = GetWindowTextW(b, text, 256);
            SIZE sz = {};
            if (GetTextExtentPoint32W(hdc, text, len, &sz) && sz.cx > widest) {
                widest = sz.cx;
            }
        }
        SelectObject(hdc, old);
        ReleaseDC(hWnd, hdc);
    }
    return (std::max)(S(Layout::BTN_MIN_W), widest + S(Layout::BTN_PAD_X));
}

// Width of `text` in the current UI font, in pixels.
static int MeasureTextWidth(HWND hWnd, const wchar_t* text) {
    int width = 0;
    HDC hdc = GetDC(hWnd);
    if (hdc) {
        HGDIOBJ old = SelectObject(hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
        SIZE sz = {};
        if (GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &sz)) {
            width = sz.cx;
        }
        SelectObject(hdc, old);
        ReleaseDC(hWnd, hdc);
    }
    return width;
}

// Height of everything below the list: the quick-launch row (F1), the two
// button rows and the About link row (F6), with gaps between them and a
// margin above and below.
static int ButtonAreaHeight() {
    return S(Layout::BTN_H) * 3 + S(Layout::ROW_GAP) * 2 + S(Layout::LINK_H) + S(Layout::MARGIN) * 2;
}

// Apply the current DPI: (re)create the font, set it on every control and
// re-measure the buttons. Called on WM_CREATE and WM_DPICHANGED.
static void ApplyDpi(HWND hWnd) {
    HFONT newFont = Dpi::CreateUiFont(g_dpi);
    const HWND controls[] = { g_hGroupTree, g_hListView, g_hLblHost, g_hHostBox, g_hLblAction,
                              g_hComboAction, g_hBtnGo, g_hBtnRDP, g_hBtnEditActions,
                              g_hBtnEditConn, g_hBtnRun, g_hBtnExit, g_hLinkAbout };
    for (HWND c : controls) {
        if (c) {
            SendMessageW(c, WM_SETFONT,
                         reinterpret_cast<WPARAM>(newFont ? newFont : GetStockObject(DEFAULT_GUI_FONT)),
                         TRUE);
        }
    }
    if (g_hFont) {
        DeleteObject(g_hFont);  // only after no control uses it any more
    }
    g_hFont = newFont;
    g_btnW  = MeasureButtonWidth(hWnd);
    // Label captions without the '&' mnemonic marker.
    g_lblHostW   = MeasureTextWidth(hWnd, L"Host:");
    g_lblActionW = MeasureTextWidth(hWnd, L"Action:");
}

// Single-line text label, vertically centered in its rectangle. A '&' in
// the text underlines the next letter; Alt+that letter moves focus to the
// matching control (handled by HandleQuickLaunchMnemonic in the message loop).
static HWND CreateLabel(HWND hWnd, const wchar_t* text, int id, DWORD align) {
    return CreateWindowExW(
        0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | align,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

static HWND CreateButton(HWND hWnd, const wchar_t* text, int id) {
    return CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

// Launch the selected list entry (double-click, or Enter on the list).
// The device is looked up by the ID in the row's lParam (B14), so this stays
// correct even if list order and g_devices order ever differ.
static void ActivateSelection(HWND hWnd) {
    const int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel < 0) return;
    const int index = FindDeviceIndexById(GetRowDeviceId(g_hListView, sel));
    if (index >= 0) OnItemActivated(hWnd, index, g_actions);
}

// Switch every DPI-dependent part of the UI to newDpi: font, button width and
// column widths. Does not resize the window itself.
static void RescaleForDpi(HWND hWnd, UINT newDpi) {
    if (newDpi == 0 || newDpi == g_dpi) {
        return;
    }
    const UINT oldDpi = g_dpi;
    g_dpi = newDpi;

    ApplyDpi(hWnd);
    for (int col = 0; col < 3; ++col) {
        const int w = ListView_GetColumnWidth(g_hListView, col);
        ListView_SetColumnWidth(g_hListView, col,
            MulDiv(w, static_cast<int>(g_dpi), static_cast<int>(oldDpi)));
    }
}

// ---------------------------------------------------------------------------
// About link and About box (F6)
// ---------------------------------------------------------------------------

// A SysLink control showing "About" as a link. SysLink needs Common Controls
// v6 (the manifest); if it can't be created, a clickable label (SS_NOTIFY,
// reported as STN_CLICKED) is used instead so the About box is still reachable.
static HWND CreateAboutLink(HWND hWnd, int id) {
    HWND h = CreateWindowExW(0, WC_LINK, L"<a>About</a>", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             GetModuleHandleW(nullptr), nullptr);
    if (!h) {
        h = CreateWindowExW(0, L"STATIC", L"About", WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                            0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            GetModuleHandleW(nullptr), nullptr);
    }
    return h;
}

// Version, author and release date, plus whether the editor is installed.
static void ShowAboutBox(HWND hWnd) {
    const bool editorFound = FileExists(GetExeDir() + L"\\DDLaunchEdit.exe");
    wchar_t jsonVer[32];
    swprintf_s(jsonVer, L"%d.%d.%d", NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR,
               NLOHMANN_JSON_VERSION_PATCH);

    const std::wstring text =
        std::wstring(L"DDLaunch+ ") + VERSION + L"\n"
        L"Data-Driven Launcher\n\n"
        L"Author:  " APP_AUTHOR L"\n"
        L"Release date:  " APP_RELEASE_DATE L"\n"
        L"Version:  " + std::wstring(VERSION).substr(1) + L"\n\n"
        L"Editor:  " + (editorFound ? L"DDLaunchEdit.exe (Edit buttons open it)"
                                    : L"not installed (Edit buttons open Notepad)") + L"\n\n"
        L"Win32 C++ \u00B7 nlohmann/json " + jsonVer;  // B29: middle dot

    MSGBOXPARAMSW mbp = {};
    mbp.cbSize = sizeof(mbp);
    mbp.hwndOwner = hWnd;
    mbp.hInstance = GetModuleHandleW(nullptr);
    mbp.lpszText = text.c_str();
    mbp.lpszCaption = L"About DDLaunch+";
    mbp.dwStyle = MB_OK | MB_USERICON;
    mbp.lpszIcon = MAKEINTRESOURCEW(IDI_ICON1);  // the application icon
    MessageBoxIndirectW(&mbp);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_dpi = Dpi::ForWindow(hWnd);

        // F7: group tree, left of the list. Created first so it is first in
        // the Tab order; hidden until connections.json turns out to use groups.
        g_hGroupTree = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_TABSTOP | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 0, 0,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_GROUP_TREE)), GetModuleHandleW(nullptr), nullptr);

        // Create ListView (WS_TABSTOP so Tab can reach it)
        g_hListView = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LISTVIEW)), GetModuleHandleW(nullptr), nullptr);

        ListView_SetExtendedListViewStyle(g_hListView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // Add columns (widths scaled for the current DPI)
        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
        lvc.fmt  = LVCFMT_LEFT;

        lvc.iSubItem = 0; lvc.cx = S(Layout::COL_W[0]); lvc.pszText = (LPWSTR)L"Resource (Host, Drive, or Shell Item)";
        ListView_InsertColumn(g_hListView, 0, &lvc);

        lvc.iSubItem = 1; lvc.cx = S(Layout::COL_W[1]); lvc.pszText = (LPWSTR)L"Type";
        lvc.fmt = LVCFMT_CENTER;
        ListView_InsertColumn(g_hListView, 1, &lvc);

        lvc.iSubItem = 2; lvc.cx = S(Layout::COL_W[2]); lvc.pszText = (LPWSTR)L"Description";
        lvc.fmt = LVCFMT_LEFT;
        ListView_InsertColumn(g_hListView, 2, &lvc);

        // Controls are created in on-screen reading order, which is also the
        // Tab order: quick-launch row, then the two button rows, each
        // left-to-right. Each label is created just before its control so
        // its Alt+letter mnemonic focuses that control.
        //   Host: [ host box   ] Action: [ dropdown ] [ Go ]
        //   [ Edit Actions     ]         [ Launch RDP ... ]
        //   [ Edit Connections ]         [ Run... ] [ Exit ]
        //   About
        g_hLblHost  =CreateLabel(hWnd, L"&Host:", IDC_LBL_HOST, SS_LEFT);
        // F4: an editable dropdown (CBS_DROPDOWN) -- type a host, or open
        // the list for the history of hosts launched with Go.
        g_hHostBox = CreateWindowExW(
            0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
            0, 0, 0, 0,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HOST_BOX)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_hHostBox, CB_LIMITTEXT, 2048, 0);
        SendMessageW(g_hHostBox, CB_SETCUEBANNER, 0,
                     reinterpret_cast<LPARAM>(L"host name, IP address or URL"));

        g_hLblAction   = CreateLabel(hWnd, L"&Action:", IDC_LBL_ACTION, SS_RIGHT);
        g_hComboAction = CreateWindowExW(
            0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
            0, 0, 0, 0,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_ACTION)),
            GetModuleHandleW(nullptr), nullptr);

        g_hBtnGo          = CreateButton(hWnd, L"&Go", IDC_BTN_GO);
        g_hBtnEditActions = CreateButton(hWnd, L"Edit Actions", IDC_BTN_EDIT_ACTIONS);
        g_hBtnRDP         = CreateButton(hWnd, L"Launch RDP connection for host not in this list", IDC_BTN_RDP);
        g_hBtnEditConn    = CreateButton(hWnd, L"Edit Connections", IDC_BTN_EDIT_CONN);
        g_hBtnRun         = CreateButton(hWnd, L"Run...", IDC_BTN_RUN);
        g_hBtnExit        = CreateButton(hWnd, L"Exit", IDC_BTN_EXIT);
        g_hLinkAbout      = CreateAboutLink(hWnd, IDC_LINK_ABOUT);  // F6 (last in Tab order)

        // DPI-correct font on every control, and button width to fit text
        ApplyDpi(hWnd);

        // Data is not loaded here (B19): the window is still hidden, so an
        // error box would have no visible owner. wWinMain does the first
        // load right after ShowWindow.

        g_hLastFocus = g_hListView;
        return 0;
    }

    case WM_SIZE: {
        TrackNormalRect(hWnd);  // R8

        RECT rc;
        GetClientRect(hWnd, &rc);

        const int btnH   = S(Layout::BTN_H);
        const int btnW   = g_btnW;
        const int gap    = S(Layout::GAP);
        const int margin = S(Layout::MARGIN);
        const int rowGap = S(Layout::ROW_GAP);

        // ListView fills everything above the button area -- to the right of
        // the group tree (F7) when that is shown, with a draggable bar between.
        const int listH = (std::max)(0, static_cast<int>(rc.bottom) - ButtonAreaHeight());
        if (g_hGroupTree && IsWindowVisible(g_hGroupTree)) {
            const int splitW = S(Layout::SPLIT_W);
            const int maxW = (std::max)(S(Layout::TREE_MIN_W), static_cast<int>(rc.right) - S(Layout::LIST_MIN_W));
            g_treePx = (std::min)((std::max)(S(g_treeW96), S(Layout::TREE_MIN_W)), maxW);
            MoveWindow(g_hGroupTree, 0, 0, g_treePx, listH, TRUE);
            MoveWindow(g_hListView, g_treePx + splitW, 0, (std::max)(0, static_cast<int>(rc.right) - g_treePx - splitW), listH, TRUE);
        } else {
            g_treePx = 0;
            MoveWindow(g_hListView, 0, 0, rc.right, listH, TRUE);
        }

        const int totalW = btnW * 2 + gap;
        const int startX = (rc.right - totalW) / 2;
        const int rightX = startX + btnW + gap;
        const int linkH  = S(Layout::LINK_H);
        const int row2Y  = rc.bottom - margin - linkH - btnH;
        const int row1Y  = row2Y - rowGap - btnH;
        const int row0Y  = row1Y - rowGap - btnH;
        // Run/Exit (and the dropdown/Go above them) split the right-hand slot
        // so all three rows stay aligned.
        const int halfW  = (btnW - gap) / 2;

        // Quick-launch row (F1), aligned to the button columns:
        //   left slot : "Host:" + host box + "Action:" (label reaches into the gap)
        //   right slot: dropdown (under/over Run...) | Go (over Exit)
        {
            const int labelGap = S(4);
            const int hostX    = startX + g_lblHostW + labelGap;
            const int actLblX  = rightX - labelGap - g_lblActionW;
            const int hostW    = (std::max)(S(40), actLblX - S(8) - hostX);

            MoveWindow(g_hLblHost,   startX,  row0Y, g_lblHostW,   btnH, TRUE);
            MoveWindow(g_hLblAction, actLblX, row0Y, g_lblActionW, btnH, TRUE);

            // For a combo box the height given is that of the open list; the
            // closed box's height comes from the font. Center the closed box
            // on the row. Used for the Host box (F4) and the Action dropdown.
            auto placeCombo = [&](HWND hCombo, int x, int w) {
                MoveWindow(hCombo, x, row0Y, w, S(200), TRUE);
                RECT cb = {};
                GetWindowRect(hCombo, &cb);
                const int comboH = cb.bottom - cb.top;
                SetWindowPos(hCombo, nullptr, x, row0Y + (btnH - comboH) / 2, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            };
            placeCombo(g_hHostBox, hostX, hostW);
            placeCombo(g_hComboAction, rightX, halfW);

            MoveWindow(g_hBtnGo, rightX + halfW + gap, row0Y, halfW, btnH, TRUE);
        }

        // Middle row: Edit Actions | Launch RDP (F2)
        MoveWindow(g_hBtnEditActions, startX, row1Y, btnW, btnH, TRUE);
        MoveWindow(g_hBtnRDP,         rightX, row1Y, btnW, btnH, TRUE);

        // Bottom row: Edit Connections | Run... | Exit
        MoveWindow(g_hBtnEditConn, startX,               row2Y, btnW,  btnH, TRUE);
        MoveWindow(g_hBtnRun,      rightX,               row2Y, halfW, btnH, TRUE);
        MoveWindow(g_hBtnExit,     rightX + halfW + gap, row2Y, halfW, btnH, TRUE);

        // About link (F6): under the bottom row, lined up with its left edge.
        MoveWindow(g_hLinkAbout, startX, row2Y + btnH + S(3),
                   MeasureTextWidth(hWnd, L"About") + S(6), linkH - S(3), TRUE);

        return 0;
    }

    case WM_GETMINMAXINFO: {
        // Keep the window large enough that the buttons never overlap and
        // some of the list stays visible (also fixes R4).
        if (g_hBtnRDP) {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const int clientW = g_btnW * 2 + S(Layout::GAP) + S(Layout::MARGIN) * 2;
            const int clientH = ButtonAreaHeight() + S(Layout::MIN_LIST_H);
            const SIZE minSize = Dpi::WindowSizeForClient(hWnd, clientW, clientH, g_dpi);
            mmi->ptMinTrackSize.x = minSize.cx;
            mmi->ptMinTrackSize.y = minSize.cy;
        }
        return 0;
    }

    case WM_MOVE:
        TrackNormalRect(hWnd);  // R8
        return 0;

    // --- F7: the bar between the group tree and the list --------------------
    // It is the strip of the main window left uncovered between the two
    // controls; drag it to change the tree width.
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wParam) == hWnd && LOWORD(lParam) == HTCLIENT && g_treePx > 0) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            RECT rc;
            GetClientRect(hWnd, &rc);
            if (g_splitDrag || (pt.x >= g_treePx && pt.x < g_treePx + S(Layout::SPLIT_W) &&
                                pt.y < rc.bottom - ButtonAreaHeight())) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN: {
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        RECT rc;
        GetClientRect(hWnd, &rc);
        // B26: only the bar beside the list, not the strip below it by the
        // buttons (the same area WM_SETCURSOR shows the resize cursor for).
        if (g_treePx > 0 && x >= g_treePx && x < g_treePx + S(Layout::SPLIT_W) &&
            y < rc.bottom - ButtonAreaHeight()) {
            g_splitDrag = true;
            SetCapture(hWnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_splitDrag) {
            const int x = static_cast<short>(LOWORD(lParam)) - S(Layout::SPLIT_W) / 2;
            g_treeW96 = MulDiv((std::max)(x, S(Layout::TREE_MIN_W)), static_cast<int>(Dpi::kBase),
                               static_cast<int>(g_dpi));
            SendMessageW(hWnd, WM_SIZE, 0, 0);  // re-layout (WM_SIZE clamps the width)
            g_treeW96 = MulDiv(g_treePx, static_cast<int>(Dpi::kBase), static_cast<int>(g_dpi));
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_splitDrag) ReleaseCapture();  // WM_CAPTURECHANGED ends the drag
        return 0;

    case WM_CAPTURECHANGED:
        g_splitDrag = false;
        return 0;

    case WM_DPICHANGED: {
        // Window moved to a monitor with a different DPI (or the user changed
        // scaling). Rescale fonts, columns and buttons, then take the size
        // Windows suggests -- except while wWinMain is placing the window,
        // where the requested rectangle is already sized for the target
        // monitor and must not be scaled a second time.
        RescaleForDpi(hWnd, LOWORD(wParam));

        if (!g_placingWindow) {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDOK: {
            // IsDialogMessageW turns Enter into IDOK when the focused control
            // isn't a push button. On the list that means "launch"; in the
            // Host box or the Action dropdown it means "Go" (F1).
            // (In the Host box, focus is on the combo box's own edit child.)
            const HWND focus = GetFocus();
            if (focus == g_hListView) {
                ActivateSelection(hWnd);
            } else if (focus == g_hGroupTree) {
                // F7: Enter in the group tree moves to its entries.
                SetFocus(g_hListView);
                if (ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED) < 0 &&
                    ListView_GetItemCount(g_hListView) > 0) {
                    ListView_SetItemState(g_hListView, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
                }
            } else if (focus == g_hHostBox || IsChild(g_hHostBox, focus) || focus == g_hComboAction) {
                OnGo(hWnd);
            }
            break;
        }
        case IDC_BTN_GO:
            OnGo(hWnd);
            break;
        case IDC_LINK_ABOUT:
            // Fallback label (no Common Controls v6) reports clicks this way.
            if (HIWORD(wParam) == STN_CLICKED) {
                ShowAboutBox(hWnd);
            }
            break;
        case IDC_HOST_BOX:
            // F4: a host picked from the history brings its last action along.
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                OnHostHistoryPicked();
            }
            break;
        case IDCANCEL:
            // Esc: deliberately ignored so it can't close the launcher.
            break;
        case IDC_BTN_RDP:
            ReportLaunchFailure(hWnd, ShellOpen(hWnd, L"mstsc.exe", L""),
                L"Remote Desktop Connection", L"mstsc.exe", L"", L"");
            break;
        case IDC_BTN_RUN:
            ShowRunDialog(hWnd);
            break;
        case IDC_BTN_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDC_BTN_EDIT_CONN:
            OpenJsonFileForEditing(hWnd, GetConnectionsJsonPath(), L"/connections");
            break;
        case IDC_BTN_EDIT_ACTIONS:
            OpenJsonFileForEditing(hWnd, GetActionsJsonPath(), L"/actions");
            break;
        }
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);
        // F6: About link clicked, or Enter pressed while it has focus.
        if (pnmh->idFrom == IDC_LINK_ABOUT && (pnmh->code == NM_CLICK || pnmh->code == NM_RETURN)) {
            ShowAboutBox(hWnd);
            return 0;
        }
        if (pnmh->idFrom == IDC_GROUP_TREE && pnmh->code == TVN_SELCHANGEDW) {  // F7
            OnGroupTreeSelChanged(reinterpret_cast<const NMTREEVIEWW*>(lParam));
            return 0;
        }
        if (pnmh->idFrom == IDC_LISTVIEW) {
            // NM_RETURN only arrives if the list itself handles Enter; with
            // IsDialogMessageW it normally comes through IDOK instead. A key
            // press produces one or the other, never both.
            if (pnmh->code == NM_DBLCLK || pnmh->code == NM_RETURN) {
                ActivateSelection(hWnd);
            } else if (pnmh->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                ToggleSortColumn(pnmlv->iSubItem);
                PopulateListView(g_hListView);
                UpdateHeaderSortArrow(g_hListView, g_sortCol, g_sortAsc);
            }
        }
        return 0;
    }

    case WM_ACTIVATE: {
        if (LOWORD(wParam) == WA_INACTIVE) {
            // Remember which control had focus so it can be restored.
            HWND focus = GetFocus();
            if (focus && IsChild(hWnd, focus)) {
                g_hLastFocus = focus;
            }
        } else if (g_hListView) {
            // Reload connections.json / actionDefinitions.json when the main
            // window regains focus, so edits made in Notepad are picked up
            // with no restart. Only re-reads files whose last-write time or
            // size changed (B1). Skipped while the window is still hidden
            // (activation during the first ShowWindow): wWinMain does the
            // first load once the window is visible (B19).
            if (IsWindowVisible(hWnd)) {
                ReloadAllData(hWnd, false);
            }

            // Returning 0 without DefWindowProc means Windows won't set focus
            // for us; put it back on the control that last had it.
            SetFocus(g_hLastFocus && IsWindow(g_hLastFocus) ? g_hLastFocus : g_hListView);
        }
        return 0;
    }

    case WM_SETFOCUS:
        // E.g. restoring from minimized: forward focus to a child control.
        if (g_hListView) {
            SetFocus(g_hLastFocus && IsWindow(g_hLastFocus) ? g_hLastFocus : g_hListView);
        }
        return 0;

    case WM_ENDSESSION:
        // Windows is logging off or shutting down; WM_DESTROY may never
        // arrive, so save the window placement now (R8).
        if (wParam) {
            SaveSettings(hWnd);
        }
        return 0;

    case WM_DESTROY:
        SaveSettings(hWnd);  // R8
        if (g_hFont) {
            DeleteObject(g_hFont);
            g_hFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Build self-check (R11): is Common Controls v6 active?
//
// Without the manifest the process silently gets comctl32 v5.8: no visual
// styles and no header sort arrows. That is a build problem (DDLaunch.rc or
// DDLaunch.manifest missing from the build, or the manifest failed to embed),
// so report it where the developer will see it: the debugger output always,
// plus a message box in debug builds. Release builds keep working quietly.
// ---------------------------------------------------------------------------
static void CheckCommonControlsVersion() {
    DLLVERSIONINFO dvi = {};
    dvi.cbSize = sizeof(dvi);

    using PFN_DllGetVersion = HRESULT (CALLBACK*)(DLLVERSIONINFO*);
    HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
    auto getVersion = comctl ? reinterpret_cast<PFN_DllGetVersion>(reinterpret_cast<void*>(
                                   GetProcAddress(comctl, "DllGetVersion")))
                             : nullptr;
    if (!getVersion || FAILED(getVersion(&dvi)) || dvi.dwMajorVersion >= 6) {
        return;
    }

    wchar_t msg[256];
    swprintf_s(msg,
        L"DDLaunch+: Common Controls %lu.%lu is loaded instead of 6.0. "
        L"DDLaunch.manifest is not embedded -- build DDLaunch.rc and link DDLaunch.res.\n",
        dvi.dwMajorVersion, dvi.dwMinorVersion);
    OutputDebugStringW(msg);
#ifndef NDEBUG
    MessageBoxW(nullptr, msg, L"DDLaunch+ build check", MB_ICONWARNING | MB_OK);
#endif
}

// ---------------------------------------------------------------------------
// Single instance (R12)
//
// A named mutex in the Local\ namespace is unique per logon session, so each
// user (including separate Remote Desktop sessions) gets their own
// DDLaunch+, but one user can't start two. The second copy finds the first
// one's window by class name, restores it if minimized and brings it to the
// front. It is allowed to do so because the user just started it, which
// makes it the foreground process.
// ---------------------------------------------------------------------------
static const wchar_t kWindowClass[]  = L"DDLaunchClass";
static const wchar_t kInstanceMutex[] = L"Local\\DDLaunchPlus.SingleInstance.7C1E4B2A";

// Returns true if this is the first instance (carry on starting up), false
// if another instance is running and has been activated (exit now).
static bool AcquireSingleInstance() {
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (!hMutex || GetLastError() != ERROR_ALREADY_EXISTS) {
        return true;  // first instance (or mutex unavailable: don't block startup)
    }
    CloseHandle(hMutex);

    // The first instance may still be creating its window; wait up to ~2 s.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (HWND hExisting = FindWindowW(kWindowClass, nullptr)) {
            if (IsIconic(hExisting)) {
                ShowWindow(hExisting, SW_RESTORE);
            }
            SetForegroundWindow(hExisting);
            return false;
        }
        Sleep(100);
    }
    return false;  // already running but no window found: still don't start twice
}

// ---------------------------------------------------------------------------
// Alt+H / Alt+A / Alt+G for the quick-launch row (F1)
//
// Handled explicitly in the message loop instead of relying on the
// mnemonic support in IsDialogMessageW, which is not dependable for a
// window that isn't a real dialog. Returns true if the key was used (the
// message is then swallowed, so there is no "no menu" beep).
// ---------------------------------------------------------------------------
static bool HandleQuickLaunchMnemonic(HWND hWnd, const MSG& msg) {
    if (msg.message != WM_SYSCHAR || (msg.hwnd != hWnd && !IsChild(hWnd, msg.hwnd))) {
        return false;
    }
    switch (towlower(static_cast<wint_t>(msg.wParam))) {
    case L'h':                                     // Host box, text selected
        SetFocus(g_hHostBox);
        SendMessageW(g_hHostBox, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
        return true;
    case L'a':                                     // Action dropdown
        SetFocus(g_hComboAction);
        return true;
    case L'g':                                     // Go
        OnGo(hWnd);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // R12: single instance. If DDLaunch+ is already running in this logon
    // session, bring its window forward and exit instead of opening a second
    // copy (which would also race the first one when saving settings.json).
    // The handle is intentionally never closed: Windows releases the mutex
    // when this process exits.
    if (!AcquireSingleInstance()) {
        return 0;
    }

    // B15: single-threaded apartment COM for the UI thread, initialized once.
    // ShellExecuteEx needs it for shell extensions and some URL handlers, and
    // the Run dialog (Shell.Application) uses it. Uninitialized after the
    // message loop ends.
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Must run before any window is created.
    Dpi::EnableDpiAwareness();

    // Common Controls v6 (visual styles) -- requires DDLaunch.manifest to be
    // embedded via DDLaunch.rc.
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);
    CheckCommonControlsVersion();

    // Register window class. Icons are loaded at the exact sizes Windows
    // wants (large and small) so they aren't blurred by scaling.
    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);  // dialog-style background behind the buttons
    wc.lpszClassName  = kWindowClass;
    wc.hIcon   = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
                     GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    std::wstring windowTitle = std::wstring(L"DDLaunch+ ") + VERSION + L" - double-click a row or press Enter to launch";

    // WS_EX_CONTROLPARENT lets the dialog manager (IsDialogMessageW) treat
    // this window like a dialog for Tab navigation.
    HWND hWnd = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        kWindowClass,
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,  // R17: children aren't painted over
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        Layout::WINDOW_W,
        Layout::WINDOW_H,
        nullptr,
        nullptr,
        hInst,
        nullptr);

    if (!hWnd) {
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 1;
    }

    // Work out where the window should open (R8, R10):
    //  - saved placement: saved top-left, saved size scaled for the DPI of
    //    the monitor it will be on;
    //  - otherwise: where Windows put it (CW_USEDEFAULT), at the default
    //    size scaled for that monitor.
    // Either way the rectangle is then clamped to that monitor's work area.
    const json settings = LoadSettingsFile();  // R18: read once
    const std::optional<WindowSettings> saved = LoadWindowSettings(settings);
    RECT target;
    HMONITOR hMon;
    if (saved) {
        RECT probe = { saved->x, saved->y, saved->x + saved->width96, saved->y + saved->height96 };
        hMon = MonitorFromRect(&probe, MONITOR_DEFAULTTONEAREST);
        const UINT monDpi = Dpi::ForMonitor(hMon);
        target = { saved->x, saved->y,
                   saved->x + Dpi::Scale(saved->width96, monDpi),
                   saved->y + Dpi::Scale(saved->height96, monDpi) };
    } else {
        RECT cur;
        GetWindowRect(hWnd, &cur);
        hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        const UINT monDpi = Dpi::ForMonitor(hMon);
        target = { cur.left, cur.top,
                   cur.left + Dpi::Scale(Layout::WINDOW_W, monDpi),
                   cur.top  + Dpi::Scale(Layout::WINDOW_H, monDpi) };
    }
    ClampToWorkArea(target, hMon);

    // Moving the (still hidden) window may cross into a monitor with a
    // different DPI; g_placingWindow stops WM_DPICHANGED from rescaling the
    // already-correct rectangle. Afterwards make sure the UI matches the
    // DPI of wherever the window ended up.
    g_placingWindow = true;
    SetWindowPos(hWnd, nullptr, target.left, target.top,
                 target.right - target.left, target.bottom - target.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    g_placingWindow = false;
    RescaleForDpi(hWnd, Dpi::ForWindow(hWnd));
    SendMessageW(hWnd, WM_SIZE, 0, 0);  // re-layout controls for the final size

    // Seed the "normal" rect now: if the window opens maximized and is closed
    // without ever being restored, this is still the size to save.
    GetWindowRect(hWnd, &g_normalRect);

    // F4: Host box history from settings.json.
    LoadHostHistory(settings);
    RefreshHostHistoryCombo();
    // F7: group tree width and the group that was selected last time.
    LoadTreeSettings(settings);
    g_normalRectDpi = g_dpi;

    // Reopen maximized if it was closed maximized, unless the shortcut asked
    // for something specific (e.g. "Run: Minimized").
    int showCmd = nCmdShow;
    if (saved && saved->maximized &&
        (nCmdShow == SW_SHOWNORMAL || nCmdShow == SW_SHOWDEFAULT || nCmdShow == SW_SHOW)) {
        showCmd = SW_SHOWMAXIMIZED;
    }
    ShowWindow(hWnd, showCmd);
    TrackNormalRect(hWnd);  // record the starting rect even if never moved
    UpdateWindow(hWnd);

    // B19: first load of connections.json + actionDefinitions.json, now that
    // the window is visible (WM_ACTIVATE skips reloading while it is hidden).
    // Not forced: g_dataLoaded is still false, so this loads both files; if
    // activation already loaded them after the window became visible, the
    // stamps are unchanged and this returns at once -- either way the files
    // are read only once at startup. Also covers a window that opened without
    // activation (e.g. a shortcut set to "Run: Minimized").
    ReloadAllData(hWnd, false);

    // IsDialogMessageW gives the main window dialog-style keyboard handling:
    // Tab/Shift+Tab, arrow keys between buttons, Enter/Space on buttons.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (HandleQuickLaunchMnemonic(hWnd, msg)) {  // F1: Alt+H / Alt+A / Alt+G
            continue;
        }
        if (HandleHostHistoryDelete(hWnd, msg)) {    // F4: Shift+Delete in the history list
            continue;
        }
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
    return (int)msg.wParam;
}
