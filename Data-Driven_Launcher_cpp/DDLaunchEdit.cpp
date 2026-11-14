// DDLaunch+ Editor version 1.0.1
// C++ (Win32 API)
// Author: Glenn Madine
// Release_Date: 11/14/2026
// Changes in 1.0.1:
//   - "About" link at the left end of the status line. Clicking it, or
//     pressing Enter on it, opens an About box with the version, author,
//     and release date. (SysLink control; a plain clickable
//     label is used if Common Controls v6 isn't available.)
//
// A form-based editor for the two DDLaunch+ configuration files:
//   %USERPROFILE%\DDLaunch\connections.json        (Connections tab)
//   %USERPROFILE%\DDLaunch\actionDefinitions.json  (Actions tab)
// so they can be maintained without hand-editing JSON.
//
// Features
//   - One window, two tabs. Each tab has a list of entries and a details
//     form below it. Edits apply as you type; Save writes both files.
//   - Connections: Host, Type (a dropdown of the defined actions, or type
//     any name), Description and Group. A filter box narrows the list.
//   - Actions: Action name, Command (with Browse...), Arguments, and a test
//     host. Renaming an action offers to update the connections that use it.
//   - "Launches:" line: shows exactly what DDLaunch+ will run or open for the
//     entry, using the same rules as DDLaunch+ (host quoting, URL building).
//   - Problems (no host, unknown type, duplicate or unnamed action, no
//     command) appear in a Check column, in red, and are listed before saving.
//   - Safe saving: the previous file is kept as <name>.json.bak, and the new
//     file is written to a temporary file and swapped in.
//   - Round trip: key order and any extra fields in an entry are kept.
//     connections.json keeps its format: a flat array stays an array; a file
//     of named groups stays grouped (entries with no group are saved under
//     "Ungrouped"). Typing a group into a flat file switches it to groups.
//   - Notices when a file is changed outside the editor (e.g. in Notepad):
//     reloads it if there are no unsaved edits, otherwise asks.
//   - Single instance: starting it again brings the running editor forward
//     and switches to the requested tab.
//   - Per-Monitor V2 DPI aware; window position, size and last tab are
//     remembered in %USERPROFILE%\DDLaunch\editor.json.
//
// Command line:  DDLaunchEdit.exe [/connections | /actions]
//   (DDLaunch+ 2.6.0 and later starts it this way from its Edit buttons.)
//
// Keyboard:  Ctrl+S save   Ctrl+N add   Ctrl+D duplicate   Del delete (in list)
//            Ctrl+Up / Ctrl+Down move   F5 reload   Ctrl+F filter
//            Ctrl+Tab / Ctrl+Shift+Tab switch tabs   Enter (in list) edit
//
// Requires: Windows SDK, nlohmann/json (single-header, included as json.hpp)
// Build files: DDLaunchEdit.cpp, DDLaunchEdit.rc, DDLaunchEdit.manifest,
//              resource.h, DDLaunch.ico, json.hpp
// Compile and link command lines:
//     rc /nologo DDLaunchEdit.rc
//     CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 DDLaunchEdit.cpp DDLaunchEdit.res /Fe:DDLaunchEdit.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib comdlg32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib
// The manifest comes only from DDLaunchEdit.rc; /MANIFEST:NO stops the
// linker adding a second one (Linker > Manifest File > Generate Manifest = No).
// Put DDLaunchEdit.exe in the same folder as DDLaunch.exe so DDLaunch+'s Edit
// buttons find it.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <cwctype>
#include <cstdint>
#include "json.hpp"
#include "resource.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// ordered_json keeps the keys of each object in file order, so a saved file
// looks like the one that was loaded (nlohmann::json would sort them).
using ojson = nlohmann::ordered_json;

#define APP_NAME     L"DDLaunch+ Editor"
#define APP_VERSION  L"v1.0.1"
// Shown in the About box. Keep in step with the header and DDLaunchEdit.rc.
#define APP_AUTHOR        L"Glenn Madine"
#define APP_RELEASE_DATE  L"11/14/2026"

// ---------------------------------------------------------------------------
// Control IDs
// ---------------------------------------------------------------------------
enum : int {
    IDC_TAB = 100, IDC_STATUS, IDC_BTN_SAVE, IDC_BTN_RELOAD, IDC_BTN_NOTEPAD, IDC_BTN_CLOSE,
    IDC_LINK_ABOUT,

    // Connections tab
    IDC_C_FILTER_LBL = 200, IDC_C_FILTER, IDC_C_LIST, IDC_C_ADD, IDC_C_DUP, IDC_C_DEL, IDC_C_UP,
    IDC_C_DOWN, IDC_C_SEP, IDC_C_LBL_HOST, IDC_C_HOST, IDC_C_LBL_TYPE, IDC_C_TYPE, IDC_C_LBL_DESC,
    IDC_C_DESC, IDC_C_LBL_GROUP, IDC_C_GROUP, IDC_C_LBL_RUNS, IDC_C_RUNS,

    // Actions tab
    IDC_A_LIST = 300, IDC_A_ADD, IDC_A_DUP, IDC_A_DEL, IDC_A_UP, IDC_A_DOWN, IDC_A_SEP,
    IDC_A_LBL_NAME, IDC_A_NAME, IDC_A_LBL_CMD, IDC_A_CMD, IDC_A_BROWSE, IDC_A_LBL_ARGS, IDC_A_ARGS,
    IDC_A_LBL_TEST, IDC_A_TEST, IDC_A_LBL_RUNS, IDC_A_RUNS,

    // Keyboard shortcuts
    ID_ACC_SAVE = 900, ID_ACC_ADD, ID_ACC_DUP, ID_ACC_RELOAD, ID_ACC_UP, ID_ACC_DOWN,
    ID_ACC_NEXTTAB, ID_ACC_PREVTAB, ID_ACC_FIND,
};

enum { TAB_CONN = 0, TAB_ACT = 1 };

#define WM_APP_CHECKFILES  (WM_APP + 1)   // look for changes made outside the editor
#define WM_APP_SHOWERRORS  (WM_APP + 2)   // report load errors once the window is visible

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------
static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) throw std::runtime_error("UTF-8 to UTF-16 conversion failed");
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
    return w;
}

static std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0) throw std::runtime_error("UTF-16 to UTF-8 conversion failed");
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// Text of a std::exception (UTF-8, from this file or nlohmann::json). Never throws.
static std::wstring ExceptionText(const std::exception& e) {
    try { return Utf8ToWide(e.what()); } catch (...) { return L"(error text could not be converted)"; }
}

// Same case rule as DDLaunch+: ordinal, ignoring case, independent of locale.
static bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

static bool LessIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
}

static std::wstring ToUpper(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towupper(c));
    return s;
}

static bool ContainsIgnoreCase(const std::wstring& haystack, const std::wstring& needleUpper) {
    return needleUpper.empty() || ToUpper(haystack).find(needleUpper) != std::wstring::npos;
}

static std::wstring Trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return std::wstring();
    return s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
}

static std::wstring GetText(HWND h) {
    const int len = GetWindowTextLengthW(h);
    if (len <= 0) return std::wstring();
    std::wstring t(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(h, &t[0], len + 1);
    t.resize(got > 0 ? static_cast<size_t>(got) : 0);
    return t;
}

static std::wstring GetErrorText(DWORD err) {
    LPWSTR buf = nullptr;
    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
                               reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring text = (len && buf) ? std::wstring(buf, len) : std::wstring(L"Unknown error.");
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    return text;
}

// ---------------------------------------------------------------------------
// Files and folders (same locations and rules as DDLaunch+)
// ---------------------------------------------------------------------------
static std::string ReadFileUtf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Could not open file: " + WideToUtf8(path));
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        throw std::runtime_error("Could not get file size: " + WideToUtf8(path));
    }
    std::string buf(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf.empty() ? nullptr : &buf[0], static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != buf.size()) {
        throw std::runtime_error("Failed reading file: " + WideToUtf8(path));
    }
    return buf;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirectoryExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return buf;
}

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
    PWSTR known = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &known)) && known) {
        result = known;
    }
    CoTaskMemFree(known);
    return result;
}

// %USERPROFILE%\DDLaunch, or the .exe folder if the profile can't be found.
static std::wstring GetDataDir() {
    std::wstring profile = GetUserProfileDir();
    if (profile.empty()) return GetExeDir();
    if (profile.back() == L'\\' || profile.back() == L'/') profile.pop_back();
    return profile + L"\\DDLaunch";
}

static std::wstring ConnectionsPath() { return GetDataDir() + L"\\connections.json"; }
static std::wstring ActionsPath()     { return GetDataDir() + L"\\actionDefinitions.json"; }
static std::wstring EditorSettingsPath() { return GetDataDir() + L"\\editor.json"; }

// Last-write time and size, to notice changes made outside the editor.
struct FileStamp {
    bool exists = false;
    FILETIME lastWrite{};
    ULONGLONG size = 0;
    bool operator==(const FileStamp& o) const {
        if (exists != o.exists) return false;
        if (!exists) return true;
        return size == o.size && CompareFileTime(&lastWrite, &o.lastWrite) == 0;
    }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};

static FileStamp GetFileStamp(const std::wstring& path) {
    FileStamp s;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) &&
        !(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        s.exists = true;
        s.lastWrite = fad.ftLastWriteTime;
        s.size = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    }
    return s;
}

// Write `utf8` to `path` safely: the current file (if any) is copied to
// <path>.bak, the new text goes to <path>.tmp, which then replaces <path>.
// Returns L"" on success, otherwise a description of what failed.
static std::wstring WriteFileSafely(const std::wstring& path, const std::string& utf8, bool backup) {
    const std::wstring dir = GetDataDir();
    if (!DirectoryExists(dir) && !CreateDirectoryW(dir.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return L"Could not create the folder " + dir + L": " + GetErrorText(GetLastError());
    }
    if (backup && FileExists(path)) {
        CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);  // best effort
    }
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return L"Could not create " + tmp + L": " + GetErrorText(GetLastError());
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    const DWORD writeErr = GetLastError();
    CloseHandle(h);
    if (!ok || written != utf8.size()) {
        DeleteFileW(tmp.c_str());
        return L"Could not write " + tmp + L": " + GetErrorText(writeErr);
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD err = GetLastError();
        DeleteFileW(tmp.c_str());
        return L"Could not replace " + path + L": " + GetErrorText(err);
    }
    return std::wstring();
}

