// Data-Driven Launcher+ version 2.1.0
// C++ (Win32 API)
// Author: Glenn Madine
// Release_Date: 09/25/2026
// Changes in 2.1.0 (UI polish):
//   - Keyboard: IsDialogMessageW in the message loop, so Tab / Shift+Tab move
//     between the list and the buttons, Enter/Space press the focused
//     button, and Enter on the list launches the selected entry. Tab order
//     follows the on-screen layout; focus is restored on re-activation.
//   - Common Controls v6 manifest dependency (#pragma comment(linker)).
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
//               (folder and files are created automatically if missing)
// Compile and link command line:
//     CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunch.cpp DDLaunch.res /Fe:DDLaunch.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shldisp.h>  // IShellDispatch (Shell.Application) -- used for the Run dialog
#include <shlobj.h>   // SHGetKnownFolderPath / FOLDERID_Profile -- fallback for %USERPROFILE%
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include "json.hpp"   // nlohmann/json single-header
#include "resource.h"
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Common Controls v6: visual styles, header sort arrows (HDF_SORTUP/DOWN) and
// themed buttons. The linker writes this dependency into the manifest; build
// with /MANIFEST:EMBED (see the command line above) so it is embedded in the
// .exe. If DDLaunch.rc also embeds its own RT_MANIFEST resource, remove that
// one (or remove this pragma) to avoid a duplicate-manifest link error.
#if defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define VERSION         		L"v2.1.0"
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

// --- 1. The structure that mirrors each entry in the JSON array -----------
struct Action {
    std::wstring action;	// e.g. L"RDP"
    std::wstring args;		// e.g. L" /v:" note the space before /v: 
    std::wstring command;	// e.g. L"C:\\Windows\\System32\\MSTSC.exe"
};

// The JSON layer still deals in UTF-8 std::string (that's what's on disk
// and what nlohmann::json's default json type holds) -- these two
// functions are where the UTF-8 <-> UTF-16/wstring conversion happens.
void from_json(const json& j, Action& a) {
    std::string actionUtf8, argsUtf8, commandUtf8;
    j.at("action").get_to(actionUtf8);  
    j.at("args").get_to(argsUtf8); 
    j.at("command").get_to(commandUtf8);
    a.action = Utf8ToWide(actionUtf8);
    a.args = Utf8ToWide(argsUtf8);    
    a.command = Utf8ToWide(commandUtf8);
}
  
void to_json(json& j, const Action& a) {
    j = json{{"action", WideToUtf8(a.action)}, {"args", WideToUtf8(a.args)}, {"command", WideToUtf8(a.command)}}; 
}

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
// One lookup returning the whole entry (replaces the former separate
// findCommandByAction / findArgsByAction, which searched twice and forced a
// "Default" fallback when nothing matched). Returns nullptr if not found.
const Action* findAction(const std::vector<Action>& actions, const std::wstring& actionName) {
    auto it = std::find_if(actions.begin(), actions.end(),
                           [&actionName](const Action& a) { return a.action == actionName; });
    return it == actions.end() ? nullptr : &*it;
}

// --- 5. Case-insensitive lookup, used only to improve the error message ----
// Types are upper-cased before lookup but action names are matched exactly,
// so an action written as "Rdp" never matches. This lets the "no action"
// error point that out instead of just saying nothing was found.
const Action* findActionIgnoreCase(const std::vector<Action>& actions, const std::wstring& actionName) {
    auto it = std::find_if(actions.begin(), actions.end(),
                           [&actionName](const Action& a) { return _wcsicmp(a.action.c_str(), actionName.c_str()) == 0; });
    return it == actions.end() ? nullptr : &*it;
}

// --- 6. Windows-specific helpers -------------------------------------------

// Directory containing the running .exe, with a trailing backslash.
// Falls back to L"" if it can't be determined.
std::wstring getExeDirectory() {
    wchar_t pathBuf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return L"";
    }
    std::wstring exePath(pathBuf, len);
    size_t slash = exePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L"";
    }
    return exePath.substr(0, slash + 1);
}

