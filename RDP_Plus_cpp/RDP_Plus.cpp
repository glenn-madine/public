// RDP+ version 1.0.9
// Converted from Python to C++ (Win32 API)
// Author: Glenn Madine
// Release_Date: 06/29/2026
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
#define VERSION         L"v1.0.9"
#define IDC_LISTVIEW    1001
#define IDC_BTN_RDP     1002
#define IDC_BTN_EXIT    1003
#define WM_LAUNCH       (WM_USER + 1)

// Column indices
#define COL_HOST    0
#define COL_TYPE    1
#define COL_DESCRIPTION 2

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
// Launch functions
// ---------------------------------------------------------------------------
static void LaunchURL(const std::wstring& prefix, const std::wstring& host) {
    std::wstring url = prefix + host;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void LaunchRDP(const std::wstring& server) {
    std::wstring cmd = L"mstsc /v:" + server;
    // ShellExecute handles mstsc better than CreateProcess on most Windows versions
    ShellExecuteW(nullptr, L"open", L"mstsc.exe",
        (L"/v:" + server).c_str(), nullptr, SW_SHOWNORMAL);
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

static void LaunchSSH(HWND parent, const std::wstring& server, int port = 22) {
    std::wstring username = GetUsernamePopup(parent);
    if (username.empty()) {
        OutputDebugStringW(L"SSH connection cancelled or no username provided.\n");
        return;
    }

    std::wstring sshCmd = L"ssh -p " + std::to_wstring(port)
        + L" " + username + L"@" + server;

    // Launch in Windows Terminal if available, else cmd
    HRESULT hr = ShellExecuteW(nullptr, L"open", L"wt.exe",
        (L"cmd /c \"" + sshCmd + L" & pause\"").c_str(),
        nullptr, SW_SHOWNORMAL) > (HINSTANCE)32 ? S_OK : E_FAIL;

    if (FAILED(hr)) {
        // Fallback: plain cmd window
        ShellExecuteW(nullptr, L"open", L"cmd.exe",
            (L"/c \"" + sshCmd + L" & pause\"").c_str(),
            nullptr, SW_SHOWNORMAL);
    }
}

// ---------------------------------------------------------------------------
// Main Window
// ---------------------------------------------------------------------------
static HWND      g_hListView = nullptr;
static HWND      g_hBtnRDP   = nullptr;
static HWND      g_hBtnExit  = nullptr;
static std::vector<Device> g_devices;

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

static void PopulateListView(HWND hLV) {
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

static void OnItemActivated(HWND hWnd, int index) {
    if (index < 0 || index >= (int)g_devices.size()) return;
    const Device& dev = g_devices[index];
    std::wstring type = ToUpper(dev.type);

    if (type == L"RDP") {
        LaunchRDP(dev.host);
    } else if (type == L"HTTP") {
        LaunchURL(L"http://", dev.host);
    } else if (type == L"HTTPS") {
        LaunchURL(L"https://", dev.host);
    } else if (type == L"SSH") {
        LaunchSSH(hWnd, dev.host);
    }
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

        lvc.iSubItem = 1; lvc.cx = 80; lvc.pszText = (LPWSTR)L"Type";
        lvc.fmt = LVCFMT_CENTER;
        ListView_InsertColumn(g_hListView, 1, &lvc);

        lvc.iSubItem = 2; lvc.cx = 250; lvc.pszText = (LPWSTR)L"Description";
        lvc.fmt = LVCFMT_LEFT;
        ListView_InsertColumn(g_hListView, 2, &lvc);

        // Load data
        std::wstring jsonPath = GetExeDir() + L"\\connections.json";
        g_devices = LoadConnections(jsonPath);
        PopulateListView(g_hListView);

        // Create "Launch RDP" button
        g_hBtnRDP = CreateWindowExW(
            0, L"BUTTON", L"New RDP Connection",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_BTN_RDP, GetModuleHandleW(nullptr), nullptr);

        // Match the ListView font
        SendMessageW(g_hBtnRDP, WM_SETFONT,
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

        const int BTN_H   = 28;
        const int BTN_W   = 110;
        const int GAP     = 10;
        const int MARGIN  = 6;

        // ListView fills everything above the button row
        MoveWindow(g_hListView,
            0, 0,
            rc.right, rc.bottom - BTN_H - MARGIN * 2,
            TRUE);

        // Two buttons centred together at the bottom
        int totalW = BTN_W * 2 + GAP;
        int startX = (rc.right - totalW) / 2;
        int btnY   = rc.bottom - BTN_H - MARGIN;

        MoveWindow(g_hBtnRDP,  startX,           btnY, BTN_W, BTN_H, TRUE);
        MoveWindow(g_hBtnExit, startX + BTN_W + GAP, btnY, BTN_W, BTN_H, TRUE);

        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_BTN_RDP) {
            ShellExecuteW(nullptr, L"open", L"mstsc.exe",
                nullptr, nullptr, SW_SHOWNORMAL);
        } else if (LOWORD(wParam) == IDC_BTN_EXIT) {
            DestroyWindow(hWnd);
        }
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);
        if (pnmh->idFrom == IDC_LISTVIEW) {
            if (pnmh->code == NM_DBLCLK || pnmh->code == NM_RETURN) {
                int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
                if (sel >= 0) OnItemActivated(hWnd, sel);
            } else if (pnmh->code == LVN_COLUMNCLICK) {
                LPNMLISTVIEW pnmlv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                SortDevices(pnmlv->iSubItem);
                PopulateListView(g_hListView);
                UpdateHeaderSortArrow(g_hListView, g_sortCol, g_sortAsc);
            }
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
 
    std::wstring windowTitle = std::wstring(L"RDP+ ") + VERSION + L" - Select a connection";

    HWND hWnd = CreateWindowExW(
        0, L"RDPPlusClass", windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 340,
        nullptr, nullptr, hInst, nullptr);

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