// ---------------------------------------------------------------------------
// Launch preview -- the same rules DDLaunch+ 2.3.1+ uses to build what it
// passes to ShellExecuteExW (StartsWithUrlScheme, HostIsFullUrl,
// EncodeUrlUnsafeChars, QuoteArgIfNeeded). Keep these in step with
// DDLaunch.cpp if those rules ever change.
// ---------------------------------------------------------------------------
static bool IsAsciiAlpha(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
static bool IsAsciiDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

static bool StartsWithUrlScheme(const std::wstring& text) {
    const size_t colon = text.find(L':');
    if (colon == std::wstring::npos || colon < 2 || !IsAsciiAlpha(text[0])) return false;
    for (size_t i = 1; i < colon; ++i) {
        const wchar_t c = text[i];
        if (!IsAsciiAlpha(c) && !IsAsciiDigit(c) && c != L'+' && c != L'-' && c != L'.') return false;
    }
    return true;
}

static std::wstring UrlScheme(const std::wstring& text) {
    return StartsWithUrlScheme(text) ? text.substr(0, text.find(L':')) : std::wstring();
}

static bool HostIsFullUrl(const std::wstring& host, const std::wstring& command) {
    const std::wstring scheme = UrlScheme(host);
    if (scheme.empty()) return false;
    if (host.compare(scheme.size(), 3, L"://") == 0) return true;
    const std::wstring cmdScheme = UrlScheme(command);
    return !cmdScheme.empty() && EqualsIgnoreCase(scheme, cmdScheme);
}

static std::wstring EncodeUrlUnsafeChars(const std::wstring& text) {
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring out;
    for (wchar_t c : text) {
        if (c <= 0x20 || c == 0x7F || wcschr(L"\"<>^`{|}", c) != nullptr) {
            out += L'%';
            out += kHex[(c >> 4) & 0xF];
            out += kHex[c & 0xF];
        } else {
            out += c;
        }
    }
    return out;
}

static std::wstring QuoteArgIfNeeded(const std::wstring& arg) {
    if (arg.empty() || arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
        else           out.append(backslashes, L'\\');
        out += c;
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

// One line describing what DDLaunch+ will launch for this command/args/host
// (shown after the "Launches:" label).
static std::wstring DescribeLaunch(const std::wstring& command, const std::wstring& args,
                                   const std::wstring& host) {
    if (command.empty()) return L"Nothing - this action has no command.";
    if (StartsWithUrlScheme(command)) {
        return EncodeUrlUnsafeChars(HostIsFullUrl(host, command) ? host : command + host) +
               L"   (opened with the app registered for it)";
    }
    const std::wstring shownCmd = command.find(L' ') != std::wstring::npos ? L"\"" + command + L"\"" : command;
    const std::wstring params = Trim(args + QuoteArgIfNeeded(host));
    return shownCmd + (params.empty() ? L"" : L"  " + params);
}

// ---------------------------------------------------------------------------
// The data being edited
// ---------------------------------------------------------------------------
struct ConnEntry {
    UINT_PTR id = 0;                    // stable ID, stored in the list row's lParam
    ojson obj = ojson::object();        // the entry as loaded (keeps extra fields and key order)
    std::wstring host, type, description, group;
};

struct ActionEntry {
    UINT_PTR id = 0;
    ojson obj = ojson::object();
    std::wstring action, args, command;
};

struct DocState {
    FileStamp stamp;            // file on disk when last loaded or saved
    bool dirty = false;         // unsaved edits
    std::wstring loadError;     // non-empty: the file could not be read; editing it is disabled
    int skipped = 0;            // non-entry values that will be dropped on save
    bool grouped = false;       // connections.json only: saved as named groups
};

static std::vector<ConnEntry>   g_conns;
static std::vector<ActionEntry> g_acts;
static DocState g_connDoc, g_actDoc;
static UINT_PTR g_nextId = 1;

static const wchar_t kUngrouped[] = L"Ungrouped";

static std::wstring StrField(const ojson& o, const char* key) {
    auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? Utf8ToWide(it->get<std::string>()) : std::wstring();
}

// First action with this name (ignoring case), as DDLaunch+ resolves it.
static const ActionEntry* FindAction(const std::wstring& name) {
    if (name.empty()) return nullptr;
    for (const ActionEntry& a : g_acts) {
        if (EqualsIgnoreCase(a.action, name)) return &a;
    }
    return nullptr;
}

static int CountConnsUsing(const std::wstring& actionName) {
    if (actionName.empty()) return 0;
    int n = 0;
    for (const ConnEntry& c : g_conns) {
        if (EqualsIgnoreCase(c.type, actionName)) ++n;
    }
    return n;
}

// Problem with a connection, or L"" if none.
static std::wstring ConnProblem(const ConnEntry& c) {
    if (Trim(c.host).empty()) return L"No host";
    if (Trim(c.type).empty()) return L"No type";
    if (!g_actDoc.loadError.empty()) return std::wstring();  // can't check types
    if (!FindAction(c.type)) return L"Unknown type";
    return std::wstring();
}

// Problem with the action at `index`, or L"" if none.
static std::wstring ActProblem(size_t index) {
    const ActionEntry& a = g_acts[index];
    if (Trim(a.action).empty()) return L"No name";
    for (size_t i = 0; i < index; ++i) {
        if (EqualsIgnoreCase(g_acts[i].action, a.action)) {
            return L"Duplicate of entry " + std::to_wstring(i + 1) + L" (ignored)";
        }
    }
    if (Trim(a.command).empty()) return L"No command";
    return std::wstring();
}

static size_t FindConnIndex(UINT_PTR id) {
    for (size_t i = 0; i < g_conns.size(); ++i) if (g_conns[i].id == id) return i;
    return static_cast<size_t>(-1);
}

static size_t FindActIndex(UINT_PTR id) {
    for (size_t i = 0; i < g_acts.size(); ++i) if (g_acts[i].id == id) return i;
    return static_cast<size_t>(-1);
}

// --- Loading ---------------------------------------------------------------

static void LoadConnectionsDoc() {
    g_conns.clear();
    g_connDoc = DocState{};
    const std::wstring path = ConnectionsPath();
    g_connDoc.stamp = GetFileStamp(path);
    if (!g_connDoc.stamp.exists) return;  // starts empty; created on save

    try {
        const ojson j = ojson::parse(ReadFileUtf8(path));
        auto add = [&](const ojson& o, const std::wstring& group) {
            if (!o.is_object()) { ++g_connDoc.skipped; return; }
            ConnEntry e;
            e.id = g_nextId++;
            e.obj = o;
            e.host = StrField(o, "host");
            e.type = StrField(o, "type");
            e.description = StrField(o, "description");
            e.group = group;
            g_conns.push_back(std::move(e));
        };
        if (j.is_array()) {
            for (const ojson& item : j) add(item, L"");
        } else if (j.is_object()) {
            g_connDoc.grouped = true;
            for (auto it = j.begin(); it != j.end(); ++it) {
                const std::wstring group = Utf8ToWide(it.key());
                if (it.value().is_object())      add(it.value(), group);
                else if (it.value().is_array())  for (const ojson& item : it.value()) add(item, group);
                else                             ++g_connDoc.skipped;
            }
        } else {
            throw std::runtime_error("The file must contain a JSON array or an object of named groups.");
        }
    } catch (const std::exception& e) {
        g_conns.clear();
        g_connDoc.loadError = ExceptionText(e);
    }
}

static void LoadActionsDoc() {
    g_acts.clear();
    g_actDoc = DocState{};
    const std::wstring path = ActionsPath();
    g_actDoc.stamp = GetFileStamp(path);
    if (!g_actDoc.stamp.exists) return;

    try {
        const ojson j = ojson::parse(ReadFileUtf8(path));
        if (!j.is_array()) {
            throw std::runtime_error("The file must contain a JSON array of actions.");
        }
        for (const ojson& o : j) {
            if (!o.is_object()) { ++g_actDoc.skipped; continue; }
            ActionEntry e;
            e.id = g_nextId++;
            e.obj = o;
            e.action = StrField(o, "action");
            e.args = StrField(o, "args");
            e.command = StrField(o, "command");
            g_acts.push_back(std::move(e));
        }
    } catch (const std::exception& e) {
        g_acts.clear();
        g_actDoc.loadError = ExceptionText(e);
    }
}

// --- Saving ----------------------------------------------------------------

static ojson ConnToJson(const ConnEntry& e) {
    ojson o = e.obj.is_object() ? e.obj : ojson::object();
    auto put = [&](const char* key, const std::wstring& value, bool always) {
        if (always || !value.empty() || o.contains(key)) o[key] = WideToUtf8(value);
    };
    put("host", e.host, true);
    put("type", e.type, true);
    put("description", e.description, false);
    return o;
}

static ojson ActToJson(const ActionEntry& e) {
    ojson o = e.obj.is_object() ? e.obj : ojson::object();
    o["action"]  = WideToUtf8(e.action);
    o["args"]    = WideToUtf8(e.args);
    o["command"] = WideToUtf8(e.command);
    return o;
}

static bool ConnsUseGroups() {
    if (g_connDoc.grouped) return true;
    return std::any_of(g_conns.begin(), g_conns.end(), [](const ConnEntry& c) { return !Trim(c.group).empty(); });
}

// Build connections.json. With groups, entries are gathered by group (in
// order of first appearance) and g_conns is reordered to match the file.
static std::string BuildConnectionsText() {
    if (!ConnsUseGroups()) {
        ojson arr = ojson::array();
        for (const ConnEntry& e : g_conns) arr.push_back(ConnToJson(e));
        return arr.dump(4) + "\n";
    }

    for (ConnEntry& e : g_conns) {
        e.group = Trim(e.group);
        if (e.group.empty()) e.group = kUngrouped;
    }
    std::vector<std::wstring> order;
    for (const ConnEntry& e : g_conns) {
        if (std::none_of(order.begin(), order.end(), [&](const std::wstring& g) { return g == e.group; })) {
            order.push_back(e.group);
        }
    }
    std::vector<ConnEntry> sorted;
    ojson root = ojson::object();
    for (const std::wstring& g : order) {
        ojson arr = ojson::array();
        for (const ConnEntry& e : g_conns) {
            if (e.group == g) {
                arr.push_back(ConnToJson(e));
                sorted.push_back(e);
            }
        }
        root[WideToUtf8(g)] = std::move(arr);
    }
    g_conns = std::move(sorted);
    return root.dump(4) + "\n";
}

static std::string BuildActionsText() {
    ojson arr = ojson::array();
    for (const ActionEntry& e : g_acts) arr.push_back(ActToJson(e));
    return arr.dump(4) + "\n";
}

// ---------------------------------------------------------------------------
// DPI helpers (as in DDLaunch+)
// ---------------------------------------------------------------------------
namespace Dpi {
const UINT kBase = USER_DEFAULT_SCREEN_DPI;

template <typename Fn>
static Fn LoadUser32(const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name)));
}

static void EnableDpiAwareness() {
    if (IsProcessDPIAware()) return;  // the manifest already set it
    using PFN_SetCtx = BOOL(WINAPI*)(HANDLE);
    if (auto setCtx = LoadUser32<PFN_SetCtx>("SetProcessDpiAwarenessContext")) {
        if (setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) return;  // PER_MONITOR_AWARE_V2
    }
    SetProcessDPIAware();
}

static UINT SystemDpi() {
    HDC hdc = GetDC(nullptr);
    UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : kBase;
    if (hdc) ReleaseDC(nullptr, hdc);
    return dpi ? dpi : kBase;
}

static UINT ForWindow(HWND hWnd) {
    using PFN = UINT(WINAPI*)(HWND);
    static auto getDpi = LoadUser32<PFN>("GetDpiForWindow");
    if (getDpi && hWnd) {
        UINT d = getDpi(hWnd);
        if (d) return d;
    }
    return SystemDpi();
}

static UINT ForMonitor(HMONITOR hMon) {
    using PFN = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static PFN fn = []() -> PFN {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        return shcore ? reinterpret_cast<PFN>(reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor")))
                      : nullptr;
    }();
    UINT x = 0, y = 0;
    if (fn && hMon && SUCCEEDED(fn(hMon, 0, &x, &y)) && x) return x;
    return SystemDpi();
}

static int Scale(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), static_cast<int>(kBase)); }

static HFONT CreateUiFont(UINT dpi) {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    using PFN = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static auto spiForDpi = LoadUser32<PFN>("SystemParametersInfoForDpi");
    if (spiForDpi && spiForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) {
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, static_cast<int>(dpi),
                                            static_cast<int>(SystemDpi()));
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    return nullptr;
}
}  // namespace Dpi

