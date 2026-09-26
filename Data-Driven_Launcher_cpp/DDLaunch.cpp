// Data-Driven Launcher+ version 2.3.0
// C++ (Win32 API)
// Author: Glenn Madine
// Release_Date: 09/26/2026
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
//               %USERPROFILE%\DDLaunch\settings.json (window placement,
//               written on exit)
//               (folder and files are created automatically if missing)
// Build files: DDLaunch.cpp, DDLaunch.rc, DDLaunch.manifest, resource.h,
//              DDLaunch.ico, json.hpp
// Compile and link command lines:
//     rc /nologo DDLaunch.rc
//     CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunch.cpp DDLaunch.res /Fe:DDLaunch.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib
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
#define VERSION         		L"v2.3.0"
#define IDC_LISTVIEW        	1001
#define IDC_BTN_RDP         	1002
#define IDC_BTN_EXIT        	1003
#define IDC_BTN_EDIT_CONN   	1004
#define IDC_BTN_EDIT_ACTIONS	1005
#define IDC_BTN_RUN         	1006
#define WM_LAUNCH				(WM_USER + 1)

// Column indices
#define COL_HOST		0
#define COL_TYPE		1
#define COL_DESCRIPTION	2

using json = nlohmann::json;

// --- UTF-8 (std::string, what's in the JSON file) <-> UTF-16 -------------
// (std::wstring, what Windows wide APIs use) conversion helpers.

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    int required = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                        nullptr, 0);
    if (required <= 0) {
        throw std::runtime_error("UTF-8 to UTF-16 conversion failed");
    }
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wide[0], required);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    int required = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        throw std::runtime_error("UTF-16 to UTF-8 conversion failed");
    }
    std::string utf8(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &utf8[0], required,
                         nullptr, nullptr);
    return utf8;
}

// Text of a std::exception for a wide message box (B10). Exception messages
// built by this file and by nlohmann::json are UTF-8 (they can contain file
// paths from the user's profile), so they must not go to MessageBoxA, which
// would read them in the ANSI code page. Never throws.
std::wstring ExceptionText(const std::exception& e) {
    try {
        return Utf8ToWide(e.what());
    } catch (...) {
        return L"(error text could not be converted)";
    }
}

// --- 1. The structure that mirrors each entry in the JSON array -----------
struct Action {
    std::wstring action;	// e.g. L"RDP"
    std::wstring args;		// e.g. L" /v:" note the space before /v: 
    std::wstring command;	// e.g. L"C:\\Windows\\System32\\MSTSC.exe"
};

// The JSON layer still deals in UTF-8 std::string (that's what's on disk
// and what nlohmann::json's default json type holds) -- this is where the
// UTF-8 -> UTF-16/wstring conversion happens.
void from_json(const json& j, Action& a) {
    std::string actionUtf8, argsUtf8, commandUtf8;
    j.at("action").get_to(actionUtf8);  
    j.at("args").get_to(argsUtf8); 
    j.at("command").get_to(commandUtf8);
    a.action = Utf8ToWide(actionUtf8);
    a.args = Utf8ToWide(argsUtf8);    
    a.command = Utf8ToWide(commandUtf8);
}

// (No to_json(Action): the app only reads actionDefinitions.json, it never
// writes it.)

// --- 2. Read the raw file bytes via Win32 (CreateFileW/ReadFile) ----------
// so the path itself can be a std::wstring without depending on the
// MSVC-only std::ifstream(wchar_t*) extension.
std::string ReadFileUtf8(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Could not open file: " + WideToUtf8(filePath));
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        throw std::runtime_error("Could not get file size: " + WideToUtf8(filePath));
    }

    std::string buffer(static_cast<size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, buffer.empty() ? nullptr : &buffer[0],
                        static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
    CloseHandle(hFile);

    if (!ok || bytesRead != buffer.size()) {
        throw std::runtime_error("Failed reading file: " + WideToUtf8(filePath));
    }
    return buffer;
}

