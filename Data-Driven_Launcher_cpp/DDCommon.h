// DDCommon.h -- code shared by DDLaunch+ (DDLaunch.cpp) and the DDLaunch+
// Editor (DDLaunchEdit.cpp).
//
// Before DDLaunch+ 2.7.1 / Editor 1.0.2 these helpers were copied into both
// files, and the Editor's "Launches:" preview had to be kept in step with
// DDLaunch+ by hand (E8). Now both programs include this header, so the
// preview is built by exactly the same code that DDLaunch+ launches with
// (BuildLaunch, below).
//
// Header-only: each program is a single translation unit, so everything is
// declared inline. Include it after the UNICODE defines.
//
// Contents
//   - UTF-8 <-> UTF-16 conversion, case-insensitive comparison, Trim
//   - Files and folders: ReadFileUtf8, FileExists, GetExeDir, GetDataDir,
//     FileStamp (change detection)
//   - Launch rules: URL detection, host quoting, URL encoding, environment
//     variable expansion and BuildLaunch()
//   - DPI helpers and ClampToWorkArea

#ifndef DDLAUNCH_DDCOMMON_H
#define DDLAUNCH_DDCOMMON_H

#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath / FOLDERID_Profile
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

// UTF-8 (what is in the JSON files) -> UTF-16 (what the wide Windows APIs use).
inline std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) throw std::runtime_error("UTF-8 to UTF-16 conversion failed");
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wide[0], n);
    return wide;
}

inline std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) throw std::runtime_error("UTF-16 to UTF-8 conversion failed");
    std::string utf8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &utf8[0], n, nullptr, nullptr);
    return utf8;
}

// Text of a std::exception for a wide message box. Exception messages from
// this code and from nlohmann::json are UTF-8 (they can contain profile
// paths), so they must not go to MessageBoxA. Never throws.
inline std::wstring ExceptionText(const std::exception& e) {
    try {
        return Utf8ToWide(e.what());
    } catch (...) {
        return L"(error text could not be converted)";
    }
}

// Case-insensitive comparison by code point using the operating system's
// invariant upper-case table (CompareStringOrdinal) -- the same rule the file
// system uses -- so results don't depend on the user's locale.
// Returns <0, 0 or >0.
inline int CompareIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                b.c_str(), static_cast<int>(b.size()), TRUE) - CSTR_EQUAL;
}

inline bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return CompareIgnoreCase(a, b) == 0;
}

inline bool LessIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return CompareIgnoreCase(a, b) < 0;
}

// Leading and trailing spaces, tabs and line breaks removed.
inline std::wstring Trim(const std::wstring& s) {
    const size_t first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return std::wstring();
    return s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}

// Full text of a window (edit box, combo box ...).
inline std::wstring GetWindowTextString(HWND hWnd) {
    const int len = GetWindowTextLengthW(hWnd);
    if (len <= 0) return std::wstring();
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(hWnd, &text[0], len + 1);
    text.resize(got > 0 ? static_cast<size_t>(got) : 0);
    return text;
}

// System error text for a Win32 error code, without the trailing CR/LF.
inline std::wstring GetErrorText(DWORD err) {
    LPWSTR buf = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring text = (len && buf) ? std::wstring(buf, len) : std::wstring(L"Unknown error.");
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    return text;
}

// ---------------------------------------------------------------------------
// Files and folders
// ---------------------------------------------------------------------------

// Whole file as bytes (UTF-8 JSON). Read through Win32 so the path can contain
// any characters. Shares read/write/delete, so a read succeeds while an
// editor still has the file open or is replacing it.
inline std::string ReadFileUtf8(const std::wstring& path) {
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
    const BOOL ok = ReadFile(h, buf.empty() ? nullptr : &buf[0], static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != buf.size()) {
        throw std::runtime_error("Failed reading file: " + WideToUtf8(path));
    }
    return buf;
}