static void ClampToWorkArea(RECT& rc, HMONITOR hMon) {
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!hMon || !GetMonitorInfoW(hMon, &mi)) return;
    const RECT& w = mi.rcWork;
    LONG cw = (std::min)(rc.right - rc.left, w.right - w.left);
    LONG ch = (std::min)(rc.bottom - rc.top, w.bottom - w.top);
    LONG x = (std::max)(w.left, (std::min)(rc.left, w.right - cw));
    LONG y = (std::max)(w.top, (std::min)(rc.top, w.bottom - ch));
    rc = RECT{ x, y, x + cw, y + ch };
}

// ---------------------------------------------------------------------------
// Window state
// ---------------------------------------------------------------------------
static HWND  g_hWnd = nullptr;
static HWND  g_hTab = nullptr, g_hStatus = nullptr, g_hLinkAbout = nullptr;
static HWND  g_hBtnSave = nullptr, g_hBtnReload = nullptr, g_hBtnNotepad = nullptr, g_hBtnClose = nullptr;

// Connections tab
static HWND  g_cFilterLbl, g_cFilter, g_cList, g_cAdd, g_cDup, g_cDel, g_cUp, g_cDown, g_cSep;
static HWND  g_cLblHost, g_cHost, g_cLblType, g_cType, g_cLblDesc, g_cDesc, g_cLblGroup, g_cGroup,
             g_cLblRuns, g_cRuns;
// Actions tab
static HWND  g_aList, g_aAdd, g_aDup, g_aDel, g_aUp, g_aDown, g_aSep;
static HWND  g_aLblName, g_aName, g_aLblCmd, g_aCmd, g_aBrowse, g_aLblArgs, g_aArgs, g_aLblTest, g_aTest,
             g_aLblRuns, g_aRuns;

static std::vector<HWND> g_connControls, g_actControls, g_allControls;

static HFONT g_hFont = nullptr;
static UINT  g_dpi = USER_DEFAULT_SCREEN_DPI;
static int   g_tab = TAB_CONN;
static UINT_PTR g_selId[2] = { 0, 0 };   // selected entry per tab (0 = none)
static bool  g_filling = false;          // true while code fills the form (ignore change notifications)
static bool  g_rebuilding = false;       // true while a list is being refilled
static bool  g_checkingFiles = false;
static std::wstring g_nameAtFocus;       // action name when its box got focus (rename detection)
static HWND  g_lastFocus = nullptr;
static RECT  g_normalRect = {};
static UINT  g_normalRectDpi = USER_DEFAULT_SCREEN_DPI;
static bool  g_placingWindow = false;
static UINT  g_msgShowTab = 0;           // registered message: another instance asks for a tab

static int S(int v) { return Dpi::Scale(v, g_dpi); }

static void SetStatus(const std::wstring& text) { SetWindowTextW(g_hStatus, text.c_str()); }

static bool AnyDirty() { return g_connDoc.dirty || g_actDoc.dirty; }

// Window title and tab captions show counts and unsaved changes ("*").
static void UpdateTitleAndTabs() {
    std::wstring title = std::wstring(APP_NAME L" ") + APP_VERSION;
    if (AnyDirty()) title += L" - unsaved changes";
    SetWindowTextW(g_hWnd, title.c_str());

    const std::wstring captions[2] = {
        L"Connections (" + std::to_wstring(g_conns.size()) + L")" + (g_connDoc.dirty ? L" *" : L""),
        L"Actions (" + std::to_wstring(g_acts.size()) + L")" + (g_actDoc.dirty ? L" *" : L""),
    };
    for (int i = 0; i < 2; ++i) {
        TCITEMW ti = {};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<LPWSTR>(captions[i].c_str());
        TabCtrl_SetItem(g_hTab, i, &ti);
    }
    InvalidateRect(g_hTab, nullptr, TRUE);  // tabs change width when "*" comes and goes
    EnableWindow(g_hBtnSave, AnyDirty());
}

static void MarkDirty(int tab) {
    (tab == TAB_CONN ? g_connDoc : g_actDoc).dirty = true;
    UpdateTitleAndTabs();
}

// ---------------------------------------------------------------------------
// Lists
// ---------------------------------------------------------------------------
static int RowForId(HWND lv, UINT_PTR id) {
    LVFINDINFOW fi = {};
    fi.flags = LVFI_PARAM;
    fi.lParam = static_cast<LPARAM>(id);
    return ListView_FindItem(lv, -1, &fi);
}

static void SetCell(HWND lv, int row, int col, const std::wstring& text) {
    ListView_SetItemText(lv, row, col, const_cast<LPWSTR>(text.c_str()));
}

static std::wstring CheckText(const std::wstring& problem) {
    return problem.empty() ? std::wstring(L"OK") : problem;
}

static void SetConnRow(int row, const ConnEntry& c) {
    SetCell(g_cList, row, 0, c.host);
    SetCell(g_cList, row, 1, c.type);
    SetCell(g_cList, row, 2, c.description);
    SetCell(g_cList, row, 3, c.group);
    SetCell(g_cList, row, 4, CheckText(ConnProblem(c)));
}

static void SetActRow(int row, size_t index) {
    const ActionEntry& a = g_acts[index];
    SetCell(g_aList, row, 0, a.action);
    SetCell(g_aList, row, 1, a.command);
    SetCell(g_aList, row, 2, L"\"" + a.args + L"\"");  // quotes make leading spaces visible
    // Only the first entry with a name is used by DDLaunch+, so only it counts.
    const int used = FindAction(a.action) == &a ? CountConnsUsing(a.action) : 0;
    SetCell(g_aList, row, 3, used ? std::to_wstring(used) : std::wstring(L"-"));
    SetCell(g_aList, row, 4, CheckText(ActProblem(index)));
}

// Refresh every row's text (cheap; used after edits that can affect other
// rows, e.g. a rename creating or removing a duplicate).
static void RefreshRows(int tab) {
    HWND lv = tab == TAB_CONN ? g_cList : g_aList;
    const int n = ListView_GetItemCount(lv);
    for (int row = 0; row < n; ++row) {
        LVITEMW li = {};
        li.mask = LVIF_PARAM;
        li.iItem = row;
        if (!ListView_GetItem(lv, &li)) continue;
        const UINT_PTR id = static_cast<UINT_PTR>(li.lParam);
        if (tab == TAB_CONN) {
            const size_t i = FindConnIndex(id);
            if (i != static_cast<size_t>(-1)) SetConnRow(row, g_conns[i]);
        } else {
            const size_t i = FindActIndex(id);
            if (i != static_cast<size_t>(-1)) SetActRow(row, i);
        }
    }
}

static bool ConnMatchesFilter(const ConnEntry& c, const std::wstring& filterUpper) {
    return filterUpper.empty() || ContainsIgnoreCase(c.host, filterUpper) || ContainsIgnoreCase(c.type, filterUpper) ||
           ContainsIgnoreCase(c.description, filterUpper) || ContainsIgnoreCase(c.group, filterUpper);
}

static void OnSelectionChanged(int tab);

// Refill a list from the data, selecting the entry with `selectId` if shown.
static void RebuildList(int tab, UINT_PTR selectId) {
    HWND lv = tab == TAB_CONN ? g_cList : g_aList;
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    g_rebuilding = true;
    ListView_DeleteAllItems(lv);

    const std::wstring filterUpper = tab == TAB_CONN ? ToUpper(Trim(GetText(g_cFilter))) : std::wstring();
    int row = 0, selRow = -1;
    const size_t n = tab == TAB_CONN ? g_conns.size() : g_acts.size();
    for (size_t i = 0; i < n; ++i) {
        if (tab == TAB_CONN && !ConnMatchesFilter(g_conns[i], filterUpper)) continue;
        LVITEMW li = {};
        li.mask = LVIF_TEXT | LVIF_PARAM;
        li.iItem = row;
        li.pszText = const_cast<LPWSTR>(L"");
        li.lParam = static_cast<LPARAM>(tab == TAB_CONN ? g_conns[i].id : g_acts[i].id);
        ListView_InsertItem(lv, &li);
        if (tab == TAB_CONN) SetConnRow(row, g_conns[i]); else SetActRow(row, i);
        if (selectId && static_cast<UINT_PTR>(li.lParam) == selectId) selRow = row;
        ++row;
    }
    if (selRow >= 0) {
        ListView_SetItemState(lv, selRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(lv, selRow, FALSE);
    }
    g_rebuilding = false;
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, nullptr, TRUE);
    OnSelectionChanged(tab);
}

// ---------------------------------------------------------------------------
// Details forms
// ---------------------------------------------------------------------------

// Fill a combo box's list without losing its typed text.
static void SetComboChoices(HWND combo, const std::vector<std::wstring>& items) {
    const bool wasFilling = g_filling;
    g_filling = true;
    const std::wstring text = GetText(combo);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& s : items) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
    }
    SetWindowTextW(combo, text.c_str());
    g_filling = wasFilling;
}

// Type choices: the defined action names (first of any duplicates), A-Z.
// Group choices: the groups in use, A-Z.
static void RefreshConnChoices() {
    std::vector<std::wstring> types;
    for (const ActionEntry& a : g_acts) {
        if (!Trim(a.action).empty() && FindAction(a.action) == &a) types.push_back(a.action);
    }
    std::sort(types.begin(), types.end(), LessIgnoreCase);
    SetComboChoices(g_cType, types);

    std::vector<std::wstring> groups;
    for (const ConnEntry& c : g_conns) {
        const std::wstring g = Trim(c.group);
        if (!g.empty() && std::none_of(groups.begin(), groups.end(),
                                       [&](const std::wstring& x) { return EqualsIgnoreCase(x, g); })) {
            groups.push_back(g);
        }
    }
    std::sort(groups.begin(), groups.end(), LessIgnoreCase);
    SetComboChoices(g_cGroup, groups);
}

static void UpdateConnPreview() {
    const size_t i = FindConnIndex(g_selId[TAB_CONN]);
    std::wstring text;
    if (i != static_cast<size_t>(-1)) {
        const ConnEntry& c = g_conns[i];
        const std::wstring host = Trim(c.host);
        if (host.empty()) {
            text = L"Nothing yet - enter a host (host name, user@host, host:port, a path or a URL).";
        } else if (Trim(c.type).empty()) {
            text = L"Nothing yet - pick a type. It chooses which action DDLaunch+ runs.";
        } else if (const ActionEntry* a = FindAction(c.type)) {
            text = DescribeLaunch(a->command, a->args, host);
        } else if (!g_actDoc.loadError.empty()) {
            text = L"(actionDefinitions.json could not be read, so this can't be checked)";
        } else {
            text = L"Nothing - there is no action named \"" + c.type + L"\". Pick one from the list, or add it on the Actions tab.";
        }
    }
    SetWindowTextW(g_cRuns, text.c_str());
}

static void UpdateActPreview() {
    const size_t i = FindActIndex(g_selId[TAB_ACT]);
    std::wstring text;
    if (i != static_cast<size_t>(-1)) {
        std::wstring host = Trim(GetText(g_aTest));
        if (host.empty()) host = L"server01";
        text = DescribeLaunch(g_acts[i].command, g_acts[i].args, host);
        const std::wstring problem = ActProblem(i);
        if (!problem.empty() && problem != L"No command") text = problem + L".   " + text;
    }
    SetWindowTextW(g_aRuns, text.c_str());
}