// Simple append-only log file writer built on Win32 (CreateFileW/WriteFile)
// so, like ReadFileUtf8 above, it doesn't depend on std::wofstream's
// wide-path support. Lines are handed in as std::wstring and converted to
// UTF-8 only at the moment they're written to disk.
class Logger {
public:
    explicit Logger(const std::wstring& path) {
        handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    ~Logger() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    void writeLine(const std::wstring& line) {
        OutputDebugStringW((line + L"\n").c_str());
        if (handle_ == INVALID_HANDLE_VALUE) {
            return;
        }
        std::string utf8Line = WideToUtf8(line) + "\n";
        DWORD written = 0;
        WriteFile(handle_, utf8Line.data(), static_cast<DWORD>(utf8Line.size()), &written, nullptr);
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};
// ---------------------------------------------------------------------------
// Device record
// ---------------------------------------------------------------------------
struct Device {
    std::wstring host;
    std::wstring type;
    std::wstring description;
};

// ---------------------------------------------------------------------------
// Helper: narrow -> wide
// ---------------------------------------------------------------------------
static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

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
static std::vector<Device> LoadConnections(HWND hOwner, const std::wstring& jsonPath) {
    std::vector<Device> devices;

    // Read via Win32 (ReadFileUtf8) instead of std::ifstream so the path can
    // contain non-ASCII characters -- important now that the file lives under
    // %USERPROFILE%, which frequently includes the user's real name.
    std::string contents;
    try {
        contents = ReadFileUtf8(jsonPath);
    } catch (const std::exception&) {
        MessageBoxW(hOwner,
            (L"Error: Could not open " + jsonPath).c_str(),
            L"DDLaunch+", MB_ICONERROR | MB_OK);
        return devices;
    }

    try {
        nlohmann::json data = nlohmann::json::parse(contents);

        auto parseDevice = [&](const nlohmann::json& obj) {
            if (!obj.is_object()) return;
            Device d;
            if (obj.contains("host")    && obj["host"].is_string())
                d.host    = ToWide(obj["host"].get<std::string>());
            if (obj.contains("type")    && obj["type"].is_string())
                d.type    = ToWide(obj["type"].get<std::string>());
            if (obj.contains("description") && obj["description"].is_string())
                d.description = ToWide(obj["description"].get<std::string>());
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

    } catch (const nlohmann::json::exception& e) {
        MessageBoxA(hOwner, e.what(), "DDLaunch+ JSON Error", MB_ICONERROR | MB_OK);
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

    // Quote the path in case the exe lives under a directory with spaces.
    std::wstring quotedPath = L"\"" + path + L"\"";
    HINSTANCE result = ShellExecuteW(hWnd, L"open", L"notepad.exe", quotedPath.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hWnd, (L"Could not open Notepad for:\n" + path).c_str(),
                    L"DDLaunch+", MB_ICONERROR | MB_OK);
    }
}

// ---------------------------------------------------------------------------
// Launch functions
// ---------------------------------------------------------------------------
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(
        CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(
        CP_UTF8, 0, wstr.data(), (int)wstr.size(), result.data(), sizeNeeded, nullptr, nullptr);
    return result;
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
        params = args + host;
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

// Ask for username via InputBox-style dialog
struct UsernameDialog {
    static std::wstring username;

    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemTextW(hDlg, IDOK + 10, L"");
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                wchar_t buf[256] = {};
                GetDlgItemTextW(hDlg, IDOK + 10, buf, 256);
                username = buf;
                EndDialog(hDlg, IDOK);
            } else if (LOWORD(wParam) == IDCANCEL) {
                username.clear();
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }
        return FALSE;
    }
};
std::wstring UsernameDialog::username;

// Simple programmatic dialog for username input
static std::wstring GetUsernamePopup(HWND parent) {
    // Build dialog template in memory
    struct {
        DLGTEMPLATE tmpl;
        WORD menu, cls, title[9];    // "Username"
        // Controls follow
    } dlgBase = {};

    // Use a simpler approach: InputBox via a modal dialog resource-less
    // We'll use a quick custom DialogBoxIndirect approach
    struct DlgLayout {
        DLGTEMPLATE tmpl;
        WORD        padding[3]; // menu=0, class=0, title
        WCHAR       title[12];  // "SSH Login\0"
    };

    // Use the easiest portable approach: a small custom window
    // For simplicity, use MessageBox-style with an edit control via DialogBoxIndirect

    const int EDIT_ID = 100;

    // Pack dialog template manually
    BYTE dlgMem[512] = {};
    DLGTEMPLATE* pDlg = reinterpret_cast<DLGTEMPLATE*>(dlgMem);
    pDlg->style       = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    pDlg->dwExtendedStyle = 0;
    pDlg->cdit        = 3;   // label + edit + OK button
    pDlg->x = 0; pDlg->y = 0; pDlg->cx = 200; pDlg->cy = 70;

    // After DLGTEMPLATE: menu (0), windowClass (0), title
    WORD* pw = reinterpret_cast<WORD*>(pDlg + 1);
    *pw++ = 0; // no menu
    *pw++ = 0; // default dialog class
    // Title: "SSH Login"
    const wchar_t title[] = L"SSH Login";
    for (const wchar_t* p = title; *p; ++p) *pw++ = *p;
    *pw++ = 0;
    // Font point size + face
    *pw++ = 9;
    const wchar_t face[] = L"Segoe UI";
    for (const wchar_t* p = face; *p; ++p) *pw++ = *p;
    *pw++ = 0;

    // Align to DWORD
    auto align4 = [](WORD*& p) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(p);
        if (addr % 4) p = reinterpret_cast<WORD*>((addr + 3) & ~3ULL);
    };
    align4(pw);

    // --- STATIC label ---
    DLGITEMTEMPLATE* pItem = reinterpret_cast<DLGITEMTEMPLATE*>(pw);
    pItem->style           = WS_CHILD | WS_VISIBLE | SS_LEFT;
    pItem->dwExtendedStyle = 0;
    pItem->x = 7; pItem->y = 10; pItem->cx = 186; pItem->cy = 10;
    pItem->id = (WORD)-1;
    pw = reinterpret_cast<WORD*>(pItem + 1);
    *pw++ = 0xFFFF; *pw++ = 0x0082; // STATIC
    const wchar_t lbl[] = L"Enter your username:";
    for (const wchar_t* p = lbl; *p; ++p) *pw++ = *p;
    *pw++ = 0;
    *pw++ = 0; // creation data
    align4(pw);

    // --- EDIT control ---
    pItem = reinterpret_cast<DLGITEMTEMPLATE*>(pw);
    pItem->style           = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    pItem->dwExtendedStyle = 0;
    pItem->x = 7; pItem->y = 23; pItem->cx = 186; pItem->cy = 14;
    pItem->id = EDIT_ID;
    pw = reinterpret_cast<WORD*>(pItem + 1);
    *pw++ = 0xFFFF; *pw++ = 0x0081; // EDIT
    *pw++ = 0; // no caption
    *pw++ = 0; // creation data
    align4(pw);

    // --- OK Button ---
    pItem = reinterpret_cast<DLGITEMTEMPLATE*>(pw);
    pItem->style           = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON;
    pItem->dwExtendedStyle = 0;
    pItem->x = 75; pItem->y = 45; pItem->cx = 50; pItem->cy = 14;
    pItem->id = IDOK;
    pw = reinterpret_cast<WORD*>(pItem + 1);
    *pw++ = 0xFFFF; *pw++ = 0x0080; // BUTTON
    const wchar_t ok[] = L"OK";
    for (const wchar_t* p = ok; *p; ++p) *pw++ = *p;
    *pw++ = 0;
    *pw++ = 0;
    align4(pw);

    // Dialog proc captures the edit text
    static wchar_t resultBuf[256];
    resultBuf[0] = L'\0';
    static int editId = EDIT_ID;

    struct Proc {
        static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM /*lParam*/) {
            switch (msg) {
            case WM_INITDIALOG:
                SetFocus(GetDlgItem(hDlg, editId));
                return FALSE;
            case WM_COMMAND:
                if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                    if (LOWORD(wParam) == IDOK)
                        GetDlgItemTextW(hDlg, editId, resultBuf, 256);
                    else
                        resultBuf[0] = L'\0';
                    EndDialog(hDlg, LOWORD(wParam));
                    return TRUE;
                }
                break;
            }
            return FALSE;
        }
    };

    DialogBoxIndirectW(GetModuleHandleW(nullptr),
        reinterpret_cast<DLGTEMPLATE*>(dlgMem),
        parent, Proc::DlgProc);

    return std::wstring(resultBuf);
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