// --- 3. Function that opens the file and returns the parsed array ---------
std::vector<Action> loadActions(const std::wstring& filePath) {
    std::string utf8Contents = ReadFileUtf8(filePath);

    json j = json::parse(utf8Contents);
    if (!j.is_array()) {
        throw std::runtime_error("Expected top-level JSON array in " + WideToUtf8(filePath));
    }

    return j.get<std::vector<Action>>();
}

// --- 4. Search / lookup: find the action entry for a given name -----------
// Returns the first action whose name matches actionName, ignoring case
// (B5), or nullptr if there is none. CompareStringOrdinal with
// bIgnoreCase = TRUE compares code points using the operating system's
// invariant upper-case table -- the same rule the file system uses -- so the
// result does not depend on the user's locale (unlike _wcsicmp / towupper).
const Action* findAction(const std::vector<Action>& actions, const std::wstring& actionName) {
    auto it = std::find_if(actions.begin(), actions.end(), [&actionName](const Action& a) {
        return CompareStringOrdinal(a.action.c_str(), static_cast<int>(a.action.size()),
                                    actionName.c_str(), static_cast<int>(actionName.size()),
                                    TRUE) == CSTR_EQUAL;
    });
    return it == actions.end() ? nullptr : &*it;
}

// ---------------------------------------------------------------------------
// Device record
// ---------------------------------------------------------------------------
struct Device {
    std::wstring host;
    std::wstring type;
    std::wstring description;
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
// Get the directory of the running executable
// ---------------------------------------------------------------------------
static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return std::wstring(buf);
}

// ---------------------------------------------------------------------------
// Load connections.json
// ---------------------------------------------------------------------------
// hOwner parents the error boxes so they are modal to the main window.
// Returns std::nullopt if the file can't be read or isn't valid JSON, so the
// caller can keep the list it already has (B8).
static std::optional<std::vector<Device>> LoadConnections(HWND hOwner, const std::wstring& jsonPath) {
    std::vector<Device> devices;

    // Read via Win32 (ReadFileUtf8) instead of std::ifstream so the path can
    // contain non-ASCII characters -- important now that the file lives under
    // %USERPROFILE%, which frequently includes the user's real name.
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

    try {
        nlohmann::json data = nlohmann::json::parse(contents);

        auto parseDevice = [&](const nlohmann::json& obj) {
            if (!obj.is_object()) return;
            Device d;
            if (obj.contains("host")    && obj["host"].is_string())
                d.host    = Utf8ToWide(obj["host"].get<std::string>());
            if (obj.contains("type")    && obj["type"].is_string())
                d.type    = Utf8ToWide(obj["type"].get<std::string>());
            if (obj.contains("description") && obj["description"].is_string())
                d.description = Utf8ToWide(obj["description"].get<std::string>());
            devices.push_back(d);
        };

        if (data.is_object()) {
            for (auto& [key, val] : data.items()) {
                if (val.is_object())      parseDevice(val);
                else if (val.is_array())  for (auto& item : val) parseDevice(item);
            }
        } else if (data.is_array()) {
            for (auto& item : data) parseDevice(item);
        }

    } catch (const std::exception& e) {
        MessageBoxW(hOwner,
            (L"connections.json is not valid JSON:\n\n" + ExceptionText(e) +
             L"\n\nThe previously loaded list is kept until the file is fixed.").c_str(),
            L"DDLaunch+ JSON Error", MB_ICONERROR | MB_OK);
        return std::nullopt;
    }

    return devices;
}

// ---------------------------------------------------------------------------
// Config file paths
//
// Both configs live in a per-user data folder: %USERPROFILE%\DDLaunch
// (e.g. C:\Users\glenn\DDLaunch). The folder and both files are created on
// demand by EnsureDataFiles() if they don't exist.
// ---------------------------------------------------------------------------