static void UpdateButtons(int tab) {
    const bool conn = tab == TAB_CONN;
    const bool blocked = !(conn ? g_connDoc : g_actDoc).loadError.empty();
    const size_t n = conn ? g_conns.size() : g_acts.size();
    const size_t i = conn ? FindConnIndex(g_selId[TAB_CONN]) : FindActIndex(g_selId[TAB_ACT]);
    const bool sel = i != static_cast<size_t>(-1);
    const bool canMove = sel && !blocked && (!conn || Trim(GetText(g_cFilter)).empty());

    EnableWindow(conn ? g_cAdd : g_aAdd, !blocked);
    EnableWindow(conn ? g_cDup : g_aDup, sel && !blocked);
    EnableWindow(conn ? g_cDel : g_aDel, sel && !blocked);
    EnableWindow(conn ? g_cUp : g_aUp, canMove && i > 0);
    EnableWindow(conn ? g_cDown : g_aDown, canMove && i + 1 < n);
    EnableWindow(conn ? g_cList : g_aList, !blocked);
    if (conn) EnableWindow(g_cFilter, !blocked);

    const HWND fields[2][5] = { { g_cHost, g_cType, g_cDesc, g_cGroup, nullptr },
                                { g_aName, g_aCmd, g_aBrowse, g_aArgs, g_aTest } };
    for (HWND f : fields[conn ? 0 : 1]) {
        if (f) EnableWindow(f, sel && !blocked);
    }
}

static void FillConnFields() {
    g_filling = true;
    const size_t i = FindConnIndex(g_selId[TAB_CONN]);
    const bool sel = i != static_cast<size_t>(-1);
    SetWindowTextW(g_cHost, sel ? g_conns[i].host.c_str() : L"");
    SetWindowTextW(g_cType, sel ? g_conns[i].type.c_str() : L"");
    SetWindowTextW(g_cDesc, sel ? g_conns[i].description.c_str() : L"");
    SetWindowTextW(g_cGroup, sel ? g_conns[i].group.c_str() : L"");
    g_filling = false;
    UpdateConnPreview();
}

static void FillActFields() {
    g_filling = true;
    const size_t i = FindActIndex(g_selId[TAB_ACT]);
    const bool sel = i != static_cast<size_t>(-1);
    SetWindowTextW(g_aName, sel ? g_acts[i].action.c_str() : L"");
    SetWindowTextW(g_aCmd, sel ? g_acts[i].command.c_str() : L"");
    SetWindowTextW(g_aArgs, sel ? g_acts[i].args.c_str() : L"");
    g_filling = false;
    UpdateActPreview();
}

static void OnSelectionChanged(int tab) {
    HWND lv = tab == TAB_CONN ? g_cList : g_aList;
    const int row = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    UINT_PTR id = 0;
    if (row >= 0) {
        LVITEMW li = {};
        li.mask = LVIF_PARAM;
        li.iItem = row;
        if (ListView_GetItem(lv, &li)) id = static_cast<UINT_PTR>(li.lParam);
    }
    g_selId[tab] = id;
    if (tab == TAB_CONN) FillConnFields(); else FillActFields();
    UpdateButtons(tab);
}

// A form field changed (typing or a dropdown pick): copy it into the data.
static void OnConnFieldChanged(HWND field, bool fromDropdownPick) {
    if (g_filling) return;
    const size_t i = FindConnIndex(g_selId[TAB_CONN]);
    if (i == static_cast<size_t>(-1)) return;

    std::wstring value;
    if (fromDropdownPick) {  // CBN_SELCHANGE comes before the edit text changes
        const int sel = static_cast<int>(SendMessageW(field, CB_GETCURSEL, 0, 0));
        if (sel < 0) return;
        const int len = static_cast<int>(SendMessageW(field, CB_GETLBTEXTLEN, sel, 0));
        value.assign(static_cast<size_t>((std::max)(len, 0)) + 1, L'\0');
        SendMessageW(field, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(&value[0]));
        value.resize(static_cast<size_t>((std::max)(len, 0)));
    } else {
        value = GetText(field);
    }

    ConnEntry& c = g_conns[i];
    if (field == g_cHost) c.host = value;
    else if (field == g_cType) c.type = value;
    else if (field == g_cDesc) c.description = value;
    else if (field == g_cGroup) c.group = value;
    else return;

    MarkDirty(TAB_CONN);
    const int row = RowForId(g_cList, c.id);
    if (row >= 0) SetConnRow(row, c);
    UpdateConnPreview();
}

static void OnActFieldChanged(HWND field) {
    if (g_filling) return;
    if (field == g_aTest) { UpdateActPreview(); return; }
    const size_t i = FindActIndex(g_selId[TAB_ACT]);
    if (i == static_cast<size_t>(-1)) return;

    ActionEntry& a = g_acts[i];
    const std::wstring value = GetText(field);
    if (field == g_aName) a.action = value;
    else if (field == g_aCmd) a.command = value;
    else if (field == g_aArgs) a.args = value;
    else return;

    MarkDirty(TAB_ACT);
    RefreshRows(TAB_ACT);  // a rename can create or clear a duplicate elsewhere
    UpdateActPreview();
}

// The action name box lost focus: if the action was renamed and connections
// use the old name (and no other action still has it), offer to update them.
static void OnActionNameCommitted() {
    const size_t i = FindActIndex(g_selId[TAB_ACT]);
    if (i == static_cast<size_t>(-1)) return;
    const std::wstring oldName = Trim(g_nameAtFocus);
    const std::wstring newName = Trim(g_acts[i].action);
    g_nameAtFocus = g_acts[i].action;
    if (oldName.empty() || newName.empty() || EqualsIgnoreCase(oldName, newName) || FindAction(oldName)) return;

    const int used = CountConnsUsing(oldName);
    if (used == 0) return;
    const std::wstring msg = std::to_wstring(used) + (used == 1 ? L" connection uses" : L" connections use") +
        L" the type \"" + oldName + L"\".\n\nChange " + (used == 1 ? L"it" : L"them") + L" to \"" + newName + L"\"?";
    if (MessageBoxW(g_hWnd, msg.c_str(), L"Action renamed", MB_ICONQUESTION | MB_YESNO) == IDYES) {
        for (ConnEntry& c : g_conns) {
            if (EqualsIgnoreCase(c.type, oldName)) c.type = newName;
        }
        MarkDirty(TAB_CONN);
        RefreshRows(TAB_ACT);
        SetStatus(L"Updated " + std::to_wstring(used) + L" connection" + (used == 1 ? L"" : L"s") +
                  L" to the type \"" + newName + L"\".");
    }
}

// ---------------------------------------------------------------------------
// Commands: add, duplicate, delete, move
// ---------------------------------------------------------------------------
static void FocusAndSelectAll(HWND h) {
    SetFocus(h);
    SendMessageW(h, EM_SETSEL, 0, -1);
}

static void OnAdd(int tab) {
    if (tab == TAB_CONN) {
        if (!g_connDoc.loadError.empty()) return;
        const size_t i = FindConnIndex(g_selId[TAB_CONN]);
        ConnEntry e;
        e.id = g_nextId++;
        if (i != static_cast<size_t>(-1)) {       // start like the selected entry
            e.type = g_conns[i].type;
            e.group = g_conns[i].group;
        } else if (!g_acts.empty()) {             // DDLaunch+'s default action
            e.type = g_acts.front().action;
        }
        const size_t at = i == static_cast<size_t>(-1) ? g_conns.size() : i + 1;
        g_conns.insert(g_conns.begin() + static_cast<ptrdiff_t>(at), e);
        if (!Trim(GetText(g_cFilter)).empty()) {
            g_filling = true;
            SetWindowTextW(g_cFilter, L"");       // make sure the new entry is visible
            g_filling = false;
        }
        MarkDirty(TAB_CONN);
        RefreshConnChoices();
        RebuildList(TAB_CONN, e.id);
        SetFocus(g_cHost);
        SetStatus(L"New connection added. Type its host, then pick a type.");
    } else {
        if (!g_actDoc.loadError.empty()) return;
        const size_t i = FindActIndex(g_selId[TAB_ACT]);
        ActionEntry e;
        e.id = g_nextId++;
        const size_t at = i == static_cast<size_t>(-1) ? g_acts.size() : i + 1;
        g_acts.insert(g_acts.begin() + static_cast<ptrdiff_t>(at), e);
        MarkDirty(TAB_ACT);
        RebuildList(TAB_ACT, e.id);
        SetFocus(g_aName);
        g_nameAtFocus.clear();
        SetStatus(L"New action added. Give it a name and a command.");
    }
}

static void OnDuplicate(int tab) {
    if (tab == TAB_CONN) {
        const size_t i = FindConnIndex(g_selId[TAB_CONN]);
        if (i == static_cast<size_t>(-1)) return;
        ConnEntry e = g_conns[i];
        e.id = g_nextId++;
        g_conns.insert(g_conns.begin() + static_cast<ptrdiff_t>(i) + 1, e);
        MarkDirty(TAB_CONN);
        RebuildList(TAB_CONN, e.id);
        FocusAndSelectAll(g_cHost);
        SetStatus(L"Copy added below the original. Change its host.");
    } else {
        const size_t i = FindActIndex(g_selId[TAB_ACT]);
        if (i == static_cast<size_t>(-1)) return;
        ActionEntry e = g_acts[i];
        e.id = g_nextId++;
        e.action += L"_copy";
        g_acts.insert(g_acts.begin() + static_cast<ptrdiff_t>(i) + 1, e);
        MarkDirty(TAB_ACT);
        RebuildList(TAB_ACT, e.id);
        FocusAndSelectAll(g_aName);
        g_nameAtFocus.clear();
        SetStatus(L"Copy added below the original. Give it a new name.");
    }
}