// Call once, before any window is created. The manifest is the officially
// preferred place for this, but an API call keeps everything in the source
// file. If a manifest already sets DPI awareness, these calls simply fail
// and the manifest setting wins.
static void EnableDpiAwareness() {
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

}  // namespace Dpi

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

static void PopulateListView(HWND hLV, const std::vector<Action>& actions) {
    ListView_DeleteAllItems(hLV);

    for (int i = 0; i < (int)g_devices.size(); ++i) {
        LVITEMW lvi = {};
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = i;
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(g_devices[i].host.c_str());
        ListView_InsertItem(hLV, &lvi);

        ListView_SetItemText(hLV, i, COL_TYPE,
            const_cast<LPWSTR>(g_devices[i].type.c_str()));
        ListView_SetItemText(hLV, i, COL_DESCRIPTION,
            const_cast<LPWSTR>(g_devices[i].description.c_str()));
    }
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

    if (reloadConn) {
        g_devices = LoadConnections(hWnd, connPath);
        // B2: keep the user's chosen sort order (and the header arrow, which
        // is left unchanged) instead of reverting to file order.
        ApplyCurrentSort();
    }

    if (reloadActions) {
        try {
            g_actions = loadActions(actionsPath);
        } catch (const std::exception& e) {
            // actionDefinitions.json is missing or not valid JSON (e.g.
            // mid-edit). Keep whatever actions we already had rather than
            // crashing the app. Shown once per save thanks to the stamp.
            MessageBoxA(hWnd, e.what(), "DDLaunch+ - Could not load actionDefinitions.json",
                        MB_ICONWARNING | MB_OK);
        }
    }

    // Actions don't appear in the list, so only repaint when devices changed.
    if (reloadConn && g_hListView) {
        PopulateListView(g_hListView, g_actions);
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

    const Action* action = findAction(actions, type);
    if (!action) {
        std::wstring msg = L"No action is defined for type \"" + dev.type + L"\" (host \"" + hostLabel + L"\").\n\n";
        if (const Action* nearMiss = findActionIgnoreCase(actions, type)) {
            msg += L"actionDefinitions.json has an action named \"" + nearMiss->action +
                   L"\", but action names must be written in UPPER CASE. Rename it to \"" + type + L"\".";
        } else {
            msg += L"Add an entry with \"action\": \"" + type + L"\" to actionDefinitions.json, "
                   L"or change this connection's type in connections.json.";
            if (!actions.empty()) {
                msg += L"\n\nDefined actions:";
                for (const Action& a : actions) {
                    msg += L" " + a.action;
                }
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
static void ActivateSelection(HWND hWnd) {
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel >= 0) OnItemActivated(hWnd, sel, g_actions);
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

    case WM_DPICHANGED: {
        // Window moved to a monitor with a different DPI (or the user changed
        // scaling). Rescale fonts, columns and buttons, then take the size
        // Windows suggests.
        const UINT oldDpi = g_dpi;
        g_dpi = LOWORD(wParam);

        ApplyDpi(hWnd);
        for (int col = 0; col < 3; ++col) {
            const int w = ListView_GetColumnWidth(g_hListView, col);
            ListView_SetColumnWidth(g_hListView, col,
                MulDiv(w, static_cast<int>(g_dpi), static_cast<int>(oldDpi)));
        }

        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
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
                PopulateListView(g_hListView, g_actions);
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

    case WM_DESTROY:
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
// WinMain
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Must run before any window is created.
    Dpi::EnableDpiAwareness();

    // Common Controls v6 (visual styles) -- requires the manifest dependency
    // declared at the top of this file.
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Register window class. Icons are loaded at the exact sizes Windows
    // wants (large and small) so they aren't blurred by scaling.
    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);  // dialog-style background behind the buttons
    wc.lpszClassName  = L"DDLaunchClass";
    wc.hIcon   = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
                     GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    std::wstring windowTitle = std::wstring(L"DDLaunch+ ") + VERSION + L" - to launch a resource, doubleclick a lineitem below.";

    // WS_EX_CONTROLPARENT lets the dialog manager (IsDialogMessageW) treat
    // this window like a dialog for Tab navigation.
    HWND hWnd = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        L"DDLaunchClass",
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

    // The window was created at 96-DPI size; scale it for the monitor it
    // landed on (WM_GETMINMAXINFO still enforces the minimum size).
    SetWindowPos(hWnd, nullptr, 0, 0, S(Layout::WINDOW_W), S(Layout::WINDOW_H),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    ShowWindow(hWnd, nCmdShow);
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