// Resolve %USERPROFILE%. Falls back to the shell's Profile known folder if the
// environment variable is missing (e.g. launched from a stripped environment).
// Returns L"" if neither can be determined.
static std::wstring GetUserProfileDir() {
    DWORD needed = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (needed > 0) {
        std::wstring buf(needed, L'\0');
        DWORD len = GetEnvironmentVariableW(L"USERPROFILE", &buf[0], needed);
        if (len > 0 && len < needed) {
            buf.resize(len);
            return buf;
        }
    }

    PWSTR knownPath = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &knownPath)) && knownPath) {
        result = knownPath;
    }
    CoTaskMemFree(knownPath);
    return result;
}

// %USERPROFILE%\DDLaunch (no trailing backslash). If the profile directory
// can't be resolved at all, fall back to the folder containing the .exe so
// the app still has somewhere to keep its configs.
static std::wstring GetDataDir() {
    std::wstring profile = GetUserProfileDir();
    if (profile.empty()) {
        return GetExeDir();
    }
    if (profile.back() == L'\\' || profile.back() == L'/') {
        profile.pop_back();
    }
    return profile + L"\\DDLaunch";
}

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
// "action" is matched against the connection's "type" upper-cased.
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
// File / folder helpers
// ---------------------------------------------------------------------------
static bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirectoryExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

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
// Open a config file for editing in Notepad.
//
// Launches Notepad directly (rather than ShellExecute's "open" verb) so this
// always works even if .json has no file association or is associated with
// something other than a text editor. EnsureDataFiles() runs first so the
// folder and file exist (with valid default JSON) even if the user deleted
// them while the app was running.
// ---------------------------------------------------------------------------
static void OpenJsonFileForEditing(HWND hWnd, const std::wstring& path) {
    EnsureDataFiles(hWnd);

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
// Generic URL detection (B4)
//
// Returns true if `text` starts with a URI scheme as defined by RFC 3986:
//
//     scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"
//
// This replaces the old hard-coded list (https/http/ftp/file/mailto/tel),
// which also had an off-by-one on "file://" (compared 8 chars to a 7-char
// literal) and so never matched "file:///...". Any scheme now works --
// https://, file:///, ssh://, vnc://, rdp://, mailto:, tel:, ms-settings:,
// etc. -- as long as Windows has a handler registered for it.
//
// Schemes must be at least two characters long so that a Windows drive
// letter ("C:\Windows\System32\mstsc.exe", "D:foo.exe") is treated as a
// file path, not a URL. Plain names ("ssh.exe") and UNC paths
// ("\\server\share\tool.exe") contain no scheme and are also paths.
// Only ASCII is accepted, per the RFC, so the check is locale-independent.
// ---------------------------------------------------------------------------
static bool IsAsciiAlpha(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

static bool IsAsciiDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

static bool StartsWithUrlScheme(const std::wstring& text) {
    const size_t colon = text.find(L':');
    if (colon == std::wstring::npos || colon < 2) {
        return false;  // no scheme, or a single drive letter like "C:"
    }
    if (!IsAsciiAlpha(text[0])) {
        return false;
    }
    for (size_t i = 1; i < colon; ++i) {
        const wchar_t c = text[i];
        if (!IsAsciiAlpha(c) && !IsAsciiDigit(c) && c != L'+' && c != L'-' && c != L'.') {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Launching with error reporting (B3)
//
// Uses ShellExecuteExW instead of ShellExecuteW so failures come back as a
// Win32 error code. SEE_MASK_FLAG_NO_UI suppresses the shell's own error
// dialogs so DDLaunch+ can show one consistent message that names the host,
// type, command and parameters involved.
// ---------------------------------------------------------------------------

// System error text for a Win32 error code, without the trailing CR/LF.
static std::wstring GetErrorText(DWORD err) {
    LPWSTR buf = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring text = (len && buf) ? std::wstring(buf, len) : std::wstring(L"Unknown error.");
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    return text;
}

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

// ---------------------------------------------------------------------------
// Command-line quoting (B11)
//
// Returns `arg` unchanged if it has no spaces, tabs or quotes (the normal
// case: "dc01.ynext.corp", "admin@pi01"). Otherwise wraps it in quotes using
// the rules CommandLineToArgvW and the Microsoft C runtime use to split a
// command line: backslashes are literal except directly before a quote,
// where they must be doubled, and an embedded quote becomes \". This makes a
// host such as "Lab PC 7" arrive as one argument; with args " /v:" the result
// is  /v:"Lab PC 7", which parses as the single argument /v:Lab PC 7.
// ---------------------------------------------------------------------------
static std::wstring QuoteArgIfNeeded(const std::wstring& arg) {
    if (arg.empty() || arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');  // escape preceding \s and the quote
        } else {
            out.append(backslashes, L'\\');          // \s not before a quote are literal
        }
        out += c;
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');  // \s before the closing quote are doubled
    out += L'"';
    return out;
}

// command is either an executable (run with args + host as parameters) or
// a URL prefix (command + host is opened with its registered handler).
// Returns ERROR_SUCCESS or a Win32 error code; `file`/`params` receive what
// was actually passed to the shell so the caller can report it.
static DWORD LaunchSomething(HWND hOwner, const std::wstring& command, const std::wstring& args,
                             const std::wstring& host, std::wstring& file, std::wstring& params) {
    if (StartsWithUrlScheme(command)) {
        file = command + host;
        params.clear();
    } else {
        file   = command;
        params = args + QuoteArgIfNeeded(host);  // B11
    }
    return ShellOpen(hOwner, file, params);
}

// ---------------------------------------------------------------------------
// Show the standard Windows "Run" dialog (same as Win+R).
//
// Uses the documented Shell.Application automation object
// (IShellDispatch::FileRun) rather than the undocumented shell32 ordinal 61
// (RunFileDlg). __uuidof() is used so no extra GUID library is needed.
// ---------------------------------------------------------------------------
static void ShowRunDialog(HWND hWnd) {
    // COM may already be initialized on this thread; only balance our own init.
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

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

    if (SUCCEEDED(hrInit)) {
        CoUninitialize();
    }
}

// ---------------------------------------------------------------------------
// DPI helpers
//
// The process is Per-Monitor V2 DPI aware (see EnableDpiAwareness), so all
// sizes are in physical pixels and must be scaled from 96-DPI "design" values.
// The newer DPI APIs are loaded dynamically so the .exe still starts on older
// Windows versions, falling back to system-DPI behavior there.
// ---------------------------------------------------------------------------
namespace Dpi {

const UINT kBase = USER_DEFAULT_SCREEN_DPI;  // 96

template <typename Fn>
static Fn LoadUser32(const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), name)));
}

// Call once, before any window is created. DDLaunch.manifest normally sets
// Per-Monitor V2 already (the officially preferred way); this is a fallback
// for builds without the manifest.
static void EnableDpiAwareness() {
    // Already DPI aware (set by the manifest): keep that setting.
    if (IsProcessDPIAware()) {
        return;
    }

    // Windows 10 1703+: Per-Monitor V2 (also scales non-client area,
    // message boxes and common dialogs automatically).
    using PFN_SetCtx = BOOL (WINAPI*)(HANDLE);
    if (auto setCtx = LoadUser32<PFN_SetCtx>("SetProcessDpiAwarenessContext")) {
        if (setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) {  // PER_MONITOR_AWARE_V2
            return;
        }
    }
    // Windows 8.1+: Per-Monitor (v1) via shcore.dll.
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        using PFN_SetAwareness = HRESULT (WINAPI*)(int);
        auto setAwareness = reinterpret_cast<PFN_SetAwareness>(reinterpret_cast<void*>(
            GetProcAddress(shcore, "SetProcessDpiAwareness")));
        const bool ok = setAwareness && SUCCEEDED(setAwareness(2));  // PROCESS_PER_MONITOR_DPI_AWARE
        FreeLibrary(shcore);
        if (ok) return;
    }
    // Vista+: system DPI aware.
    SetProcessDPIAware();
}

static UINT SystemDpi() {
    HDC hdc = GetDC(nullptr);
    UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : kBase;
    if (hdc) ReleaseDC(nullptr, hdc);
    return dpi ? dpi : kBase;
}

// DPI of the monitor the window is on (Windows 10 1607+), else system DPI.
static UINT ForWindow(HWND hWnd) {
    using PFN_GetDpiForWindow = UINT (WINAPI*)(HWND);
    static auto getDpi = LoadUser32<PFN_GetDpiForWindow>("GetDpiForWindow");
    if (getDpi && hWnd) {
        UINT dpi = getDpi(hWnd);
        if (dpi) return dpi;
    }
    return SystemDpi();
}

static int Scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), static_cast<int>(kBase));
}