static void OnDelete(int tab) {
    if (tab == TAB_CONN) {
        const size_t i = FindConnIndex(g_selId[TAB_CONN]);
        if (i == static_cast<size_t>(-1)) return;
        const ConnEntry& c = g_conns[i];
        const std::wstring label = c.host.empty() ? std::wstring(L"this connection") : L"\"" + c.host + L"\"";
        if (MessageBoxW(g_hWnd, (L"Delete " + label + (c.type.empty() ? L"" : L" (" + c.type + L")") + L"?").c_str(),
                        L"Delete connection", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;

        // Select the row that takes its place in the list (or the one above).
        const int row = RowForId(g_cList, c.id);
        g_conns.erase(g_conns.begin() + static_cast<ptrdiff_t>(i));
        UINT_PTR next = 0;
        const int count = ListView_GetItemCount(g_cList);
        for (int r : { row + 1, row - 1 }) {
            if (r >= 0 && r < count) {
                LVITEMW li = {};
                li.mask = LVIF_PARAM;
                li.iItem = r;
                if (ListView_GetItem(g_cList, &li)) { next = static_cast<UINT_PTR>(li.lParam); break; }
            }
        }
        MarkDirty(TAB_CONN);
        RebuildList(TAB_CONN, next);
        SetFocus(g_cList);
    } else {
        const size_t i = FindActIndex(g_selId[TAB_ACT]);
        if (i == static_cast<size_t>(-1)) return;
        const ActionEntry& a = g_acts[i];
        const std::wstring name = a.action.empty() ? std::wstring(L"this action") : L"\"" + a.action + L"\"";
        // Connections only lose this action if no other entry has the same name.
        int used = 0;
        bool otherSameName = false;
        for (size_t k = 0; k < g_acts.size(); ++k) {
            if (k != i && EqualsIgnoreCase(g_acts[k].action, a.action)) otherSameName = true;
        }
        if (!otherSameName) used = CountConnsUsing(a.action);
        std::wstring msg = L"Delete " + name + L"?";
        if (used > 0) {
            msg = std::to_wstring(used) + (used == 1 ? L" connection uses " : L" connections use ") + name +
                  L". After deleting it, " + (used == 1 ? L"it" : L"they") +
                  L" will show \"Unknown type\" until you pick another type.\n\nDelete " + name + L" anyway?";
        }
        if (MessageBoxW(g_hWnd, msg.c_str(), L"Delete action", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
        g_acts.erase(g_acts.begin() + static_cast<ptrdiff_t>(i));
        UINT_PTR next = 0;
        if (!g_acts.empty()) next = g_acts[(std::min)(i, g_acts.size() - 1)].id;
        MarkDirty(TAB_ACT);
        RebuildList(TAB_ACT, next);
        SetFocus(g_aList);
    }
}

static void OnMove(int tab, int delta) {
    if (tab == TAB_CONN) {
        if (!Trim(GetText(g_cFilter)).empty()) return;  // order is ambiguous while filtered
        const size_t i = FindConnIndex(g_selId[TAB_CONN]);
        if (i == static_cast<size_t>(-1)) return;
        const ptrdiff_t j = static_cast<ptrdiff_t>(i) + delta;
        if (j < 0 || j >= static_cast<ptrdiff_t>(g_conns.size())) return;
        std::swap(g_conns[i], g_conns[static_cast<size_t>(j)]);
        MarkDirty(TAB_CONN);
        RebuildList(TAB_CONN, g_selId[TAB_CONN]);
    } else {
        const size_t i = FindActIndex(g_selId[TAB_ACT]);
        if (i == static_cast<size_t>(-1)) return;
        const ptrdiff_t j = static_cast<ptrdiff_t>(i) + delta;
        if (j < 0 || j >= static_cast<ptrdiff_t>(g_acts.size())) return;
        std::swap(g_acts[i], g_acts[static_cast<size_t>(j)]);
        MarkDirty(TAB_ACT);
        RebuildList(TAB_ACT, g_selId[TAB_ACT]);
    }
}

static void OnBrowseCommand() {
    wchar_t file[MAX_PATH] = {};
    const std::wstring current = Trim(GetText(g_aCmd));
    std::wstring initialDir;
    if (!current.empty() && !StartsWithUrlScheme(current) && FileExists(current)) {
        wcsncpy_s(file, current.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = L"Programs (*.exe;*.com;*.bat;*.cmd)\0*.exe;*.com;*.bat;*.cmd\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choose the program this action runs";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_aCmd, file);  // EN_CHANGE updates the data
        SetFocus(g_aCmd);
    }
}

// ---------------------------------------------------------------------------
// Load / save / reload
// ---------------------------------------------------------------------------
static void ShowLoadErrors() {
    std::wstring msg;
    if (!g_connDoc.loadError.empty()) {
        msg += L"connections.json could not be read:\n" + g_connDoc.loadError + L"\n\n";
    }
    if (!g_actDoc.loadError.empty()) {
        msg += L"actionDefinitions.json could not be read:\n" + g_actDoc.loadError + L"\n\n";
    }
    if (!msg.empty()) {
        msg += L"Editing is turned off for a file that can't be read, so it can't be overwritten by mistake. "
               L"Fix it with \"Open in Notepad\", then press Reload (F5).";
        MessageBoxW(g_hWnd, msg.c_str(), APP_NAME L" - File error", MB_ICONWARNING | MB_OK);
    }
}

static std::wstring LoadedStatus() {
    std::wstring s = L"Data folder: " + GetDataDir();
    const int skipped = g_connDoc.skipped + g_actDoc.skipped;
    if (skipped > 0) {
        s += L"    Note: " + std::to_wstring(skipped) +
             L" value(s) that aren't entries will be dropped when you save.";
    }
    if (!g_connDoc.loadError.empty() || !g_actDoc.loadError.empty()) {
        s += L"    A file has errors - see the message, fix it in Notepad, then Reload.";
    }
    return s;
}

static void LoadBoth() {
    LoadConnectionsDoc();
    LoadActionsDoc();
    g_selId[TAB_CONN] = g_conns.empty() ? 0 : g_conns.front().id;
    g_selId[TAB_ACT] = g_acts.empty() ? 0 : g_acts.front().id;
    RefreshConnChoices();
    RebuildList(TAB_CONN, g_selId[TAB_CONN]);
    RebuildList(TAB_ACT, g_selId[TAB_ACT]);
    UpdateTitleAndTabs();
    SetStatus(LoadedStatus());
}

// Problems across both files, one per line (at most `limit` lines).
static std::wstring CollectProblems(int limit, int& total) {
    std::wstring out;
    total = 0;
    auto add = [&](const std::wstring& line) {
        if (total++ < limit) out += L"\u2022 " + line + L"\n";
    };
    if (g_actDoc.dirty || g_connDoc.dirty) {
        for (size_t i = 0; i < g_acts.size(); ++i) {
            const std::wstring p = ActProblem(i);
            if (!p.empty()) add(L"Action " + std::to_wstring(i + 1) + (g_acts[i].action.empty() ? L"" : L" \"" + g_acts[i].action + L"\"") + L": " + p);
        }
        for (size_t i = 0; i < g_conns.size(); ++i) {
            const std::wstring p = ConnProblem(g_conns[i]);
            if (!p.empty()) add(L"Connection " + std::to_wstring(i + 1) + (g_conns[i].host.empty() ? L"" : L" \"" + g_conns[i].host + L"\"") +
                                L": " + p + (p == L"Unknown type" ? L" \"" + g_conns[i].type + L"\"" : L""));
        }
    }
    if (total > limit) out += L"\u2026and " + std::to_wstring(total - limit) + L" more\n";
    return out;
}

// Save both files if they have unsaved changes. Returns false if the user
// cancelled or a write failed (so closing should be cancelled too).
static bool SaveAll() {
    if (!AnyDirty()) return true;

    int total = 0;
    const std::wstring problems = CollectProblems(12, total);
    if (total > 0) {
        const std::wstring msg = L"There " + std::wstring(total == 1 ? L"is 1 problem" : L"are " + std::to_wstring(total) + L" problems") +
            L" that DDLaunch+ will complain about:\n\n" + problems + L"\nSave anyway?";
        if (MessageBoxW(g_hWnd, msg.c_str(), APP_NAME L" - Check before saving", MB_ICONWARNING | MB_YESNO) != IDYES) {
            return false;
        }
    }

    std::wstring saved, errors;
    const bool becameGrouped = !g_connDoc.grouped && ConnsUseGroups();
    try {
        if (g_actDoc.dirty && g_actDoc.loadError.empty()) {
            const std::wstring err = WriteFileSafely(ActionsPath(), BuildActionsText(), true);
            if (err.empty()) {
                g_actDoc.dirty = false;
                g_actDoc.skipped = 0;
                g_actDoc.stamp = GetFileStamp(ActionsPath());
                saved += L"actionDefinitions.json";
            } else {
                errors += err + L"\n";
            }
        }
        if (g_connDoc.dirty && g_connDoc.loadError.empty()) {
            const std::wstring err = WriteFileSafely(ConnectionsPath(), BuildConnectionsText(), true);
            if (err.empty()) {
                g_connDoc.dirty = false;
                g_connDoc.skipped = 0;
                g_connDoc.grouped = g_connDoc.grouped || becameGrouped;
                g_connDoc.stamp = GetFileStamp(ConnectionsPath());
                saved += (saved.empty() ? L"" : L" and ") + std::wstring(L"connections.json");
                RebuildList(TAB_CONN, g_selId[TAB_CONN]);  // grouped files may have been reordered
                RefreshConnChoices();
            } else {
                errors += err + L"\n";
            }
        }
    } catch (const std::exception& e) {
        errors += ExceptionText(e) + L"\n";
    }

    UpdateTitleAndTabs();
    if (!errors.empty()) {
        MessageBoxW(g_hWnd, (L"Saving failed:\n\n" + errors).c_str(), APP_NAME L" - Save failed", MB_ICONERROR | MB_OK);
        return false;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t when[16];
    swprintf_s(when, L"%02u:%02u", st.wHour, st.wMinute);
    SetStatus(L"Saved " + saved + L" at " + when + L" (previous version kept as .bak)." +
              (becameGrouped ? L" connections.json now uses named groups." : L"") +
              L" DDLaunch+ picks up the changes when you switch back to it.");
    return true;
}

// Ask about unsaved changes. Returns true if it's OK to go ahead.
static bool ConfirmDiscardOrSave(const wchar_t* action) {
    if (!AnyDirty()) return true;
    const std::wstring msg = std::wstring(L"You have unsaved changes.\n\nSave them before ") + action + L"?";
    switch (MessageBoxW(g_hWnd, msg.c_str(), APP_NAME, MB_ICONQUESTION | MB_YESNOCANCEL)) {
    case IDYES: return SaveAll();
    case IDNO:  return true;
    default:    return false;
    }
}

static void OnReload() {
    if (AnyDirty() &&
        MessageBoxW(g_hWnd, L"Discard your unsaved changes and reload both files from disk?", APP_NAME,
                    MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    LoadBoth();
    ShowLoadErrors();
}

static void OnOpenInNotepad() {
    const bool conn = g_tab == TAB_CONN;
    const DocState& doc = conn ? g_connDoc : g_actDoc;
    if (doc.dirty) {
        const int r = MessageBoxW(g_hWnd,
            L"This file has unsaved changes. Save them first?\n\n"
            L"(If you don't, Notepad shows the file as it is on disk.)",
            APP_NAME, MB_ICONQUESTION | MB_YESNOCANCEL);
        if (r == IDCANCEL || (r == IDYES && !SaveAll())) return;
    }
    const std::wstring path = conn ? ConnectionsPath() : ActionsPath();
    if (!FileExists(path)) {
        WriteFileSafely(path, "[]\n", false);
        (conn ? g_connDoc : g_actDoc).stamp = GetFileStamp(path);
    }
    const std::wstring quoted = L"\"" + path + L"\"";
    HINSTANCE r = ShellExecuteW(g_hWnd, L"open", L"notepad.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32) {
        MessageBoxW(g_hWnd, (L"Could not open Notepad for:\n" + path).c_str(), APP_NAME, MB_ICONERROR | MB_OK);
    } else {
        SetStatus(L"Opened in Notepad. Changes saved there are loaded when you switch back here.");
    }
}

// Files changed outside the editor (Notepad, DDLaunch+ creating defaults...)?
// Reload a changed file if it has no unsaved edits here; otherwise ask.
static void CheckFilesChangedOnDisk() {
    if (g_checkingFiles) return;
    g_checkingFiles = true;
    struct Item { DocState* doc; std::wstring path; int tab; const wchar_t* name; };
    const Item items[] = { { &g_connDoc, ConnectionsPath(), TAB_CONN, L"connections.json" },
                           { &g_actDoc, ActionsPath(), TAB_ACT, L"actionDefinitions.json" } };
    std::wstring reloaded;
    for (const Item& it : items) {
        const FileStamp now = GetFileStamp(it.path);
        if (now == it.doc->stamp) continue;
        bool reload = !it.doc->dirty;
        if (!reload) {
            const std::wstring msg = std::wstring(it.name) +
                L" was changed outside the editor.\n\nReload it and lose your unsaved changes to it?\n\n"
                L"(No keeps your version; saving will then overwrite the other changes.)";
            reload = MessageBoxW(g_hWnd, msg.c_str(), APP_NAME, MB_ICONWARNING | MB_YESNO) == IDYES;
        }
        if (reload) {
            if (it.tab == TAB_CONN) LoadConnectionsDoc(); else LoadActionsDoc();
            reloaded += (reloaded.empty() ? L"" : L" and ") + std::wstring(it.name);
            if (it.tab == TAB_CONN) {
                g_selId[TAB_CONN] = g_conns.empty() ? 0 : g_conns.front().id;
            } else {
                g_selId[TAB_ACT] = g_acts.empty() ? 0 : g_acts.front().id;
            }
        } else {
            it.doc->stamp = now;  // don't ask again for this version
        }
    }
    if (!reloaded.empty()) {
        RefreshConnChoices();
        RebuildList(TAB_CONN, g_selId[TAB_CONN]);
        RebuildList(TAB_ACT, g_selId[TAB_ACT]);
        UpdateTitleAndTabs();
        SetStatus(L"Reloaded " + reloaded + L" (changed outside the editor).");
        ShowLoadErrors();
    }
    g_checkingFiles = false;
}

// ---------------------------------------------------------------------------
// Editor settings (window placement, last tab, test host) -- editor.json
// ---------------------------------------------------------------------------
struct EditorSettings {
    bool hasWindow = false;
    int x = 0, y = 0, w96 = 0, h96 = 0;
    bool maximized = false;
    int tab = TAB_CONN;
    std::wstring testHost = L"server01";
};

static EditorSettings LoadEditorSettings() {
    EditorSettings s;
    try {
        if (!FileExists(EditorSettingsPath())) return s;
        const ojson j = ojson::parse(ReadFileUtf8(EditorSettingsPath()));
        if (j.contains("window") && j["window"].is_object()) {
            const ojson& w = j["window"];
            s.x = w.at("x").get<int>();
            s.y = w.at("y").get<int>();
            s.w96 = w.at("width").get<int>();
            s.h96 = w.at("height").get<int>();
            s.maximized = w.value("maximized", false);
            s.hasWindow = s.w96 >= 200 && s.h96 >= 200 && s.w96 <= 20000 && s.h96 <= 20000;
        }
        s.tab = j.value("tab", 0) == TAB_ACT ? TAB_ACT : TAB_CONN;
        if (j.contains("testHost") && j["testHost"].is_string()) s.testHost = Utf8ToWide(j["testHost"].get<std::string>());
    } catch (const std::exception&) {
        // corrupt or hand-edited: defaults
    }
    return s;
}

static void SaveEditorSettings() {
    try {
        ojson j = ojson::object();
        if (!IsRectEmpty(&g_normalRect)) {
            WINDOWPLACEMENT wp = {};
            wp.length = sizeof(wp);
            bool maximized = IsZoomed(g_hWnd) != FALSE;
            if (GetWindowPlacement(g_hWnd, &wp)) {
                maximized = wp.showCmd == SW_SHOWMAXIMIZED ||
                            (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED));
            }
            const UINT dpi = g_normalRectDpi ? g_normalRectDpi : Dpi::kBase;
            j["window"] = { { "x", g_normalRect.left }, { "y", g_normalRect.top },
                            { "width", MulDiv(g_normalRect.right - g_normalRect.left, Dpi::kBase, dpi) },
                            { "height", MulDiv(g_normalRect.bottom - g_normalRect.top, Dpi::kBase, dpi) },
                            { "maximized", maximized } };
        }
        j["tab"] = g_tab;
        j["testHost"] = WideToUtf8(GetText(g_aTest));
        if (DirectoryExists(GetDataDir())) WriteFileSafely(EditorSettingsPath(), j.dump(4) + "\n", false);
    } catch (const std::exception&) {
        // best effort
    }
}

static void TrackNormalRect() {
    if (!IsWindowVisible(g_hWnd) || IsIconic(g_hWnd) || IsZoomed(g_hWnd) || g_placingWindow) return;
    RECT rc;
    if (GetWindowRect(g_hWnd, &rc)) {
        g_normalRect = rc;
        g_normalRectDpi = g_dpi;
    }
}

// ---------------------------------------------------------------------------
// Layout (96-DPI design units, scaled with S())
// ---------------------------------------------------------------------------
namespace Layout {
const int MARGIN = 8, GAP = 6, ROW_H = 24, BTN_H = 26, TAB_H = 28, BTN_PAD = 24, MIN_BTN_W = 84;
const int COLS_CONN[5] = { 190, 90, 220, 110, 120 };
const int COLS_ACT[5] = { 110, 260, 110, 60, 170 };
const int WINDOW_W = 820, WINDOW_H = 620, MIN_W = 640, MIN_H = 520;
}

static int MeasureText(const wchar_t* text) {
    HDC hdc = GetDC(g_hWnd);
    if (!hdc) return 0;
    HGDIOBJ old = SelectObject(hdc, g_hFont ? g_hFont : GetStockObject(DEFAULT_GUI_FONT));
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &sz);
    SelectObject(hdc, old);
    ReleaseDC(g_hWnd, hdc);
    return sz.cx;
}

static int ButtonWidth(HWND b) {
    return (std::max)(S(Layout::MIN_BTN_W), MeasureText(GetText(b).c_str()) + S(Layout::BTN_PAD));
}

static void PlaceCombo(HWND combo, int x, int y, int w, int rowH) {
    MoveWindow(combo, x, y, w, S(220), TRUE);  // height = open list
    RECT rc = {};
    GetWindowRect(combo, &rc);
    SetWindowPos(combo, nullptr, x, y + (rowH - (rc.bottom - rc.top)) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void DoLayout() {
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int m = S(Layout::MARGIN), gap = S(Layout::GAP), rowH = S(Layout::ROW_H), btnH = S(Layout::BTN_H);

    // Tab strip
    MoveWindow(g_hTab, m, m, W - 2 * m, S(Layout::TAB_H), TRUE);

    // Bottom bar: status text, then Open in Notepad | Reload | Save | Close
    const int barY = H - m - btnH;
    int x = W - m;
    for (HWND b : { g_hBtnClose, g_hBtnSave, g_hBtnReload, g_hBtnNotepad }) {
        const int bw = ButtonWidth(b);
        x -= bw;
        MoveWindow(b, x, barY, bw, btnH, TRUE);
        x -= gap;
    }
    // About link at the far left, then the status text up to the buttons.
    const int linkW = MeasureText(L"About") + S(6);
    const int linkH = S(18);
    MoveWindow(g_hLinkAbout, m, barY + (btnH - linkH) / 2, linkW, linkH, TRUE);
    const int statusX = m + linkW + S(12);
    MoveWindow(g_hStatus, statusX, barY, (std::max)(0, x - statusX), btnH, TRUE);

    // Details form (5 rows) above the bottom bar
    const int detH = rowH * 5 + gap * 4;
    const int detBottom = barY - S(10);
    const int detTop = detBottom - detH;
    const int sepY = detTop - S(8);

    // Label column width: widest label of either tab
    int labelW = 0;
    for (const wchar_t* t : { L"Description:", L"Host:", L"Type:", L"Group:", L"Launches:", L"Action:",
                              L"Command:", L"Arguments:", L"Test host:" }) {
        labelW = (std::max)(labelW, MeasureText(t));
    }
    labelW += S(4);
    const int fieldX = m + labelW + gap;
    const int fieldW = W - fieldX - m;
    auto rowY = [&](int r) { return detTop + r * (rowH + gap); };
    auto placeLabel = [&](HWND lbl, int r) { MoveWindow(lbl, m, rowY(r), labelW, rowH, TRUE); };

    // Edit buttons row: Add | Duplicate | Delete | Move Up | Move Down
    const int editBtnY = sepY - S(8) - btnH;
    auto placeEditButtons = [&](std::initializer_list<HWND> btns) {
        int bx = m;
        for (HWND b : btns) {
            const int bw = ButtonWidth(b);
            MoveWindow(b, bx, editBtnY, bw, btnH, TRUE);
            bx += bw + gap;
        }
    };

    int top = m + S(Layout::TAB_H) + S(8);

    // --- Connections tab ---
    {
        const int fw = MeasureText(L"Filter:") + S(4);
        MoveWindow(g_cFilterLbl, m, top, fw, rowH, TRUE);
        MoveWindow(g_cFilter, m + fw + gap, top, (std::min)(S(320), W - 2 * m - fw - gap), rowH, TRUE);
        const int listTop = top + rowH + gap;
        MoveWindow(g_cList, m, listTop, W - 2 * m, (std::max)(S(40), editBtnY - gap - listTop), TRUE);
        placeEditButtons({ g_cAdd, g_cDup, g_cDel, g_cUp, g_cDown });
        MoveWindow(g_cSep, m, sepY, W - 2 * m, 2, TRUE);

        const int comboW = (std::min)(S(240), fieldW);
        placeLabel(g_cLblHost, 0);  MoveWindow(g_cHost, fieldX, rowY(0), fieldW, rowH, TRUE);
        placeLabel(g_cLblType, 1);  PlaceCombo(g_cType, fieldX, rowY(1), comboW, rowH);
        placeLabel(g_cLblDesc, 2);  MoveWindow(g_cDesc, fieldX, rowY(2), fieldW, rowH, TRUE);
        placeLabel(g_cLblGroup, 3); PlaceCombo(g_cGroup, fieldX, rowY(3), comboW, rowH);
        placeLabel(g_cLblRuns, 4);  MoveWindow(g_cRuns, fieldX, rowY(4), fieldW, rowH, TRUE);
    }

    // --- Actions tab ---
    {
        MoveWindow(g_aList, m, top, W - 2 * m, (std::max)(S(40), editBtnY - gap - top), TRUE);
        placeEditButtons({ g_aAdd, g_aDup, g_aDel, g_aUp, g_aDown });
        MoveWindow(g_aSep, m, sepY, W - 2 * m, 2, TRUE);

        const int browseW = ButtonWidth(g_aBrowse);
        placeLabel(g_aLblName, 0); MoveWindow(g_aName, fieldX, rowY(0), (std::min)(S(240), fieldW), rowH, TRUE);
        placeLabel(g_aLblCmd, 1);  MoveWindow(g_aCmd, fieldX, rowY(1), fieldW - browseW - gap, rowH, TRUE);
        MoveWindow(g_aBrowse, W - m - browseW, rowY(1) + (rowH - btnH) / 2, browseW, btnH, TRUE);
        placeLabel(g_aLblArgs, 2); MoveWindow(g_aArgs, fieldX, rowY(2), fieldW, rowH, TRUE);
        placeLabel(g_aLblTest, 3); MoveWindow(g_aTest, fieldX, rowY(3), (std::min)(S(240), fieldW), rowH, TRUE);
        placeLabel(g_aLblRuns, 4); MoveWindow(g_aRuns, fieldX, rowY(4), fieldW, rowH, TRUE);
    }
}

static void ShowTab(int tab) {
    g_tab = tab;
    TabCtrl_SetCurSel(g_hTab, tab);
    for (HWND h : g_connControls) ShowWindow(h, tab == TAB_CONN ? SW_SHOW : SW_HIDE);
    for (HWND h : g_actControls) ShowWindow(h, tab == TAB_ACT ? SW_SHOW : SW_HIDE);
    // Counts and checks on one tab depend on the other, so refresh on switch.
    if (tab == TAB_CONN) {
        RefreshConnChoices();
        RefreshRows(TAB_CONN);
        UpdateConnPreview();
    } else {
        RefreshRows(TAB_ACT);
        UpdateActPreview();
    }
    UpdateButtons(tab);
    const std::wstring name = tab == TAB_CONN ? L"connections.json" : L"actionDefinitions.json";
    SetWindowTextW(g_hBtnNotepad, (L"Open " + name + L" in Notepad").c_str());
    DoLayout();
    SetFocus(tab == TAB_CONN ? g_cList : g_aList);
}

// ---------------------------------------------------------------------------
// Control creation
// ---------------------------------------------------------------------------
static HWND MakeChild(DWORD exStyle, const wchar_t* cls, const wchar_t* text, DWORD style, int id,
                      std::vector<HWND>* group) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, 0, 0, 0, 0, g_hWnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (group) group->push_back(h);
    g_allControls.push_back(h);
    return h;
}

static HWND MakeButton(const wchar_t* text, int id, std::vector<HWND>* group) {
    return MakeChild(0, L"BUTTON", text, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, id, group);
}

static HWND MakeLabel(const wchar_t* text, int id, std::vector<HWND>* group) {
    return MakeChild(0, L"STATIC", text, WS_VISIBLE | SS_CENTERIMAGE | SS_NOPREFIX, id, group);
}

static HWND MakeEdit(int id, const wchar_t* cue, std::vector<HWND>* group) {
    HWND h = MakeChild(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, id, group);
    SendMessageW(h, EM_SETLIMITTEXT, 4096, 0);
    if (cue) SendMessageW(h, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(cue));
    return h;
}

static HWND MakeList(int id, std::vector<HWND>* group, const wchar_t* const* cols, const int* widths) {
    HWND lv = MakeChild(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                        WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, id, group);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    for (int c = 0; c < 5; ++c) {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<LPWSTR>(cols[c]);
        col.cx = S(widths[c]);
        col.iSubItem = c;
        ListView_InsertColumn(lv, c, &col);
    }
    return lv;
}

static void CreateControls() {
    g_hTab = MakeChild(0, WC_TABCONTROLW, L"", WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, IDC_TAB, nullptr);
    for (int i = 0; i < 2; ++i) {
        TCITEMW ti = {};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<LPWSTR>(i == 0 ? L"Connections" : L"Actions");
        TabCtrl_InsertItem(g_hTab, i, &ti);
    }

    // Connections tab (created in on-screen order = Tab order)
    std::vector<HWND>* cg = &g_connControls;
    g_cFilterLbl = MakeLabel(L"Filter:", IDC_C_FILTER_LBL, cg);
    g_cFilter = MakeEdit(IDC_C_FILTER, L"show only entries containing this text (Ctrl+F)", cg);
    static const wchar_t* const kConnCols[5] = { L"Host", L"Type", L"Description", L"Group", L"Check" };
    g_cList = MakeList(IDC_C_LIST, cg, kConnCols, Layout::COLS_CONN);
    g_cAdd  = MakeButton(L"Add", IDC_C_ADD, cg);
    g_cDup  = MakeButton(L"Duplicate", IDC_C_DUP, cg);
    g_cDel  = MakeButton(L"Delete", IDC_C_DEL, cg);
    g_cUp   = MakeButton(L"Move Up", IDC_C_UP, cg);
    g_cDown = MakeButton(L"Move Down", IDC_C_DOWN, cg);
    g_cSep  = MakeChild(0, L"STATIC", L"", WS_VISIBLE | SS_ETCHEDHORZ, IDC_C_SEP, cg);
    g_cLblHost = MakeLabel(L"Host:", IDC_C_LBL_HOST, cg);
    g_cHost = MakeEdit(IDC_C_HOST, L"host name, user@host, host:port, a path or a URL", cg);
    g_cLblType = MakeLabel(L"Type:", IDC_C_LBL_TYPE, cg);
    g_cType = MakeChild(0, WC_COMBOBOXW, L"", WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                        IDC_C_TYPE, cg);
    g_cLblDesc = MakeLabel(L"Description:", IDC_C_LBL_DESC, cg);
    g_cDesc = MakeEdit(IDC_C_DESC, L"optional; shown in DDLaunch+'s list", cg);
    g_cLblGroup = MakeLabel(L"Group:", IDC_C_LBL_GROUP, cg);
    g_cGroup = MakeChild(0, WC_COMBOBOXW, L"", WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                         IDC_C_GROUP, cg);
    SendMessageW(g_cGroup, CB_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"optional; organizes the file"));
    g_cLblRuns = MakeLabel(L"Launches:", IDC_C_LBL_RUNS, cg);
    g_cRuns = MakeChild(0, L"STATIC", L"", WS_VISIBLE | SS_CENTERIMAGE | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_C_RUNS, cg);

    // Actions tab
    std::vector<HWND>* ag = &g_actControls;
    static const wchar_t* const kActCols[5] = { L"Action", L"Command", L"Arguments", L"Used by", L"Check" };
    g_aList = MakeList(IDC_A_LIST, ag, kActCols, Layout::COLS_ACT);
    g_aAdd  = MakeButton(L"Add", IDC_A_ADD, ag);
    g_aDup  = MakeButton(L"Duplicate", IDC_A_DUP, ag);
    g_aDel  = MakeButton(L"Delete", IDC_A_DEL, ag);
    g_aUp   = MakeButton(L"Move Up", IDC_A_UP, ag);
    g_aDown = MakeButton(L"Move Down", IDC_A_DOWN, ag);
    g_aSep  = MakeChild(0, L"STATIC", L"", WS_VISIBLE | SS_ETCHEDHORZ, IDC_A_SEP, ag);
    g_aLblName = MakeLabel(L"Action:", IDC_A_LBL_NAME, ag);
    g_aName = MakeEdit(IDC_A_NAME, L"name used as a connection's type, e.g. RDP", ag);
    g_aLblCmd = MakeLabel(L"Command:", IDC_A_LBL_CMD, ag);
    g_aCmd = MakeEdit(IDC_A_CMD, L"program path, or a URL prefix such as https://", ag);
    g_aBrowse = MakeButton(L"Browse...", IDC_A_BROWSE, ag);
    g_aLblArgs = MakeLabel(L"Arguments:", IDC_A_LBL_ARGS, ag);
    g_aArgs = MakeEdit(IDC_A_ARGS, L"text placed directly before the host, e.g. \" /v:\" (not used for URLs)", ag);
    g_aLblTest = MakeLabel(L"Test host:", IDC_A_LBL_TEST, ag);
    g_aTest = MakeEdit(IDC_A_TEST, L"sample host for the line below", ag);
    g_aLblRuns = MakeLabel(L"Launches:", IDC_A_LBL_RUNS, ag);
    g_aRuns = MakeChild(0, L"STATIC", L"", WS_VISIBLE | SS_CENTERIMAGE | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_A_RUNS, ag);

    // Bottom bar. The About link is a SysLink (needs Common Controls v6);
    // without it, a clickable label (SS_NOTIFY -> STN_CLICKED) stands in.
    g_hLinkAbout = MakeChild(0, WC_LINK, L"<a>About</a>", WS_VISIBLE | WS_TABSTOP, IDC_LINK_ABOUT, nullptr);
    if (!g_hLinkAbout) {
        g_allControls.pop_back();  // drop the failed (null) entry
        g_hLinkAbout = MakeChild(0, L"STATIC", L"About", WS_VISIBLE | SS_NOTIFY, IDC_LINK_ABOUT, nullptr);
    }
    g_hStatus = MakeChild(0, L"STATIC", L"", WS_VISIBLE | SS_CENTERIMAGE | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_STATUS, nullptr);
    g_hBtnNotepad = MakeButton(L"Open in Notepad", IDC_BTN_NOTEPAD, nullptr);
    g_hBtnReload  = MakeButton(L"Reload", IDC_BTN_RELOAD, nullptr);
    g_hBtnSave    = MakeButton(L"Save", IDC_BTN_SAVE, nullptr);
    g_hBtnClose   = MakeButton(L"Close", IDC_BTN_CLOSE, nullptr);
}

static void ApplyFont() {
    HFONT f = Dpi::CreateUiFont(g_dpi);
    for (HWND h : g_allControls) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f ? f : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    if (g_hFont) DeleteObject(g_hFont);
    g_hFont = f;
}

static void RescaleForDpi(UINT newDpi) {
    if (!newDpi || newDpi == g_dpi) return;
    const UINT old = g_dpi;
    g_dpi = newDpi;
    ApplyFont();
    for (HWND lv : { g_cList, g_aList }) {
        for (int c = 0; c < 5; ++c) {
            ListView_SetColumnWidth(lv, c, MulDiv(ListView_GetColumnWidth(lv, c), static_cast<int>(g_dpi), static_cast<int>(old)));
        }
    }
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
// About box: version, author and release date.
static void ShowAboutBox() {
    wchar_t jsonVer[32];
    swprintf_s(jsonVer, L"%d.%d.%d", NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR,
               NLOHMANN_JSON_VERSION_PATCH);
    const std::wstring text =
        std::wstring(APP_NAME L" ") + APP_VERSION + L"\n"
        L"Edits connections.json and actionDefinitions.json for DDLaunch+\n\n"
        L"Author:  " APP_AUTHOR L"\n"
        L"Release date:  " APP_RELEASE_DATE L"\n"
        L"Version:  " + std::wstring(APP_VERSION).substr(1) + L"\n\n"
        L"Win32 C++ · nlohmann/json " + jsonVer;

    MSGBOXPARAMSW mbp = {};
    mbp.cbSize = sizeof(mbp);
    mbp.hwndOwner = g_hWnd;
    mbp.hInstance = GetModuleHandleW(nullptr);
    mbp.lpszText = text.c_str();
    mbp.lpszCaption = L"About " APP_NAME;
    mbp.dwStyle = MB_OK | MB_USERICON;
    mbp.lpszIcon = MAKEINTRESOURCEW(IDI_ICON1);  // the application icon
    MessageBoxIndirectW(&mbp);
}

static LRESULT OnListCustomDraw(LPNMLVCUSTOMDRAW cd, int tab) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        const UINT_PTR id = static_cast<UINT_PTR>(cd->nmcd.lItemlParam);
        bool problem = false;
        if (tab == TAB_CONN) {
            const size_t i = FindConnIndex(id);
            problem = i != static_cast<size_t>(-1) && !ConnProblem(g_conns[i]).empty();
        } else {
            const size_t i = FindActIndex(id);
            problem = i != static_cast<size_t>(-1) && !ActProblem(i).empty();
        }
        if (problem) cd->clrText = RGB(176, 0, 32);  // rows with problems in red
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_msgShowTab && g_msgShowTab) {  // from a second DDLaunchEdit.exe
        if (wParam == TAB_CONN || wParam == TAB_ACT) ShowTab(static_cast<int>(wParam));
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        g_hWnd = hWnd;
        g_dpi = Dpi::ForWindow(hWnd);
        CreateControls();
        ApplyFont();
        return 0;

    case WM_SIZE:
        TrackNormalRect();
        DoLayout();
        return 0;

    case WM_MOVE:
        TrackNormalRect();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = S(Layout::MIN_W);
        mmi->ptMinTrackSize.y = S(Layout::MIN_H);
        return 0;
    }

    case WM_DPICHANGED: {
        RescaleForDpi(LOWORD(wParam));
        if (!g_placingWindow) {
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        DoLayout();
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        const HWND ctl = reinterpret_cast<HWND>(lParam);
        switch (id) {
        case IDOK:  // Enter: on a list, jump to the first field of the form
            if (GetFocus() == g_cList && g_selId[TAB_CONN]) FocusAndSelectAll(g_cHost);
            else if (GetFocus() == g_aList && g_selId[TAB_ACT]) FocusAndSelectAll(g_aName);
            return 0;
        case IDCANCEL:  // Esc: ignored, so it can't close the editor by accident
            return 0;
        case IDC_BTN_SAVE:   case ID_ACC_SAVE:   SaveAll(); return 0;
        case IDC_BTN_RELOAD: case ID_ACC_RELOAD: OnReload(); return 0;
        case IDC_BTN_NOTEPAD: OnOpenInNotepad(); return 0;
        case IDC_BTN_CLOSE:  PostMessageW(hWnd, WM_CLOSE, 0, 0); return 0;
        case IDC_C_ADD: case IDC_A_ADD: OnAdd(id == IDC_C_ADD ? TAB_CONN : TAB_ACT); return 0;
        case IDC_C_DUP: case IDC_A_DUP: OnDuplicate(id == IDC_C_DUP ? TAB_CONN : TAB_ACT); return 0;
        case IDC_C_DEL: case IDC_A_DEL: OnDelete(id == IDC_C_DEL ? TAB_CONN : TAB_ACT); return 0;
        case IDC_C_UP:  case IDC_A_UP:  OnMove(id == IDC_C_UP ? TAB_CONN : TAB_ACT, -1); return 0;
        case IDC_C_DOWN: case IDC_A_DOWN: OnMove(id == IDC_C_DOWN ? TAB_CONN : TAB_ACT, +1); return 0;
        case ID_ACC_ADD:  OnAdd(g_tab); return 0;
        case ID_ACC_DUP:  OnDuplicate(g_tab); return 0;
        case ID_ACC_UP:   OnMove(g_tab, -1); return 0;
        case ID_ACC_DOWN: OnMove(g_tab, +1); return 0;
        case ID_ACC_NEXTTAB: case ID_ACC_PREVTAB: ShowTab(g_tab == TAB_CONN ? TAB_ACT : TAB_CONN); return 0;
        case ID_ACC_FIND:
            if (g_tab != TAB_CONN) ShowTab(TAB_CONN);
            FocusAndSelectAll(g_cFilter);
            return 0;
        case IDC_A_BROWSE: OnBrowseCommand(); return 0;
        case IDC_LINK_ABOUT:  // fallback label (no Common Controls v6)
            if (code == STN_CLICKED) ShowAboutBox();
            return 0;

        case IDC_C_FILTER:
            if (code == EN_CHANGE && !g_filling) RebuildList(TAB_CONN, g_selId[TAB_CONN]);
            return 0;
        case IDC_C_HOST: case IDC_C_DESC:
            if (code == EN_CHANGE) OnConnFieldChanged(ctl, false);
            return 0;
        case IDC_C_TYPE: case IDC_C_GROUP:
            if (code == CBN_EDITCHANGE) OnConnFieldChanged(ctl, false);
            else if (code == CBN_SELCHANGE) OnConnFieldChanged(ctl, true);
            return 0;
        case IDC_A_NAME:
            if (code == EN_CHANGE) OnActFieldChanged(ctl);
            else if (code == EN_SETFOCUS) {
                const size_t i = FindActIndex(g_selId[TAB_ACT]);
                g_nameAtFocus = i != static_cast<size_t>(-1) ? g_acts[i].action : std::wstring();
            } else if (code == EN_KILLFOCUS) {
                OnActionNameCommitted();
            }
            return 0;
        case IDC_A_CMD: case IDC_A_ARGS: case IDC_A_TEST:
            if (code == EN_CHANGE) OnActFieldChanged(ctl);
            return 0;
        }
        break;
    }

    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<LPNMHDR>(lParam);
        if (nm->idFrom == IDC_LINK_ABOUT && (nm->code == NM_CLICK || nm->code == NM_RETURN)) {
            ShowAboutBox();
            return 0;
        }
        if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) {
            ShowTab(TabCtrl_GetCurSel(g_hTab));
            return 0;
        }
        if (nm->idFrom == IDC_C_LIST || nm->idFrom == IDC_A_LIST) {
            const int tab = nm->idFrom == IDC_C_LIST ? TAB_CONN : TAB_ACT;
            switch (nm->code) {
            case LVN_ITEMCHANGED: {
                auto* lv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                if (!g_rebuilding && (lv->uChanged & LVIF_STATE) &&
                    ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED)) {
                    OnSelectionChanged(tab);
                }
                return 0;
            }
            case LVN_KEYDOWN:
                if (reinterpret_cast<LPNMLVKEYDOWN>(lParam)->wVKey == VK_DELETE) OnDelete(tab);
                return 0;
            case NM_DBLCLK:
                FocusAndSelectAll(tab == TAB_CONN ? g_cHost : g_aName);
                return 0;
            case NM_CUSTOMDRAW:
                return OnListCustomDraw(reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam), tab);
            }
        }
        break;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && IsChild(hWnd, f)) g_lastFocus = f;
        } else {
            if (g_lastFocus && IsWindow(g_lastFocus) && IsWindowVisible(g_lastFocus)) SetFocus(g_lastFocus);
            else SetFocus(g_tab == TAB_CONN ? g_cList : g_aList);
            PostMessageW(hWnd, WM_APP_CHECKFILES, 0, 0);  // after activation has finished
        }
        return 0;

    case WM_SETFOCUS:
        if (g_lastFocus && IsWindow(g_lastFocus) && IsWindowVisible(g_lastFocus)) SetFocus(g_lastFocus);
        return 0;

    case WM_APP_CHECKFILES:
        if (IsWindowVisible(hWnd)) CheckFilesChangedOnDisk();
        return 0;

    case WM_APP_SHOWERRORS:
        ShowLoadErrors();
        return 0;

    case WM_CLOSE:
        if (GetFocus() == g_aName) OnActionNameCommitted();  // finish a pending rename first
        if (!ConfirmDiscardOrSave(L"closing")) return 0;
        DestroyWindow(hWnd);
        return 0;

    case WM_ENDSESSION:
        if (wParam) SaveEditorSettings();
        return 0;

    case WM_DESTROY:
        SaveEditorSettings();
        if (g_hFont) { DeleteObject(g_hFont); g_hFont = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------
static const wchar_t kWindowClass[] = L"DDLaunchEditClass";
static const wchar_t kInstanceMutex[] = L"Local\\DDLaunchEdit.SingleInstance.5B8F2C41";

// Tab requested on the command line: /connections or /actions (also -), or -1.
static int TabFromCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int tab = -1;
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (!a.empty() && (a[0] == L'/' || a[0] == L'-')) a.erase(0, 1);
        if (EqualsIgnoreCase(a, L"connections") || EqualsIgnoreCase(a, L"c")) tab = TAB_CONN;
        else if (EqualsIgnoreCase(a, L"actions") || EqualsIgnoreCase(a, L"a")) tab = TAB_ACT;
    }
    if (argv) LocalFree(argv);
    return tab;
}

