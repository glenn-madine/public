// RDP+ version 1.6.1
// C++ (Win32 API)
// Author: Glenn Madine
// Release_Date: 09/15/2026
// Requires: Windows SDK, nlohmann/json (single-header, included as json.hpp)
// Compiled using Microsoft C++ 19.51
// Compile and link command line:
//     CL /EHsc /W3 /O2 /GL /DUNICODE /D_UNICODE /DNDEBUG /std:c++17 RDP_Plus.cpp RDP_Plus.res /Fe:RDP_Plus.exe /link /SUBSYSTEM:WINDOWS comctl32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
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

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define VERSION         		L"v1.6.1"
#define IDC_LISTVIEW        	1001
#define IDC_BTN_RDP         	1002
#define IDC_BTN_EXIT        	1003
#define IDC_BTN_EDIT_CONN   	1004
#define IDC_BTN_EDIT_ACTIONS	1005
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

// --- 4. Search / lookup: get the command for a given action name ----------
std::optional<std::wstring> findCommandByAction(const std::vector<Action>& actions,
                                                 const std::wstring& actionName) {
    auto it = std::find_if(actions.begin(), actions.end(),
                            [&actionName](const Action& a) { return a.action == actionName; });

    if (it == actions.end()) {
        return std::nullopt;
    }
    return it->command;
}

// --- 5. Search / lookup: get the args for a given action name ---------- 
std::optional<std::wstring> findArgsByAction(const std::vector<Action>& actions,
                                                 const std::wstring& actionName) {
    auto it = std::find_if(actions.begin(), actions.end(),
                            [&actionName](const Action& a) { return a.action == actionName; });

    if (it == actions.end()) {
        return std::nullopt;
    }
    return it->args;
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
static std::vector<Device> LoadConnections(const std::wstring& jsonPath) {
    std::vector<Device> devices;

    // Convert wide path to narrow for ifstream
    int sz = WideCharToMultiByte(CP_UTF8, 0, jsonPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrowPath(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, jsonPath.c_str(), -1, &narrowPath[0], sz, nullptr, nullptr);
    if (!narrowPath.empty() && narrowPath.back() == '\0') narrowPath.pop_back();

    std::ifstream file(narrowPath);
    if (!file.is_open()) {
        MessageBoxW(nullptr,
            (L"Error: Could not open " + jsonPath).c_str(),
            L"RDP+", MB_ICONERROR | MB_OK);
        return devices;
    }

    try {
        nlohmann::json data;
        file >> data;

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
        MessageBoxA(nullptr, e.what(), "RDP+ JSON Error", MB_ICONERROR | MB_OK);
    }

    return devices;
}

// ---------------------------------------------------------------------------
// Config file paths (both configs live next to the .exe)
// ---------------------------------------------------------------------------
static std::wstring GetConnectionsJsonPath() {
    return GetExeDir() + L"\\connections.json";
}

static std::wstring GetActionsJsonPath() {
    return GetExeDir() + L"\\actionDefinitions.json";
}

// ---------------------------------------------------------------------------
// Open a config file for editing in the user's default text editor.
//
// Launches Notepad directly (rather than ShellExecute's "open" verb) so this
// always works even if .json has no file association or is associated with
// something other than a text editor. If the file doesn't exist yet, it is
// created with harmless placeholder content first so Notepad opens valid
// JSON instead of a blank file that would fail to parse on next load.
// ---------------------------------------------------------------------------
static bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static void CreateFileWithDefaultContent(const std::wstring& path, const std::string& utf8Content) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return; // Someone else created it first, or the directory isn't writable -- either way, let Notepad deal with it.
    }
    DWORD written = 0;
    WriteFile(hFile, utf8Content.data(), static_cast<DWORD>(utf8Content.size()), &written, nullptr);
    CloseHandle(hFile);
}