// The user's message-box font (normally Segoe UI 9pt) at the given DPI.
static HFONT CreateUiFont(UINT dpi) {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);

    using PFN_SPIForDpi = BOOL (WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static auto spiForDpi = LoadUser32<PFN_SPIForDpi>("SystemParametersInfoForDpi");
    if (spiForDpi && spiForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) {
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        // Metrics are reported at system DPI; rescale to the target DPI.
        ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight,
                                            static_cast<int>(dpi), static_cast<int>(SystemDpi()));
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    return nullptr;
}

// Window size needed for a given client size at the given DPI.
static SIZE WindowSizeForClient(HWND hWnd, int clientW, int clientH, UINT dpi) {
    RECT rc = { 0, 0, clientW, clientH };
    const DWORD style   = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_EXSTYLE));

    using PFN_AdjustForDpi = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static auto adjustForDpi = LoadUser32<PFN_AdjustForDpi>("AdjustWindowRectExForDpi");
    if (!(adjustForDpi && adjustForDpi(&rc, style, FALSE, exStyle, dpi))) {
        AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    }
    return SIZE{ rc.right - rc.left, rc.bottom - rc.top };
}

// DPI of a monitor (Windows 8.1+ via shcore.dll), else system DPI.
static UINT ForMonitor(HMONITOR hMon) {
    using PFN_GetDpiForMonitor = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static PFN_GetDpiForMonitor getDpiForMonitor = []() -> PFN_GetDpiForMonitor {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");  // kept loaded for the process lifetime
        return shcore ? reinterpret_cast<PFN_GetDpiForMonitor>(reinterpret_cast<void*>(
                            GetProcAddress(shcore, "GetDpiForMonitor")))
                      : nullptr;
    }();
    UINT dpiX = 0, dpiY = 0;
    if (getDpiForMonitor && hMon &&
        SUCCEEDED(getDpiForMonitor(hMon, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) && dpiX) {
        return dpiX;
    }
    return SystemDpi();
}

}  // namespace Dpi