// Returns false if another editor is already running (it has been brought
// forward and told which tab to show).
static bool AcquireSingleInstance(int tab) {
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (!hMutex || GetLastError() != ERROR_ALREADY_EXISTS) return true;  // handle kept until exit
    CloseHandle(hMutex);
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (HWND h = FindWindowW(kWindowClass, nullptr)) {
            if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
            if (tab >= 0) PostMessageW(h, g_msgShowTab, static_cast<WPARAM>(tab), 0);
            return false;
        }
        Sleep(100);
    }
    return false;
}

static HACCEL CreateAccelerators() {
    ACCEL acc[] = {
        { FVIRTKEY | FCONTROL, 'S', ID_ACC_SAVE },
        { FVIRTKEY | FCONTROL, 'N', ID_ACC_ADD },
        { FVIRTKEY | FCONTROL, 'D', ID_ACC_DUP },
        { FVIRTKEY | FCONTROL, 'F', ID_ACC_FIND },
        { FVIRTKEY, VK_F5, ID_ACC_RELOAD },
        { FVIRTKEY | FCONTROL, VK_UP, ID_ACC_UP },
        { FVIRTKEY | FCONTROL, VK_DOWN, ID_ACC_DOWN },
        { FVIRTKEY | FCONTROL, VK_TAB, ID_ACC_NEXTTAB },
        { FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, ID_ACC_PREVTAB },
    };
    return CreateAcceleratorTableW(acc, static_cast<int>(sizeof(acc) / sizeof(acc[0])));
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    g_msgShowTab = RegisterWindowMessageW(L"DDLaunchEdit.ShowTab.5B8F2C41");
    const int requestedTab = TabFromCommandLine();
    if (!AcquireSingleInstance(requestedTab)) return 0;

    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Dpi::EnableDpiAwareness();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    wc.hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClass, APP_NAME, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, Layout::WINDOW_W, Layout::WINDOW_H,
                                nullptr, nullptr, hInst, nullptr);
    if (!hWnd) {
        if (SUCCEEDED(hrCom)) CoUninitialize();
        return 1;
    }

    // Placement: saved position/size (scaled for the monitor) or the default,
    // kept inside the monitor's work area.
    const EditorSettings saved = LoadEditorSettings();
    RECT target;
    HMONITOR hMon;
    if (saved.hasWindow) {
        RECT probe = { saved.x, saved.y, saved.x + saved.w96, saved.y + saved.h96 };
        hMon = MonitorFromRect(&probe, MONITOR_DEFAULTTONEAREST);
        const UINT d = Dpi::ForMonitor(hMon);
        target = { saved.x, saved.y, saved.x + Dpi::Scale(saved.w96, d), saved.y + Dpi::Scale(saved.h96, d) };
    } else {
        RECT cur;
        GetWindowRect(hWnd, &cur);
        hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        const UINT d = Dpi::ForMonitor(hMon);
        target = { cur.left, cur.top, cur.left + Dpi::Scale(Layout::WINDOW_W, d), cur.top + Dpi::Scale(Layout::WINDOW_H, d) };
    }
    ClampToWorkArea(target, hMon);
    g_placingWindow = true;
    SetWindowPos(hWnd, nullptr, target.left, target.top, target.right - target.left, target.bottom - target.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    g_placingWindow = false;
    RescaleForDpi(Dpi::ForWindow(hWnd));
    GetWindowRect(hWnd, &g_normalRect);
    g_normalRectDpi = g_dpi;

    // Data
    SetWindowTextW(g_aTest, saved.testHost.c_str());
    LoadBoth();
    ShowTab(requestedTab >= 0 ? requestedTab : saved.tab);

    int showCmd = nCmdShow;
    if (saved.hasWindow && saved.maximized &&
        (nCmdShow == SW_SHOWNORMAL || nCmdShow == SW_SHOWDEFAULT || nCmdShow == SW_SHOW)) {
        showCmd = SW_SHOWMAXIMIZED;
    }
    ShowWindow(hWnd, showCmd);
    TrackNormalRect();
    UpdateWindow(hWnd);
    PostMessageW(hWnd, WM_APP_SHOWERRORS, 0, 0);

    HACCEL hAccel = CreateAccelerators();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (hAccel && TranslateAcceleratorW(hWnd, hAccel, &msg)) continue;
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (hAccel) DestroyAcceleratorTable(hAccel);
    if (SUCCEEDED(hrCom)) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