static void OpenJsonFileForEditing(HWND hWnd, const std::wstring& path, const std::string& defaultContentIfMissing) {
    if (!FileExists(path)) {
        CreateFileWithDefaultContent(path, defaultContentIfMissing);
    }

    // Quote the path in case the exe lives under a directory with spaces.
    std::wstring quotedPath = L"\"" + path + L"\"";
    HINSTANCE result = ShellExecuteW(hWnd, L"open", L"notepad.exe", quotedPath.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hWnd, (L"Could not open Notepad for:\n" + path).c_str(),
                    L"RDP+", MB_ICONERROR | MB_OK);
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

static void LaunchSomething(const std::wstring prefix, const std::wstring args,const std::wstring host) {
    std::wstring url = prefix + host;
    if ((prefix.substr(0, 8) == L"https://") || (prefix.substr(0, 7) == L"http://") || (prefix.substr(0, 6) == L"ftp://") || (prefix.substr(0, 8) == L"file://") || (prefix.substr(0, 7) == L"mailto:") || (prefix.substr(0, 4) == L"tel:")) {     
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {  
        ShellExecuteW(nullptr, L"open", prefix.c_str(), (args+host).c_str(), nullptr, SW_SHOWNORMAL);  
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
// Main Window
// ---------------------------------------------------------------------------
static HWND      g_hListView       = nullptr;
static HWND      g_hBtnRDP         = nullptr;
static HWND      g_hBtnExit        = nullptr;
static HWND      g_hBtnEditConn    = nullptr;
static HWND      g_hBtnEditActions = nullptr;
static std::vector<Device> g_devices;
static std::vector<Action> g_actions;

// ---------------------------------------------------------------------------
// Sort state
// ---------------------------------------------------------------------------
static int  g_sortCol = -1;      // last sorted column (-1 = none)
static bool g_sortAsc = true;    // true = ascending, false = descending

// Sort g_devices by column, toggling direction when the same column is clicked.
static void SortDevices(int col) {
    if (g_sortCol == col) {
        g_sortAsc = !g_sortAsc;  // same column: flip direction
    } else {
        g_sortCol = col;
        g_sortAsc = true;        // new column: always start ascending
    }

    bool ascending = g_sortAsc;
    std::sort(g_devices.begin(), g_devices.end(),
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

// Reload connections.json and actionDefinitions.json from disk and refresh
// the list view. Called at startup and whenever the window regains focus,
// so that changes made via the "Edit connections" / "Edit actions" buttons
// (in Notepad, running as a separate process) show up without a restart.
static void ReloadAllData(HWND hWnd) {
    g_devices = LoadConnections(GetConnectionsJsonPath());

    try {
        g_actions = loadActions(GetActionsJsonPath());
    } catch (const std::exception& e) {
        // actionDefinitions.json is missing or not valid JSON (e.g. mid-edit).
        // Keep whatever actions we already had rather than crashing the app --
        // loadActions() throws, and WndProc previously called it unguarded on
        // every single message, so any parse failure used to take the whole
        // window down immediately.
        MessageBoxA(hWnd, e.what(), "RDP+ - Could not load actionDefinitions.json", MB_ICONWARNING | MB_OK);
    }

    if (g_hListView) {
        PopulateListView(g_hListView, g_actions);
    }
}

static void OnItemActivated(HWND hWnd, int index, const std::vector<Action>& actions) {
    if (index < 0 || index >= (int)g_devices.size()) return;
    const Device& dev = g_devices[index];
    std::wstring type = ToUpper(dev.type);
	
    // std::optional<std::wstring> 
	std::wstring cmd = (std::wstring)findCommandByAction(actions, type).value_or(L"Default");
	std::wstring args = (std::wstring)findArgsByAction(actions, type).value_or(L"Default");
	LaunchSomething(cmd, args, dev.host); 
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Create ListView
        g_hListView = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_LISTVIEW, GetModuleHandleW(nullptr), nullptr);

        ListView_SetExtendedListViewStyle(g_hListView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        // Add columns
        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
        lvc.fmt  = LVCFMT_LEFT;

        lvc.iSubItem = 0; lvc.cx = 200; lvc.pszText = (LPWSTR)L"Host";
        ListView_InsertColumn(g_hListView, 0, &lvc);

        lvc.iSubItem = 1; lvc.cx = 100; lvc.pszText = (LPWSTR)L"Type";
        lvc.fmt = LVCFMT_CENTER;
        ListView_InsertColumn(g_hListView, 1, &lvc);

        lvc.iSubItem = 2; lvc.cx = 300; lvc.pszText = (LPWSTR)L"Description";
        lvc.fmt = LVCFMT_LEFT;
        ListView_InsertColumn(g_hListView, 2, &lvc);

        // Load data (connections.json + actionDefinitions.json)
        ReloadAllData(hWnd);

        // Create "Launch RDP" button
        g_hBtnRDP = CreateWindowExW(
            0, L"BUTTON", L"Launch RDP connection for host not in this list",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_BTN_RDP, GetModuleHandleW(nullptr), nullptr);

        // Match the ListView font
        SendMessageW(g_hBtnRDP, WM_SETFONT,
            (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

        // Create "Edit connections.json" button
        g_hBtnEditConn = CreateWindowExW(
            0, L"BUTTON", L"Edit connections.json",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_BTN_EDIT_CONN, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(g_hBtnEditConn, WM_SETFONT,
            (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

        // Create "Edit actionDefinitions.json" button
        g_hBtnEditActions = CreateWindowExW(
            0, L"BUTTON", L"Edit actionDefinitions.json",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_BTN_EDIT_ACTIONS, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(g_hBtnEditActions, WM_SETFONT,
            (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

        // Create "Exit" button
        g_hBtnExit = CreateWindowExW(
            0, L"BUTTON", L"Exit",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_BTN_EXIT, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(g_hBtnExit, WM_SETFONT,
            (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);


        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hWnd, &rc);

        const int BTN_H   = 24;
        const int BTN_W   = 250;
        const int GAP     = 10;
        const int MARGIN  = 6;
        const int ROW_GAP = 6;

        // Two rows of two buttons now occupy the bottom of the window.
        const int BUTTON_AREA_H = BTN_H * 2 + ROW_GAP + MARGIN * 2;

        // ListView fills everything above the button rows
        MoveWindow(g_hListView,
            0, 0,
            rc.right, rc.bottom - BUTTON_AREA_H,
            TRUE);

        int totalW = BTN_W * 2 + GAP;
        int startX = (rc.right - totalW) / 2;

        // Bottom row: Launch RDP / Exit (unchanged pair, moved up one row)
        int row2Y = rc.bottom - BTN_H - MARGIN;
        int row1Y = row2Y - ROW_GAP - BTN_H;

        MoveWindow(g_hBtnRDP,  startX,               row1Y, BTN_W, BTN_H, TRUE);
        MoveWindow(g_hBtnEditActions, startX + BTN_W + GAP, row1Y, BTN_W, BTN_H, TRUE);

        // New row: Edit connections.json / Edit actionDefinitions.json
        MoveWindow(g_hBtnEditConn,    startX,               row2Y, BTN_W, BTN_H, TRUE);
        MoveWindow(g_hBtnExit, startX + BTN_W + GAP, row2Y, BTN_W, BTN_H, TRUE);

        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_BTN_RDP) {
            ShellExecuteW(nullptr, L"open", L"mstsc.exe",
                nullptr, nullptr, SW_SHOWNORMAL);
        } else if (LOWORD(wParam) == IDC_BTN_EXIT) {
            DestroyWindow(hWnd);
        } else if (LOWORD(wParam) == IDC_BTN_EDIT_CONN) {
            // connections.json is either a JSON array of device objects, or an
            // object of named groups -- an empty array is valid either way.
            OpenJsonFileForEditing(hWnd, GetConnectionsJsonPath(), "[]");
        } else if (LOWORD(wParam) == IDC_BTN_EDIT_ACTIONS) {
            // actionDefinitions.json must be a top-level JSON array (see loadActions()).
            OpenJsonFileForEditing(hWnd, GetActionsJsonPath(), "[]");
        }
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);
        if (pnmh->idFrom == IDC_LISTVIEW) {
            if (pnmh->code == NM_DBLCLK || pnmh->code == NM_RETURN) {
                int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
                if (sel >= 0) OnItemActivated(hWnd, sel, g_actions);
            } else if (pnmh->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                SortDevices(pnmlv->iSubItem);
                PopulateListView(g_hListView, g_actions);
                UpdateHeaderSortArrow(g_hListView, g_sortCol, g_sortAsc);
            }
        }
        return 0;
    }

    case WM_ACTIVATE: {
        // Reload connections.json / actionDefinitions.json whenever the main
        // window regains focus, so edits made in Notepad (opened via the
        // "Edit connections.json" / "Edit actionDefinitions.json" buttons)
        // are picked up as soon as the user switches back, with no restart.
        if (LOWORD(wParam) != WA_INACTIVE && g_hListView) {
            ReloadAllData(hWnd);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Enable visual styles / Common Controls v6
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    // Register window class
    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName  = L"RDPPlusClass";
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = wc.hIcon; 
    RegisterClassExW(&wc);
 
    std::wstring windowTitle = std::wstring(L"RDP+ ") + VERSION + L" - To launch, doubleclick a host below";

    HWND hWnd = CreateWindowExW(
        0, 
		L"RDPPlusClass", 
		windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 
		CW_USEDEFAULT, 
		600, 
		400,
        nullptr, 
		nullptr, 
		hInst, 
		nullptr);

    if (!hWnd) return 1;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}