// ---------------------------------------------------------------------------
// Keep a window rectangle inside its monitor's work area (R10)
//
// Shrinks the rectangle if it is larger than the work area (screen minus
// taskbar and docked toolbars), then shifts it so that it lies completely
// inside. The monitor is the one the rectangle overlaps most, or the nearest
// one if it is on no monitor at all (e.g. a saved position on a monitor that
// has since been disconnected).
// ---------------------------------------------------------------------------
static void ClampToWorkArea(RECT& rc, HMONITOR hMon) {
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!hMon || !GetMonitorInfoW(hMon, &mi)) {
        return;
    }
    const RECT& work = mi.rcWork;
    const LONG workW = work.right - work.left;
    const LONG workH = work.bottom - work.top;

    LONG w = (std::min)(rc.right - rc.left, workW);
    LONG h = (std::min)(rc.bottom - rc.top, workH);
    LONG x = (std::max)(work.left, (std::min)(rc.left, work.right - w));
    LONG y = (std::max)(work.top,  (std::min)(rc.top,  work.bottom - h));

    rc = RECT{ x, y, x + w, y + h };
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
// changing it. Does nothing if no column has been sorted yet. Called after a
// column click and after every reload, so freshly loaded data keeps the order
// the header arrow shows (B2). std::stable_sort keeps rows with equal keys in
// file order, so repeated reloads never shuffle them.
static void ApplyCurrentSort() {
    const int col = g_sortCol;
    if (col < COL_HOST || col > COL_DESCRIPTION) {
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
            int cmp = _wcsicmp(pa->c_str(), pb->c_str());
            return ascending ? (cmp < 0) : (cmp > 0);
        });
}