inline bool FileExists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool DirectoryExists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Folder containing the running .exe, without a trailing backslash. The
// buffer grows as needed, so installs under paths longer than MAX_PATH work
// (#14); PathRemoveFileSpecW is not used because it is limited to MAX_PATH.
inline std::wstring GetExeDir() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
        if (n == 0) return L".";
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        if (path.size() >= 32768) {  // the longest path Windows supports
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

// %USERPROFILE%, falling back to the shell's Profile known folder if the
// environment variable is missing. L"" if neither can be found.
inline std::wstring GetUserProfileDir() {
    const DWORD needed = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (needed > 0) {
        std::wstring buf(needed, L'\0');
        const DWORD len = GetEnvironmentVariableW(L"USERPROFILE", &buf[0], needed);
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

// %USERPROFILE%\DDLaunch (no trailing backslash), or the .exe folder if the
// profile folder can't be found at all.
inline std::wstring GetDataDir() {
    std::wstring profile = GetUserProfileDir();
    if (profile.empty()) return GetExeDir();
    if (profile.back() == L'\\' || profile.back() == L'/') profile.pop_back();
    return profile + L"\\DDLaunch";
}

// A file's last-write time and size, to notice changes made by other programs.
struct FileStamp {
    bool      exists = false;
    FILETIME  lastWrite{};
    ULONGLONG size = 0;

    bool operator==(const FileStamp& o) const {
        if (exists != o.exists) return false;
        if (!exists) return true;  // both missing
        return size == o.size && CompareFileTime(&lastWrite, &o.lastWrite) == 0;
    }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};

inline FileStamp GetFileStamp(const std::wstring& path) {
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

// ---------------------------------------------------------------------------
// Launch rules
//
// An action's "command" is either a URL prefix (https://, ftp://, ms-settings:
// ...) -- the host is appended and the result opened with its registered
// handler -- or a program, run with "args" + host as its parameters.
// ---------------------------------------------------------------------------

inline bool IsAsciiAlpha(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
inline bool IsAsciiDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// True if `text` starts with an RFC 3986 scheme:
//     scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"
// At least two characters, so a drive letter ("C:\...") is a path, not a URL.
// Only ASCII is accepted, so the check doesn't depend on the locale.
inline bool StartsWithUrlScheme(const std::wstring& text) {
    const size_t colon = text.find(L':');
    if (colon == std::wstring::npos || colon < 2 || !IsAsciiAlpha(text[0])) return false;
    for (size_t i = 1; i < colon; ++i) {
        const wchar_t c = text[i];
        if (!IsAsciiAlpha(c) && !IsAsciiDigit(c) && c != L'+' && c != L'-' && c != L'.') return false;
    }
    return true;
}

// The scheme of `text` (the part before ':'), or L"" if it has none.
inline std::wstring UrlScheme(const std::wstring& text) {
    return StartsWithUrlScheme(text) ? text.substr(0, text.find(L':')) : std::wstring();
}

// True if `host` is already a complete URL rather than something to append to
// `prefix`: it has a scheme AND that scheme is followed by "//"
// ("http://pi01") or is the prefix's own scheme ("mailto:a@b" for "mailto:",
// "Shell:Downloads" for "Shell:"). "dc01:8443" (host:port, which looks like the
// scheme "dc01") is therefore still appended.
inline bool HostIsFullUrl(const std::wstring& host, const std::wstring& prefix) {
    const std::wstring scheme = UrlScheme(host);
    if (scheme.empty()) return false;
    if (host.compare(scheme.size(), 3, L"://") == 0) return true;
    const std::wstring prefixScheme = UrlScheme(prefix);
    return !prefixScheme.empty() && EqualsIgnoreCase(scheme, prefixScheme);
}

// Percent-encode the characters that are never valid anywhere in a URL:
// spaces and other control characters, and " < > ^ ` { | }. Everything else
// ('/', '?', '#', ':', '%', '\', non-ASCII ...) is left alone.
inline std::wstring EncodeUrlUnsafeChars(const std::wstring& text) {
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(text.size());
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

// `arg` unchanged if it has no spaces, tabs or quotes; otherwise quoted with
// the rules CommandLineToArgvW and the C runtime use (backslashes doubled only
// before a quote, an embedded quote becomes \"), so it arrives as one argument.
inline std::wstring QuoteArgIfNeeded(const std::wstring& arg) {
    if (arg.empty() || arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') out.append(backslashes * 2 + 1, L'\\');  // escape preceding \s and the quote
        else           out.append(backslashes, L'\\');          // \s not before a quote are literal
        out += c;
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');  // \s before the closing quote are doubled
    out += L'"';
    return out;
}

// %NAME% environment variables replaced by their values (#2), e.g.
// "%OneDrive%" -> "C:\Users\glenn\OneDrive". Undefined names are left as they
// are, so "%20" in a URL or a literal "%" is not changed.
inline std::wstring ExpandEnvVars(const std::wstring& text) {
    if (text.find(L'%') == std::wstring::npos) return text;
    const DWORD needed = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (needed == 0) return text;
    std::wstring out(needed, L'\0');
    const DWORD got = ExpandEnvironmentStringsW(text.c_str(), &out[0], needed);
    if (got == 0 || got > needed) return text;
    out.resize(got - 1);  // drop the terminating null
    return out;
}

// What DDLaunch+ passes to ShellExecuteExW for an action and a host.
struct LaunchSpec {
    std::wstring file;    // program path, or the URL to open
    std::wstring params;  // command-line parameters (empty for a URL)
    bool         isUrl = false;
};

// The one launch rule, used by DDLaunch+ to launch and by the Editor's
// "Launches:" preview (E3, E5), so the two always agree.
//   - Environment variables in the command, args and host are expanded (#2).
//   - The host is trimmed (#5): " dc1" would otherwise be quoted as " dc1".
//   - URL command: host appended to the prefix, or used as-is if it is
//     already a full URL; unsafe characters are percent-encoded.
//   - Program command: args + host (quoted if needed). If args end with a URL
//     prefix (InPrivate's "... https://", SFTP's " sftp://", SHELL's
//     " Shell:") and the host is already a full URL, that prefix is dropped so
//     the result isn't "https://https://..." (#10).
inline LaunchSpec BuildLaunch(const std::wstring& command, const std::wstring& args,
                              const std::wstring& host) {
    LaunchSpec spec;
    const std::wstring cmd = Trim(ExpandEnvVars(command));
    const std::wstring h   = Trim(ExpandEnvVars(host));

    if (StartsWithUrlScheme(cmd)) {
        spec.isUrl = true;
        spec.file  = EncodeUrlUnsafeChars(HostIsFullUrl(h, cmd) ? h : cmd + h);
        return spec;
    }

    std::wstring a = ExpandEnvVars(args);
    const size_t space = a.find_last_of(L" \t");
    const std::wstring tail = space == std::wstring::npos ? a : a.substr(space + 1);
    const std::wstring tailScheme = UrlScheme(tail);
    if (!tailScheme.empty()) {
        const std::wstring rest = tail.substr(tailScheme.size());
        if ((rest == L":" || rest == L"://") && HostIsFullUrl(h, tail)) {
            a.erase(a.size() - tail.size());
        }
    }
    spec.file   = cmd;
    spec.params = a + QuoteArgIfNeeded(h);
    return spec;
}

// ---------------------------------------------------------------------------
// DPI helpers
//
// Both programs are Per-Monitor V2 DPI aware, so sizes are physical pixels
// scaled from 96-DPI design values. Newer APIs are loaded dynamically so the
// .exe still starts on older Windows, falling back to system DPI there.
// ---------------------------------------------------------------------------
namespace Dpi {

const UINT kBase = USER_DEFAULT_SCREEN_DPI;  // 96

template <typename Fn>
inline Fn LoadUser32(const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), name)));
}

// Call once, before any window is created. The manifest normally sets
// Per-Monitor V2 already; this is a fallback for builds without it.
inline void EnableDpiAwareness() {
    if (IsProcessDPIAware()) return;  // already set by the manifest

    using PFN_SetCtx = BOOL(WINAPI*)(HANDLE);
    if (auto setCtx = LoadUser32<PFN_SetCtx>("SetProcessDpiAwarenessContext")) {  // Windows 10 1703+
        if (setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) return;    // PER_MONITOR_AWARE_V2
    }
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {                             // Windows 8.1+
        using PFN_SetAwareness = HRESULT(WINAPI*)(int);
        auto setAwareness = reinterpret_cast<PFN_SetAwareness>(reinterpret_cast<void*>(
            GetProcAddress(shcore, "SetProcessDpiAwareness")));
        const bool ok = setAwareness && SUCCEEDED(setAwareness(2));                // PROCESS_PER_MONITOR_DPI_AWARE
        FreeLibrary(shcore);
        if (ok) return;
    }
    SetProcessDPIAware();                                                           // Vista+: system DPI
}

inline UINT SystemDpi() {
    HDC hdc = GetDC(nullptr);
    const UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : kBase;
    if (hdc) ReleaseDC(nullptr, hdc);
    return dpi ? dpi : kBase;
}

// DPI of the monitor the window is on (Windows 10 1607+), else system DPI.
inline UINT ForWindow(HWND hWnd) {
    using PFN_GetDpiForWindow = UINT(WINAPI*)(HWND);
    static auto getDpi = LoadUser32<PFN_GetDpiForWindow>("GetDpiForWindow");
    if (getDpi && hWnd) {
        const UINT dpi = getDpi(hWnd);
        if (dpi) return dpi;
    }
    return SystemDpi();
}

// DPI of a monitor (Windows 8.1+ via shcore.dll), else system DPI.
inline UINT ForMonitor(HMONITOR hMon) {
    using PFN_GetDpiForMonitor = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
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

inline int Scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), static_cast<int>(kBase));
}

// The user's message-box font (normally Segoe UI 9pt) at the given DPI.
inline HFONT CreateUiFont(UINT dpi) {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    using PFN_SPIForDpi = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static auto spiForDpi = LoadUser32<PFN_SPIForDpi>("SystemParametersInfoForDpi");
    if (spiForDpi && spiForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) {
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        // Reported at system DPI; rescale to the target DPI.
        ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, static_cast<int>(dpi),
                                            static_cast<int>(SystemDpi()));
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    return nullptr;
}

// Window size needed for a given client size at the given DPI.
inline SIZE WindowSizeForClient(HWND hWnd, int clientW, int clientH, UINT dpi) {
    RECT rc = { 0, 0, clientW, clientH };
    const DWORD style   = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
    using PFN_AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static auto adjustForDpi = LoadUser32<PFN_AdjustForDpi>("AdjustWindowRectExForDpi");
    if (!(adjustForDpi && adjustForDpi(&rc, style, FALSE, exStyle, dpi))) {
        AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    }
    return SIZE{ rc.right - rc.left, rc.bottom - rc.top };
}

}  // namespace Dpi

// Keep a window rectangle inside its monitor's work area (screen minus the
// taskbar): shrink it if it's too big, then shift it fully inside.
inline void ClampToWorkArea(RECT& rc, HMONITOR hMon) {
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!hMon || !GetMonitorInfoW(hMon, &mi)) return;
    const RECT& work = mi.rcWork;
    const LONG w = (std::min)(rc.right - rc.left, work.right - work.left);
    const LONG h = (std::min)(rc.bottom - rc.top, work.bottom - work.top);
    const LONG x = (std::max)(work.left, (std::min)(rc.left, work.right - w));
    const LONG y = (std::max)(work.top, (std::min)(rc.top, work.bottom - h));
    rc = RECT{ x, y, x + w, y + h };
}

#endif  // DDLAUNCH_DDCOMMON_H