// Column header click: same column flips direction, a new column starts
// ascending. Then sorts g_devices with the new state.
static void ToggleSortColumn(int col) {
    if (g_sortCol == col) {
        g_sortAsc = !g_sortAsc;  // same column: flip direction
    } else {
        g_sortCol = col;
        g_sortAsc = true;        // new column: always start ascending
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

// Refill the list from g_devices, keeping the selected device selected (B9),
// identified by the ID in its row's lParam (B14). The row keeps keyboard
// focus and is scrolled into view.
static void PopulateListView(HWND hLV) {
    // 1. Remember which device was selected.
    const int oldSel = ListView_GetNextItem(hLV, -1, LVNI_SELECTED);
    const UINT_PTR selId = oldSel >= 0 ? GetRowDeviceId(hLV, oldSel) : 0;

    // 2. Refill without repainting each row.
    SendMessageW(hLV, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hLV);

    int newSel = -1;
    for (int i = 0; i < (int)g_devices.size(); ++i) {
        const Device& d = g_devices[i];

        LVITEMW lvi = {};
        lvi.mask     = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem    = i;
        lvi.iSubItem = 0;
        lvi.pszText  = const_cast<LPWSTR>(d.host.c_str());
        lvi.lParam   = static_cast<LPARAM>(d.id);
        ListView_InsertItem(hLV, &lvi);

        ListView_SetItemText(hLV, i, COL_TYPE,        const_cast<LPWSTR>(d.type.c_str()));
        ListView_SetItemText(hLV, i, COL_DESCRIPTION, const_cast<LPWSTR>(d.description.c_str()));

        if (selId != 0 && d.id == selId) newSel = i;
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
// File change detection (B1)
//
// A FileStamp records a file's last-write time and size. The main window
// reloads a JSON file only when its stamp differs from the one recorded at the
// last load. The stamp is recorded BEFORE the file is parsed, so an invalid
// file reports its error once per save. Before this change every WM_ACTIVATE
// reloaded, so closing the error box re-activated the window, which reloaded,
// failed and showed the box again in an endless loop.
// ---------------------------------------------------------------------------
struct FileStamp {
    bool     exists = false;
    FILETIME lastWrite{};
    ULONGLONG size = 0;

    bool operator==(const FileStamp& o) const {
        if (exists != o.exists) return false;
        if (!exists) return true;  // both missing
        return size == o.size && CompareFileTime(&lastWrite, &o.lastWrite) == 0;
    }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};

static FileStamp GetFileStamp(const std::wstring& path) {
    FileStamp s;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) &&
        !(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        s.exists    = true;
        s.lastWrite = fad.ftLastWriteTime;
        s.size      = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    }
    return s;
}

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
// via the "Edit connections" / "Edit actions" buttons (in Notepad, a separate
// process) show up without a restart.
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
            g_actions = loadActions(actionsPath);
            WarnAboutDuplicateActions(hWnd, g_actions);  // B13
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
        PopulateListView(g_hListView);
    }
}

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
        if (!actions.empty()) {
            msg += L"\n\nDefined actions:";
            for (const Action& a : actions) {
                msg += L" " + a.action;
            }
        }
        MessageBoxW(hWnd, msg.c_str(), L"DDLaunch+ - Unknown type", MB_ICONWARNING | MB_OK);
        return;
    }

    if (action->command.empty()) {
        MessageBoxW(hWnd,
            (L"The \"" + type + L"\" action in actionDefinitions.json has an empty \"command\".").c_str(),
            L"DDLaunch+ - No command", MB_ICONWARNING | MB_OK);
        return;
    }

    std::wstring file, params;
    const DWORD err = LaunchSomething(hWnd, action->command, action->args, dev.host, file, params);
    ReportLaunchFailure(hWnd, err,
        L"\"" + hostLabel + L"\" (type " + type + L")", file, params,
        L"Check the \"command\" and \"args\" for " + type + L" in actionDefinitions.json.");
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
const int ROW_GAP     = 6;    // gap between the two button rows
const int MIN_LIST_H  = 80;   // smallest list height allowed by resizing
const int COL_W[3]    = { 200, 100, 300 };  // initial column widths
const int WINDOW_W    = 600;  // initial window size
const int WINDOW_H    = 400;
}

static int S(int designValue) { return Dpi::Scale(designValue, g_dpi); }

// ---------------------------------------------------------------------------
// Window placement persistence (R8)
//
// settings.json lives next to the other config files:
//   { "window": { "x": 120, "y": 80, "width": 640, "height": 420,
//                 "maximized": false } }
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

static std::optional<WindowSettings> LoadWindowSettings() {
    try {
        const std::wstring path = GetSettingsJsonPath();
        if (!FileExists(path)) {
            return std::nullopt;
        }
        const json j = json::parse(ReadFileUtf8(path));
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

// Writes to a temporary file first, then swaps it in, so a crash or power
// loss mid-write never leaves a truncated settings.json behind.
static void SaveWindowSettings(HWND hWnd) {
    if (IsRectEmpty(&g_normalRect)) {
        return;  // never shown in the restored state; nothing worth saving
    }

    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(wp);
    bool maximized = IsZoomed(hWnd) != FALSE;
    if (GetWindowPlacement(hWnd, &wp)) {
        maximized = wp.showCmd == SW_SHOWMAXIMIZED ||
                    (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED));
    }

    const UINT dpi = g_normalRectDpi ? g_normalRectDpi : Dpi::kBase;
    json j;
    j["window"] = {
        { "x",         g_normalRect.left },
        { "y",         g_normalRect.top },
        { "width",     MulDiv(g_normalRect.right - g_normalRect.left, Dpi::kBase, dpi) },
        { "height",    MulDiv(g_normalRect.bottom - g_normalRect.top, Dpi::kBase, dpi) },
        { "maximized", maximized },
    };
    const std::string text = j.dump(4) + "\n";

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

static int ButtonAreaHeight() {
    return S(Layout::BTN_H) * 2 + S(Layout::ROW_GAP) + S(Layout::MARGIN) * 2;
}

// Apply the current DPI: (re)create the font, set it on every control and
// re-measure the buttons. Called on WM_CREATE and WM_DPICHANGED.
static void ApplyDpi(HWND hWnd) {
    HFONT newFont = Dpi::CreateUiFont(g_dpi);
    const HWND controls[] = { g_hListView, g_hBtnRDP, g_hBtnEditActions,
                              g_hBtnEditConn, g_hBtnRun, g_hBtnExit };
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

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_dpi = Dpi::ForWindow(hWnd);

        // Create ListView (WS_TABSTOP so Tab can reach it)
        g_hListView = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_LISTVIEW, GetModuleHandleW(nullptr), nullptr);

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

        // Buttons are created in on-screen reading order, which is also the
        // Tab order: top row left-to-right, then bottom row left-to-right.
        //   [ Launch RDP ... ]        [ Edit actionDefinitions.json ]
        //   [ Edit connections.json ] [ Run... ] [ Exit ]
        g_hBtnRDP         = CreateButton(hWnd, L"Launch RDP connection for host not in this list", IDC_BTN_RDP);
        g_hBtnEditActions = CreateButton(hWnd, L"Edit actionDefinitions.json", IDC_BTN_EDIT_ACTIONS);
        g_hBtnEditConn    = CreateButton(hWnd, L"Edit connections.json", IDC_BTN_EDIT_CONN);
        g_hBtnRun         = CreateButton(hWnd, L"Run...", IDC_BTN_RUN);
        g_hBtnExit        = CreateButton(hWnd, L"Exit", IDC_BTN_EXIT);

        // DPI-correct font on every control, and button width to fit text
        ApplyDpi(hWnd);

        // Load data (connections.json + actionDefinitions.json). Forced here;
        // the WM_ACTIVATE that follows ShowWindow sees unchanged stamps and
        // skips a second load.
        ReloadAllData(hWnd, true);

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

        // ListView fills everything above the two button rows
        MoveWindow(g_hListView, 0, 0, rc.right, rc.bottom - ButtonAreaHeight(), TRUE);

        const int totalW = btnW * 2 + gap;
        const int startX = (rc.right - totalW) / 2;
        const int rightX = startX + btnW + gap;
        const int row2Y  = rc.bottom - btnH - margin;
        const int row1Y  = row2Y - rowGap - btnH;

        // Top row: Launch RDP | Edit actionDefinitions.json
        MoveWindow(g_hBtnRDP,         startX, row1Y, btnW, btnH, TRUE);
        MoveWindow(g_hBtnEditActions, rightX, row1Y, btnW, btnH, TRUE);

        // Bottom row: Edit connections.json | Run... | Exit
        // Run and Exit split the right-hand slot so both rows stay aligned.
        const int halfW = (btnW - gap) / 2;
        MoveWindow(g_hBtnEditConn, startX,               row2Y, btnW,  btnH, TRUE);
        MoveWindow(g_hBtnRun,      rightX,               row2Y, halfW, btnH, TRUE);
        MoveWindow(g_hBtnExit,     rightX + halfW + gap, row2Y, halfW, btnH, TRUE);

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
        case IDOK:
            // IsDialogMessageW turns Enter into IDOK when the focused control
            // isn't a push button. On the list that means "launch".
            if (GetFocus() == g_hListView) {
                ActivateSelection(hWnd);
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
            OpenJsonFileForEditing(hWnd, GetConnectionsJsonPath());
            break;
        case IDC_BTN_EDIT_ACTIONS:
            OpenJsonFileForEditing(hWnd, GetActionsJsonPath());
            break;
        }
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);
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
            // size changed (B1).
            ReloadAllData(hWnd, false);

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
            SaveWindowSettings(hWnd);
        }
        return 0;

    case WM_DESTROY:
        SaveWindowSettings(hWnd);  // R8
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

    // Must run before any window is created.
    Dpi::EnableDpiAwareness();

    // Common Controls v6 (visual styles) -- requires DDLaunch.manifest to be
    // embedded via DDLaunch.rc.
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
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
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        Layout::WINDOW_W,
        Layout::WINDOW_H,
        nullptr,
        nullptr,
        hInst,
        nullptr);

    if (!hWnd) return 1;

    // Work out where the window should open (R8, R10):
    //  - saved placement: saved top-left, saved size scaled for the DPI of
    //    the monitor it will be on;
    //  - otherwise: where Windows put it (CW_USEDEFAULT), at the default
    //    size scaled for that monitor.
    // Either way the rectangle is then clamped to that monitor's work area.
    const std::optional<WindowSettings> saved = LoadWindowSettings();
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

    // IsDialogMessageW gives the main window dialog-style keyboard handling:
    // Tab/Shift+Tab, arrow keys between buttons, Enter/Space on buttons.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
