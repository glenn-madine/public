// ClippiEditor.cpp  (Clippi Editor) - version from APP_VERSION_* in resource.h
//
// A friendly editor for Clippi's button file:
//   %USERPROFILE%\Clippi_Storage\ClipBoardButtonDefinitions.json
// (or the file given on the command line; Clippi's "Edit Clips" passes it).
//
// Left:   the list of buttons - Add, Duplicate, Delete, Move Up, Move Down.
// Right:  the selected button's label, group, prompt titles and text to paste.
//         Group: type a new name or pick one already used. Clippi shows one
//         group at a time; buttons without a group go into "General". Groups
//         appear in Clippi in the order they first occur in this list.
//         Two optional prompts: Clippi asks for "Prompt title" first and puts
//         the answer where %XXX% is, then "Second prompt title" for %YYY%.
//         "Insert %XXX%" / "Insert %YYY%" add a placeholder at the cursor.
//
// Text to paste (WYSIWYG):
//   A rich-text box that shows the clip the way it will paste. Type plain
//   text, or format it with the toolbar (Bold, Italic, Underline, Strikeout,
//   Font, Color, Link, Picture, Clear format) or Ctrl+B / Ctrl+I / Ctrl+U.
//   Ctrl+L / Ctrl+E / Ctrl+R / Ctrl+J align the paragraph.
//   Pasting keeps the formatting: RTF (Word, Outlook) is pasted as it is,
//   HTML (Teams, browsers, web mail) is converted, and a copied picture
//   (e.g. a screenshot) is added as PNG. Ctrl+Shift+V pastes plain text only.
//   "Insert from HTML/RTF file" adds a file's contents at the cursor;
//   "Export RTF" / "Export HTML" save the current clip's formatted version.
//
// Saving (Save button or Ctrl+S) writes UTF-8 JSON, keeps any fields Clippi
// doesn't use, and first copies the previous file to *.bak.
//   - A clip without formatting is stored as plain text, as before (line
//     breaks as \\n; "rawText": true if the text itself contains \n or \t).
//   - A formatted clip is stored as all three versions Clippi uses:
//       plain text  for Notepad and other programs (links as "text <url>")
//       %RTF:...%   for Outlook and Word (the box's own RTF)
//       %HTML:...%  for Teams and browsers (generated from the same content)
//   - A clip whose text wasn't changed is saved exactly as it was loaded.
//     Clips from older versions (%IMG% pictures, or %HTML% only) are shown
//     formatted and converted when their text is edited.
// Closing with unsaved changes asks whether to save.
//
// Pure Win32 / C++17. Uses Rich Edit (Msftedit.dll) for the text box and WIC
// (Windows Imaging Component) for pictures.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr wchar_t kJsonFileName[] = L"ClipBoardButtonDefinitions.json";
constexpr wchar_t kWndClass[]     = L"ClippiEditorWnd";
constexpr wchar_t kAppName[]      = L"Clippi Editor";
constexpr wchar_t kAppTitle[]     = L"Clippi Editor " APP_VERSION_WSTR;

enum : int {
    IDC_LIST_LABEL = 100, IDC_LIST, IDC_ADD, IDC_DUP, IDC_DEL, IDC_UP, IDC_DOWN,
    IDC_LABEL_LABEL, IDC_LABEL, IDC_GROUP_LABEL, IDC_GROUP, IDC_PROMPT_LABEL, IDC_PROMPT,
    IDC_PROMPT2_LABEL, IDC_PROMPT2,
    IDC_TEXT_LABEL, IDC_INSERT_XXX, IDC_INSERT_YYY,
    IDC_FMT_BOLD, IDC_FMT_ITALIC, IDC_FMT_UNDERLINE, IDC_FMT_STRIKE,
    IDC_FMT_FONT, IDC_FMT_COLOR, IDC_FMT_LINK, IDC_FMT_PICTURE, IDC_FMT_CLEAR,
    IDC_TEXT, IDC_INSERT_FILE, IDC_EXPORT_RTF, IDC_EXPORT_HTML,
    IDC_HINT, IDC_STATUS, IDC_SAVE, IDC_CLOSE,
    IDC_ASK_LABEL = 300, IDC_ASK_EDIT,
};

// ============================================================================
// JSON: parser (same rules as Clippi) and writer
// ============================================================================
struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0.0;
    std::wstring str;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::wstring, JsonValue>> object;

    const JsonValue* Find(const wchar_t* key) const {
        for (const auto& kv : object)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

struct JsonError { std::wstring message; };

class JsonParser {
public:
    explicit JsonParser(const std::wstring& text) : s_(text) {}

    JsonValue Parse() {
        JsonValue v = ParseValue();
        if (Peek() != 0) Fail(L"Unexpected data after end of JSON");
        return v;
    }

private:
    const std::wstring& s_;
    size_t p_ = 0;

    [[noreturn]] void Fail(const std::wstring& what) const {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < p_ && i < s_.size(); ++i) {
            if (s_[i] == L'\n') { ++line; col = 1; } else { ++col; }
        }
        throw JsonError{ what + L" (line " + std::to_wstring(line) +
                         L", column " + std::to_wstring(col) + L")" };
    }

    void SkipWs() {
        while (p_ < s_.size() &&
               (s_[p_] == L' ' || s_[p_] == L'\t' || s_[p_] == L'\r' || s_[p_] == L'\n'))
            ++p_;
    }

    wchar_t Peek() { SkipWs(); return p_ < s_.size() ? s_[p_] : 0; }

    void Expect(wchar_t c) {
        if (Peek() != c) Fail(std::wstring(L"Expected '") + c + L"'");
        ++p_;
    }

    bool Match(const wchar_t* lit) {
        const size_t n = wcslen(lit);
        if (s_.compare(p_, n, lit) == 0) { p_ += n; return true; }
        return false;
    }

    JsonValue ParseValue() {
        JsonValue v;
        switch (Peek()) {
        case L'{': v.type = JsonValue::Object; ParseObject(v); break;
        case L'[': v.type = JsonValue::Array;  ParseArray(v);  break;
        case L'"': v.type = JsonValue::String; v.str = ParseString(); break;
        case L't':
            if (!Match(L"true")) Fail(L"Invalid literal");
            v.type = JsonValue::Bool; v.boolean = true; break;
        case L'f':
            if (!Match(L"false")) Fail(L"Invalid literal");
            v.type = JsonValue::Bool; break;
        case L'n':
            if (!Match(L"null")) Fail(L"Invalid literal");
            break;
        case 0:
            Fail(L"Unexpected end of file");
        default:
            ParseNumber(v); break;
        }
        return v;
    }

    void ParseObject(JsonValue& v) {
        ++p_; // '{'
        if (Peek() == L'}') { ++p_; return; }
        for (;;) {
            if (Peek() != L'"') Fail(L"Expected property name in quotes");
            std::wstring key = ParseString();
            Expect(L':');
            JsonValue val = ParseValue();
            v.object.emplace_back(std::move(key), std::move(val));
            const wchar_t c = Peek();
            if (c == L',') { ++p_; continue; }
            if (c == L'}') { ++p_; return; }
            Fail(L"Expected ',' or '}'");
        }
    }

    void ParseArray(JsonValue& v) {
        ++p_; // '['
        if (Peek() == L']') { ++p_; return; }
        for (;;) {
            v.array.push_back(ParseValue());
            const wchar_t c = Peek();
            if (c == L',') { ++p_; continue; }
            if (c == L']') { ++p_; return; }
            Fail(L"Expected ',' or ']'");
        }
    }

    std::wstring ParseString() {
        ++p_; // opening quote
        std::wstring out;
        while (p_ < s_.size()) {
            const wchar_t c = s_[p_++];
            if (c == L'"') return out;
            if (c != L'\\') { out += c; continue; }
            if (p_ >= s_.size()) break;
            const wchar_t e = s_[p_++];
            switch (e) {
            case L'"':  out += L'"';  break;
            case L'\\': out += L'\\'; break;
            case L'/':  out += L'/';  break;
            case L'b':  out += L'\b'; break;
            case L'f':  out += L'\f'; break;
            case L'n':  out += L'\n'; break;
            case L'r':  out += L'\r'; break;
            case L't':  out += L'\t'; break;
            case L'u': {
                if (p_ + 4 > s_.size()) Fail(L"Bad \\u escape");
                unsigned cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const wchar_t h = s_[p_++];
                    cp <<= 4;
                    if (h >= L'0' && h <= L'9')      cp |= h - L'0';
                    else if (h >= L'a' && h <= L'f') cp |= h - L'a' + 10;
                    else if (h >= L'A' && h <= L'F') cp |= h - L'A' + 10;
                    else Fail(L"Bad \\u escape");
                }
                out += static_cast<wchar_t>(cp);
                break;
            }
            default:
                Fail(L"Invalid escape sequence");
            }
        }
        Fail(L"Unterminated string");
    }

    void ParseNumber(JsonValue& v) {
        const wchar_t* start = s_.c_str() + p_;
        wchar_t* end = nullptr;
        const double d = wcstod(start, &end);
        if (end == start) Fail(L"Unexpected character");
        p_ += static_cast<size_t>(end - start);
        v.type = JsonValue::Number;
        v.number = d;
    }
};

std::wstring Widen(const char* s) { return std::wstring(s, s + strlen(s)); }

void JsonWriteString(std::wstring& out, const std::wstring& s) {
    out += L'"';
    for (wchar_t c : s) {
        switch (c) {
        case L'"':  out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\b': out += L"\\b";  break;
        case L'\f': out += L"\\f";  break;
        case L'\n': out += L"\\n";  break;
        case L'\r': out += L"\\r";  break;
        case L'\t': out += L"\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += Widen(buf);
            } else {
                out += c;
            }
        }
    }
    out += L'"';
}

// Pretty-printed JSON, 4-space indent
void JsonWrite(std::wstring& out, const JsonValue& v, int depth) {
    const std::wstring pad(static_cast<size_t>(depth + 1) * 4, L' ');
    const std::wstring padEnd(static_cast<size_t>(depth) * 4, L' ');
    switch (v.type) {
    case JsonValue::Null:   out += L"null"; break;
    case JsonValue::Bool:   out += v.boolean ? L"true" : L"false"; break;
    case JsonValue::Number: {
        char buf[64];
        if (std::floor(v.number) == v.number && std::fabs(v.number) < 1e15)
            snprintf(buf, sizeof(buf), "%.0f", v.number);
        else
            snprintf(buf, sizeof(buf), "%.17g", v.number);
        out += Widen(buf);
        break;
    }
    case JsonValue::String: JsonWriteString(out, v.str); break;
    case JsonValue::Array:
        if (v.array.empty()) { out += L"[]"; break; }
        out += L"[\n";
        for (size_t i = 0; i < v.array.size(); ++i) {
            out += pad;
            JsonWrite(out, v.array[i], depth + 1);
            out += (i + 1 < v.array.size()) ? L",\n" : L"\n";
        }
        out += padEnd + L"]";
        break;
    case JsonValue::Object:
        if (v.object.empty()) { out += L"{}"; break; }
        out += L"{\n";
        for (size_t i = 0; i < v.object.size(); ++i) {
            out += pad;
            JsonWriteString(out, v.object[i].first);
            out += L": ";
            JsonWrite(out, v.object[i].second, depth + 1);
            out += (i + 1 < v.object.size()) ? L",\n" : L"\n";
        }
        out += padEnd + L"}";
        break;
    }
}

JsonValue JsonString(const std::wstring& s) {
    JsonValue v;
    v.type = JsonValue::String;
    v.str = s;
    return v;
}

// ============================================================================
// Text, file and encoding helpers
// ============================================================================
void ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::wstring::npos; pos += to.size())
        s.replace(pos, from.size(), to);
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromBytes(const char* src, size_t size) {
    if (size == 0) return std::wstring();
    UINT codePage = CP_UTF8;
    int len = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, src, static_cast<int>(size), nullptr, 0);
    if (len == 0) { codePage = CP_ACP; len = MultiByteToWideChar(codePage, 0, src, static_cast<int>(size), nullptr, 0); }
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(codePage, 0, src, static_cast<int>(size), &out[0], len);
    return out;
}

bool FileExists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out, std::wstring& error) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { error = L"Cannot open file:\n" + path; return false; }
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > 32 * 1024 * 1024) {
        CloseHandle(h);
        error = L"File is too large:\n" + path;
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = out.empty() ||
                    ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) { error = L"Cannot read file:\n" + path; return false; }
    out.resize(read);
    return true;
}

bool WriteFileBytes(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
    CloseHandle(h);
    return ok;
}

// Text file as UTF-8 (BOM optional) or UTF-16LE (with BOM); falls back to ANSI.
std::wstring BytesToText(const std::vector<unsigned char>& b) {
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE)
        return std::wstring(reinterpret_cast<const wchar_t*>(b.data() + 2), (b.size() - 2) / 2);
    size_t off = (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
    return FromBytes(reinterpret_cast<const char*>(b.data()) + off, b.size() - off);
}

std::wstring Base64Encode(const std::vector<unsigned char>& data) {
    static const wchar_t kChars[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kChars[(v >> 18) & 63]; out += kChars[(v >> 12) & 63];
        out += kChars[(v >> 6) & 63];  out += kChars[v & 63];
    }
    if (i + 1 == data.size()) {
        const unsigned v = data[i] << 16;
        out += kChars[(v >> 18) & 63]; out += kChars[(v >> 12) & 63]; out += L"==";
    } else if (i + 2 == data.size()) {
        const unsigned v = (data[i] << 16) | (data[i + 1] << 8);
        out += kChars[(v >> 18) & 63]; out += kChars[(v >> 12) & 63];
        out += kChars[(v >> 6) & 63];  out += L'=';
    }
    return out;
}

std::vector<unsigned char> Base64Decode(const std::wstring& s) {
    static const char kChars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> out;
    unsigned buffer = 0;
    int bits = 0;
    for (wchar_t c : s) {
        if (c == L'=') break;
        const char* hit = (c > 0 && c < 128) ? strchr(kChars, static_cast<char>(c)) : nullptr;
        if (!hit) continue;
        buffer = (buffer << 6) | static_cast<unsigned>(hit - kChars);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

bool IsPng(const std::vector<unsigned char>& d) {
    static const unsigned char kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    return d.size() >= 8 && memcmp(d.data(), kSig, 8) == 0;
}

bool PngSize(const std::vector<unsigned char>& png, int& w, int& h) {
    if (png.size() < 24 || !IsPng(png) || memcmp(&png[12], "IHDR", 4) != 0) return false;
    auto be32 = [&](size_t i) {
        return static_cast<int>((png[i] << 24) | (png[i + 1] << 16) | (png[i + 2] << 8) | png[i + 3]);
    };
    w = be32(16);
    h = be32(20);
    return w > 0 && h > 0;
}

std::wstring FormatSize(size_t bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + L" bytes";
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    return Widen(buf);
}

std::wstring DefaultJsonPath() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(L"%USERPROFILE%\\Clippi_Storage\\", buf, ARRAYSIZE(buf));
    if (n == 0 || n > ARRAYSIZE(buf) || wcschr(buf, L'%')) return kJsonFileName;
    return std::wstring(buf) + kJsonFileName;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}


std::wstring ToLowerW(std::wstring s) {
    for (wchar_t& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    return s;
}

std::string ToLowerA(std::string s) {
    for (char& ch : s) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool IEquals(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

bool StartsWithI(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

int HexVal(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// CRLF line breaks (the form edit controls and the plain-text clip use)
std::wstring LfToCrlf(std::wstring s) {
    s.erase(std::remove(s.begin(), s.end(), L'\r'), s.end());
    ReplaceAll(s, L"\n", L"\r\n");
    return s;
}

// Reduces a whole HTML page to the part that should be pasted: the clipboard
// fragment if it has one, otherwise the contents of <body>.
std::string ExtractHtmlFragment(const std::string& html) {
    const size_t fs = html.find("<!--StartFragment-->");
    const size_t fe = html.find("<!--EndFragment-->");
    if (fs != std::string::npos && fe != std::string::npos && fe > fs)
        return html.substr(fs + 20, fe - fs - 20);
    const std::string lower = ToLowerA(html);
    const size_t b = lower.find("<body");
    if (b == std::string::npos) return html;
    const size_t open = lower.find('>', b);
    const size_t close = lower.rfind("</body>");
    if (open == std::string::npos) return html;
    return html.substr(open + 1, (close == std::string::npos || close < open ? html.size() : close) - open - 1);
}

// The fragment of clipboard "HTML Format" data (UTF-8), using the byte offsets
// in its header when they are valid.
std::string CfHtmlFragment(const std::string& cf) {
    auto number = [&](const char* key) -> long long {
        const size_t at = cf.find(key);
        return at == std::string::npos ? -1 : atoll(cf.c_str() + at + strlen(key));
    };
    const long long s = number("StartFragment:"), e = number("EndFragment:");
    if (s >= 0 && e > s && e <= static_cast<long long>(cf.size()))
        return cf.substr(static_cast<size_t>(s), static_cast<size_t>(e - s));
    return ExtractHtmlFragment(cf);
}

// ============================================================================
// Pictures (WIC and GDI)
// ============================================================================
template <typename T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool JpegSize(const std::vector<unsigned char>& d, int& w, int& h) {
    if (d.size() < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < d.size()) {
        if (d[i] != 0xFF) { ++i; continue; }
        const unsigned char m = d[i + 1];
        if (m == 0xFF) { ++i; continue; }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD8)) { i += 2; continue; }
        const size_t len = (static_cast<size_t>(d[i + 2]) << 8) | d[i + 3];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            h = (d[i + 5] << 8) | d[i + 6];
            w = (d[i + 7] << 8) | d[i + 8];
            return w > 0 && h > 0;
        }
        i += 2 + len;
    }
    return false;
}

IWICImagingFactory* CreateWic() {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, reinterpret_cast<void**>(&f)))) return nullptr;
    return f;
}

// Encodes any WIC bitmap source as a 32-bit PNG
bool EncodePng(IWICImagingFactory* factory, IWICBitmapSource* src, std::vector<unsigned char>& png) {
    IWICFormatConverter* conv = nullptr;
    IStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    UINT w = 0, h = 0;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    STATSTG stat{};
    bool ok = SUCCEEDED(src->GetSize(&w, &h)) && w > 0 && h > 0 &&
        SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                   nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&frame, &props)) &&
        SUCCEEDED(frame->Initialize(props)) &&
        SUCCEEDED(frame->SetSize(w, h)) &&
        SUCCEEDED(frame->SetPixelFormat(&format)) &&
        SUCCEEDED(frame->WriteSource(conv, nullptr)) &&
        SUCCEEDED(frame->Commit()) &&
        SUCCEEDED(enc->Commit()) &&
        SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME));
    if (ok) {
        LARGE_INTEGER zero{};
        stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        png.resize(static_cast<size_t>(stat.cbSize.QuadPart));
        ULONG read = 0;
        ok = SUCCEEDED(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read)) && read == png.size();
    }
    SafeRelease(props); SafeRelease(frame); SafeRelease(enc); SafeRelease(stream); SafeRelease(conv);
    return ok;
}

// Any picture file Windows can read (PNG, JPG, GIF, BMP, TIFF, ...) -> PNG
bool ConvertFileToPng(const std::wstring& path, std::vector<unsigned char>& png) {
    IWICImagingFactory* f = CreateWic();
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    const bool ok = f &&
        SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                               WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) &&
        EncodePng(f, frame, png);
    SafeRelease(frame); SafeRelease(dec); SafeRelease(f);
    return ok;
}

// Picture file bytes in any format WIC reads -> PNG
bool BytesToPng(const std::vector<unsigned char>& in, std::vector<unsigned char>& png) {
    if (in.empty()) return false;
    IWICImagingFactory* f = CreateWic();
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    const bool ok = f &&
        SUCCEEDED(f->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(in.data()), static_cast<DWORD>(in.size()))) &&
        SUCCEEDED(f->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) &&
        EncodePng(f, frame, png);
    SafeRelease(frame); SafeRelease(dec); SafeRelease(stream); SafeRelease(f);
    return ok;
}

// Draws into a white w x h bitmap and returns it as PNG
bool RenderToPng(int w, int h, const std::function<void(HDC, const RECT&)>& draw, std::vector<unsigned char>& png) {
    if (w <= 0 || h <= 0 || w > 8000 || h > 8000) return false;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(dc); return false; }
    HGDIOBJ old = SelectObject(dc, bmp);
    const RECT rc{ 0, 0, w, h };
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    draw(dc, rc);
    GdiFlush();
    SelectObject(dc, old);

    IWICImagingFactory* f = CreateWic();
    IWICBitmap* wb = nullptr;
    const bool ok = f && SUCCEEDED(f->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &wb)) &&
                    EncodePng(f, wb, png);
    SafeRelease(wb); SafeRelease(f);
    DeleteObject(bmp);
    DeleteDC(dc);
    return ok;
}

// Offset of the pixel bits in a packed DIB (BITMAPINFOHEADER + color table + bits)
size_t DibBitsOffset(const BITMAPINFOHEADER& h) {
    size_t off = h.biSize;
    if (h.biCompression == BI_BITFIELDS && h.biSize == sizeof(BITMAPINFOHEADER)) off += 3 * sizeof(DWORD);
    const DWORD colors = h.biClrUsed ? h.biClrUsed : (h.biBitCount <= 8 ? (1u << h.biBitCount) : 0);
    return off + colors * sizeof(RGBQUAD);
}

// Packed DIB (clipboard CF_DIB, RTF \dibitmap) -> PNG at w x h (0 = its own size)
bool DibToPng(const std::vector<unsigned char>& dib, int w, int h, std::vector<unsigned char>& png) {
    if (dib.size() < sizeof(BITMAPINFOHEADER)) return false;
    BITMAPINFOHEADER hdr;
    memcpy(&hdr, dib.data(), sizeof(hdr));
    const size_t off = DibBitsOffset(hdr);
    if (hdr.biWidth <= 0 || hdr.biHeight == 0 || off >= dib.size()) return false;
    const int bw = hdr.biWidth, bh = std::abs(hdr.biHeight);
    if (w <= 0 || h <= 0) { w = bw; h = bh; }
    return RenderToPng(w, h, [&](HDC dc, const RECT&) {
        StretchDIBits(dc, 0, 0, w, h, 0, 0, bw, bh, dib.data() + off,
                      reinterpret_cast<const BITMAPINFO*>(dib.data()), DIB_RGB_COLORS, SRCCOPY);
    }, png);
}

// ============================================================================
// Rich text: one document model, read from RTF or HTML, written as RTF, HTML
// or plain text. RTF is what the Rich Edit box holds; HTML is generated from
// it for Teams and browsers.
// ============================================================================
constexpr COLORREF kAuto = 0xFFFFFFFF;    // automatic color / no highlight
constexpr int kDefaultHalfPoints = 22;    // 11 pt, Clippi's default size
constexpr wchar_t kDefaultFace[] = L"Calibri";

bool IsDefaultFace(const std::wstring& f) { return f.empty() || IEquals(f, kDefaultFace); }
bool IsDefaultSize(int halfPoints) { return halfPoints == 0 || halfPoints == kDefaultHalfPoints; }
bool IsDefaultColor(COLORREF c) { return c == kAuto || c == RGB(0, 0, 0); }

struct Fmt {
    bool b = false, i = false, u = false, s = false;
    std::wstring font;      // empty = default (Calibri)
    int size = 0;           // half-points; 0 = default (11 pt)
    COLORREF color = kAuto; // text color
    COLORREF back = kAuto;  // highlight
    std::wstring link;      // hyperlink target, empty = none
    bool operator==(const Fmt& o) const {
        return b == o.b && i == o.i && u == o.u && s == o.s && font == o.font && size == o.size &&
               color == o.color && back == o.back && link == o.link;
    }
};

enum class RunKind { Text, Break, Picture }; // Break = line break inside a paragraph
enum class Align { Left, Center, Right, Justify };

struct Run {
    RunKind kind = RunKind::Text;
    std::wstring text;
    Fmt fmt;
    std::vector<unsigned char> data; // Picture: PNG or JPEG bytes
    bool jpeg = false;
    int w = 0, h = 0;                // Picture: display size in pixels (96 DPI)
};

struct Para {
    Align align = Align::Left;
    std::vector<Run> runs;
};

struct Doc {
    std::vector<Para> paras{ Para{} }; // always at least one
};

void AddText(Para& p, const Fmt& f, const std::wstring& t) {
    if (t.empty()) return;
    if (!p.runs.empty() && p.runs.back().kind == RunKind::Text && p.runs.back().fmt == f) {
        p.runs.back().text += t;
        return;
    }
    Run r;
    r.fmt = f;
    r.text = t;
    p.runs.push_back(std::move(r));
}

// Plain text (CR/LF or LF line breaks) -> paragraphs at the end of the doc
void AppendPlainToDoc(Doc& d, const std::wstring& t) {
    std::wstring line;
    for (wchar_t c : t) {
        if (c == L'\r') continue;
        if (c == L'\n') {
            AddText(d.paras.back(), Fmt{}, line);
            line.clear();
            d.paras.emplace_back();
            continue;
        }
        line += c;
    }
    AddText(d.paras.back(), Fmt{}, line);
}

bool DocHasContent(const Doc& d) {
    for (const Para& p : d.paras)
        for (const Run& r : p.runs)
            if (r.kind != RunKind::Text || !Trim(r.text).empty()) return true;
    return false;
}

// Drops empty paragraphs at the start and the end
void TrimDoc(Doc& d) {
    while (d.paras.size() > 1 && d.paras.back().runs.empty()) d.paras.pop_back();
    while (d.paras.size() > 1 && d.paras.front().runs.empty()) d.paras.erase(d.paras.begin());
}

// True if nothing in the doc needs RTF/HTML: default font, size and color,
// no bold/italic/..., no links, no pictures, left-aligned paragraphs.
bool IsPlainDoc(const Doc& d) {
    for (const Para& p : d.paras) {
        if (p.align != Align::Left) return false;
        for (const Run& r : p.runs) {
            if (r.kind == RunKind::Picture) return false;
            if (r.kind != RunKind::Text) continue;
            const Fmt& f = r.fmt;
            if (f.b || f.i || f.u || f.s || !f.link.empty() || !IsDefaultFace(f.font) ||
                !IsDefaultSize(f.size) || !IsDefaultColor(f.color) || f.back != kAuto)
                return false;
        }
    }
    return true;
}

// ---- Doc -> plain text ----

// Plain text with LF line breaks. Links become "text <url>" (unless the text
// already shows the address); pictures are left out.
std::wstring DocToPlain(const Doc& d) {
    std::wstring out;
    for (size_t p = 0; p < d.paras.size(); ++p) {
        if (p) out += L'\n';
        const auto& runs = d.paras[p].runs;
        for (size_t r = 0; r < runs.size();) {
            const Run& run = runs[r];
            if (run.kind == RunKind::Break) { out += L'\n'; ++r; continue; }
            if (run.kind == RunKind::Picture) { ++r; continue; }
            if (run.fmt.link.empty()) { out += run.text; ++r; continue; }
            const std::wstring url = run.fmt.link;
            std::wstring text;
            size_t q = r;
            while (q < runs.size() && runs[q].kind == RunKind::Text && runs[q].fmt.link == url) text += runs[q++].text;
            out += text;
            std::wstring shown = url;
            if (StartsWithI(shown, L"mailto:")) shown = shown.substr(7);
            if (Trim(text).empty()) out += shown;
            else if (text.find(shown) == std::wstring::npos) out += L" <" + shown + L">";
            r = q;
        }
    }
    std::replace(out.begin(), out.end(), L'\x00A0', L' ');
    return out;
}

// ---- Doc -> RTF ----

std::string RtfEscape(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) {
        switch (c) {
        case L'\\': out += "\\\\"; break;
        case L'{':  out += "\\{";  break;
        case L'}':  out += "\\}";  break;
        case L'\t': out += "\\tab "; break;
        case L'\x00A0': out += "\\~"; break;
        case L'\r': case L'\n': break;
        default:
            if (c < 32) break;
            if (c < 128) out += static_cast<char>(c);
            else out += "\\u" + std::to_string(static_cast<short>(c)) + "?";
        }
    }
    return out;
}

void AppendHex(std::string& out, const std::vector<unsigned char>& data) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < data.size(); ++i) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 0x0F];
        if (i % 64 == 63) out += "\r\n";
    }
}

// {\pict ...} for a PNG/JPEG picture run (picw/pich in pixels, goal in twips)
std::string RtfPicture(const Run& r) {
    int pw = r.w, ph = r.h;
    if (r.jpeg) JpegSize(r.data, pw, ph); else PngSize(r.data, pw, ph);
    std::string s = std::string("{\\pict") + (r.jpeg ? "\\jpegblip" : "\\pngblip") +
                    "\\picw" + std::to_string(pw) + "\\pich" + std::to_string(ph) +
                    "\\picwgoal" + std::to_string(r.w * 15) + "\\pichgoal" + std::to_string(r.h * 15) + "\r\n";
    AppendHex(s, r.data);
    return s + "}";
}

// A complete RTF document. inlineFirst: the first paragraph has no \pard, so
// when it is inserted into the box it joins the paragraph at the cursor.
std::string DocToRtf(const Doc& d, bool inlineFirst = false) {
    std::vector<std::wstring> faces{ kDefaultFace };
    std::vector<COLORREF> colors;
    auto faceIndex = [&](const std::wstring& f) -> int {
        if (IsDefaultFace(f)) return 0;
        for (size_t i = 1; i < faces.size(); ++i) if (IEquals(faces[i], f)) return static_cast<int>(i);
        faces.push_back(f);
        return static_cast<int>(faces.size() - 1);
    };
    auto colorIndex = [&](COLORREF c) -> int {
        if (c == kAuto) return 0;
        for (size_t i = 0; i < colors.size(); ++i) if (colors[i] == c) return static_cast<int>(i + 1);
        colors.push_back(c);
        return static_cast<int>(colors.size());
    };
    auto textRun = [&](const Run& x) {
        std::string s = "{\\f" + std::to_string(faceIndex(x.fmt.font)) +
                        "\\fs" + std::to_string(x.fmt.size ? x.fmt.size : kDefaultHalfPoints);
        if (const int cf = colorIndex(x.fmt.color)) s += "\\cf" + std::to_string(cf);
        if (const int hl = colorIndex(x.fmt.back)) s += "\\highlight" + std::to_string(hl);
        if (x.fmt.b) s += "\\b";
        if (x.fmt.i) s += "\\i";
        if (x.fmt.u) s += "\\ul";
        if (x.fmt.s) s += "\\strike";
        return s + " " + RtfEscape(x.text) + "}";
    };

    std::string body;
    for (size_t p = 0; p < d.paras.size(); ++p) {
        const Para& para = d.paras[p];
        if (!(inlineFirst && p == 0)) {
            static const char* kAlign[] = { "\\ql", "\\qc", "\\qr", "\\qj" };
            body += std::string("\\pard") + kAlign[static_cast<int>(para.align)] + " ";
        }
        const auto& runs = para.runs;
        for (size_t r = 0; r < runs.size();) {
            const Run& run = runs[r];
            if (run.kind == RunKind::Break) { body += "\\line "; ++r; continue; }
            if (run.kind == RunKind::Picture) { body += RtfPicture(run); ++r; continue; }
            if (run.fmt.link.empty()) { body += textRun(run); ++r; continue; }
            const std::wstring url = run.fmt.link;
            std::string inner;
            size_t q = r;
            while (q < runs.size() && runs[q].kind == RunKind::Text && runs[q].fmt.link == url) inner += textRun(runs[q++]);
            std::wstring target = url;
            ReplaceAll(target, L"\"", L"%22");
            body += "{\\field{\\*\\fldinst{HYPERLINK \"" + RtfEscape(target) + "\"}}{\\fldrslt{" + inner + "}}}";
            r = q;
        }
        if (p + 1 < d.paras.size()) body += "\\par\r\n";
    }

    std::string rtf = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl";
    for (size_t i = 0; i < faces.size(); ++i)
        rtf += "{\\f" + std::to_string(i) + "\\fnil\\fcharset0 " + RtfEscape(faces[i]) + ";}";
    rtf += "}";
    if (!colors.empty()) {
        rtf += "{\\colortbl ;";
        for (COLORREF c : colors)
            rtf += "\\red" + std::to_string(GetRValue(c)) + "\\green" + std::to_string(GetGValue(c)) +
                   "\\blue" + std::to_string(GetBValue(c)) + ";";
        rtf += "}";
    }
    return rtf + "\\viewkind4\\uc1\\f0\\fs22 " + body + "}";
}

// ---- Doc -> HTML ----

std::wstring HtmlAttr(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'"': out += L"&quot;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        default: out += c;
        }
    }
    return out;
}

// Text -> HTML. A space at the start of a line or after another space becomes
// &nbsp; so indentation and runs of spaces survive. prev carries the previous
// character across runs.
std::wstring HtmlText(const std::wstring& text, wchar_t& prev) {
    std::wstring out;
    for (wchar_t c : text) {
        const wchar_t before = prev;
        prev = c;
        switch (c) {
        case L' ':  out += (before == L' ' || before == L'\n') ? L"&nbsp;" : L" "; break;
        case L'&':  out += L"&amp;";  break;
        case L'<':  out += L"&lt;";   break;
        case L'>':  out += L"&gt;";   break;
        case L'"':  out += L"&quot;"; break;
        case L'\x00A0': out += L"&nbsp;"; break;
        case L'\t': out += L"&nbsp;&nbsp;&nbsp;&nbsp;"; prev = L' '; break;
        case L'\r': case L'\n': break;
        default:    out += c;
        }
    }
    return out;
}

std::wstring CssColor(COLORREF c) {
    wchar_t buf[8];
    swprintf(buf, 8, L"#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

std::wstring CssFor(const Fmt& f) {
    std::wstring css;
    if (!IsDefaultFace(f.font)) {
        std::wstring name = f.font;
        name.erase(std::remove_if(name.begin(), name.end(), [](wchar_t c) { return c == L'"' || c == L'\''; }), name.end());
        css += L"font-family:'" + HtmlAttr(name) + L"';";
    }
    if (!IsDefaultSize(f.size)) {
        css += L"font-size:" + std::to_wstring(f.size / 2) + (f.size % 2 ? L".5" : L"") + L"pt;";
    }
    if (!IsDefaultColor(f.color)) css += L"color:" + CssColor(f.color) + L";";
    if (f.back != kAuto) css += L"background-color:" + CssColor(f.back) + L";";
    return css;
}

// UTF-8 HTML fragment in the style Clippi uses (Calibri 11 pt), one <div> per paragraph.
// Black and automatic text color are left out, so the text follows the reader's
// theme (dark mode in Teams).
std::string DocToHtml(const Doc& d) {
    std::wstring h = L"<div style=\"font-family:Calibri,Arial,sans-serif;font-size:11pt\">";
    for (const Para& para : d.paras) {
        h += L"<div";
        if (para.align == Align::Center) h += L" style=\"text-align:center\"";
        else if (para.align == Align::Right) h += L" style=\"text-align:right\"";
        else if (para.align == Align::Justify) h += L" style=\"text-align:justify\"";
        h += L">";
        wchar_t prev = L'\n';
        bool any = false;
        std::wstring openLink;
        for (const Run& run : para.runs) {
            if (run.kind == RunKind::Break) { h += L"<br>"; prev = L'\n'; continue; }
            const std::wstring link = run.kind == RunKind::Text ? run.fmt.link : openLink;
            if (link != openLink) {
                if (!openLink.empty()) h += L"</a>";
                if (!link.empty()) h += L"<a href=\"" + HtmlAttr(link) + L"\">";
                openLink = link;
            }
            if (run.kind == RunKind::Picture) {
                std::wstring b64 = Base64Encode(run.data);
                h += std::wstring(L"<img src=\"data:image/") + (run.jpeg ? L"jpeg" : L"png") + L";base64," + b64 +
                     L"\" width=\"" + std::to_wstring(run.w) + L"\" height=\"" + std::to_wstring(run.h) + L"\" alt=\"\">";
                prev = L'x';
                any = true;
                continue;
            }
            if (run.text.empty()) continue;
            any = true;
            const std::wstring css = CssFor(run.fmt);
            std::wstring open, close;
            if (!css.empty()) { open += L"<span style=\"" + css + L"\">"; close = L"</span>" + close; }
            if (run.fmt.b) { open += L"<b>"; close = L"</b>" + close; }
            if (run.fmt.i) { open += L"<i>"; close = L"</i>" + close; }
            if (run.fmt.u) { open += L"<u>"; close = L"</u>" + close; }
            if (run.fmt.s) { open += L"<s>"; close = L"</s>" + close; }
            h += open + HtmlText(run.text, prev) + close;
        }
        if (!openLink.empty()) h += L"</a>";
        if (!any) h += L"<br>";
        h += L"</div>";
    }
    return ToUtf8(h + L"</div>");
}

// ---- RTF -> Doc ----

int CodePageForCharset(int charset, int ansi) {
    switch (charset) {
    case 0: case 1: return ansi;
    case 2:   return -1; // Symbol
    case 77:  return 10000;
    case 128: return 932;  case 129: return 949;  case 130: return 1361;
    case 134: return 936;  case 136: return 950;  case 161: return 1253;
    case 162: return 1254; case 163: return 1258; case 177: return 1255;
    case 178: return 1256; case 186: return 1257; case 204: return 1251;
    case 222: return 874;  case 238: return 1250; case 255: return 437;
    default:  return ansi;
    }
}

std::wstring DecodeBytes(const std::string& b, int cp) {
    if (b.empty()) return std::wstring();
    if (cp == -1) { // Symbol font: the bullet is the only character that matters here
        std::wstring out;
        for (unsigned char ch : b) out += ch == 0xB7 ? L'\x2022' : static_cast<wchar_t>(ch);
        return out;
    }
    int n = MultiByteToWideChar(static_cast<UINT>(cp), 0, b.data(), static_cast<int>(b.size()), nullptr, 0);
    if (n <= 0) { cp = 1252; n = MultiByteToWideChar(1252, 0, b.data(), static_cast<int>(b.size()), nullptr, 0); }
    std::wstring out(static_cast<size_t>(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(static_cast<UINT>(cp), 0, b.data(), static_cast<int>(b.size()), &out[0], n);
    return out;
}

struct RtfPict {
    std::string kind;   // png, jpeg, emf, wmf, dib
    int param = 0;      // wmf: mapping mode
    int picw = 0, pich = 0, goalw = 0, goalh = 0, scalex = 100, scaley = 100;
    std::vector<unsigned char> data;
    int hi = -1;        // first hex digit of a byte
};

// A \pict group -> picture run (PNG/JPEG as they are; metafiles and bitmaps drawn into a PNG)
bool PictToRun(const RtfPict& p, Run& run) {
    run = Run{};
    run.kind = RunKind::Picture;
    auto shown = [](int px, int goal, int scale) {
        return goal > 0 ? std::max(1, MulDiv(goal, scale, 100) / 15) : std::max(1, MulDiv(px, scale, 100));
    };
    if (p.kind == "png" || p.kind == "jpeg") {
        int w = p.picw, h = p.pich;
        const bool ok = p.kind == "png" ? PngSize(p.data, w, h) : JpegSize(p.data, w, h);
        if (!ok) return false;
        run.data = p.data;
        run.jpeg = p.kind == "jpeg";
        run.w = shown(w, p.goalw, p.scalex);
        run.h = shown(h, p.goalh, p.scaley);
        return true;
    }
    int w = 0, h = 0;
    if (p.kind == "emf" || p.kind == "wmf") { // picw/pich in 0.01 mm
        w = p.goalw > 0 ? shown(0, p.goalw, p.scalex) : MulDiv(p.picw, 96 * p.scalex / 100, 2540);
        h = p.goalh > 0 ? shown(0, p.goalh, p.scaley) : MulDiv(p.pich, 96 * p.scaley / 100, 2540);
    } else if (p.kind == "dib") {
        w = shown(p.picw, p.goalw, p.scalex);
        h = shown(p.pich, p.goalh, p.scaley);
    } else {
        return false;
    }
    if (w <= 0 || h <= 0 || w > 4000 || h > 4000 || p.data.empty()) return false;
    bool ok = false;
    if (p.kind == "dib") {
        ok = DibToPng(p.data, w, h, run.data);
    } else {
        HENHMETAFILE emf = nullptr;
        if (p.kind == "emf") {
            emf = SetEnhMetaFileBits(static_cast<UINT>(p.data.size()), p.data.data());
        } else {
            METAFILEPICT mfp{ p.param ? p.param : MM_ANISOTROPIC, p.picw, p.pich, nullptr };
            emf = SetWinMetaFileBits(static_cast<UINT>(p.data.size()), p.data.data(), nullptr, &mfp);
        }
        if (emf) {
            ok = RenderToPng(w, h, [&](HDC dc, const RECT& rc) { PlayEnhMetaFile(dc, emf, &rc); }, run.data);
            DeleteEnhMetaFile(emf);
        }
    }
    run.w = w;
    run.h = h;
    return ok;
}

// Reads the RTF that Rich Edit, Word and Outlook write: text, character
// formatting, alignment, hyperlinks and pictures. Everything else is skipped.
Doc RtfToDoc(const std::string& rtf) {
    Doc doc;
    enum Dest { DNormal, DSkip, DObject, DFontTbl, DColorTbl, DFldInst, DPict, DPnText };
    struct State {
        Dest dest = DNormal;
        int font = 0, fs = 24, cf = 0, hl = 0, uc = 1;
        bool b = false, i = false, u = false, s = false;
        std::wstring link;
        bool ownsField = false, ownsPict = false;
    };
    struct FontEntry { std::wstring name; int charset = 0; bool done = false; };

    State cur;
    std::vector<State> stack;
    std::map<int, FontEntry> fonts;
    int fontEntry = -1, deff = 0, ansi = 1252;
    std::vector<COLORREF> colors;
    int cr = 0, cg = 0, cb = 0;
    bool colorSet = false;
    std::vector<std::wstring> fields; // link targets of the open \field groups
    std::wstring fldinst;
    RtfPict pict;
    Align align = Align::Left;
    std::wstring text;  // characters waiting to be added
    std::string bytes;  // \'hh bytes waiting to be decoded
    int skip = 0;       // characters to skip after \uN
    bool star = false;  // last token was \*

    auto codePage = [&]() {
        const auto it = fonts.find(cur.dest == DFontTbl ? INT_MIN : cur.font);
        return CodePageForCharset(it == fonts.end() ? 0 : it->second.charset, ansi);
    };
    auto flushBytes = [&]() {
        if (!bytes.empty()) { text += DecodeBytes(bytes, codePage()); bytes.clear(); }
    };
    auto fmtNow = [&]() {
        Fmt f;
        f.b = cur.b; f.i = cur.i; f.u = cur.u; f.s = cur.s;
        const auto it = fonts.find(cur.font);
        if (it != fonts.end()) f.font = Trim(it->second.name);
        f.size = cur.fs;
        f.color = cur.cf >= 0 && cur.cf < static_cast<int>(colors.size()) ? colors[static_cast<size_t>(cur.cf)] : kAuto;
        f.back = cur.hl > 0 && cur.hl < static_cast<int>(colors.size()) ? colors[static_cast<size_t>(cur.hl)] : kAuto;
        f.link = cur.link;
        return f;
    };
    auto flush = [&]() {
        flushBytes();
        if (text.empty()) return;
        switch (cur.dest) {
        case DNormal:
            AddText(doc.paras.back(), fmtNow(), text);
            break;
        case DPnText: { // the bullet or number of a list item
            Fmt f = fmtNow();
            f.font.clear();
            AddText(doc.paras.back(), f, text);
            break;
        }
        case DFontTbl:
            for (wchar_t ch : text) {
                if (fontEntry == INT_MIN) continue;
                FontEntry& fe = fonts[fontEntry];
                if (fe.done) continue;
                if (ch == L';') fe.done = true; else fe.name += ch;
            }
            break;
        case DColorTbl:
            for (wchar_t ch : text)
                if (ch == L';') {
                    colors.push_back(colorSet ? RGB(cr, cg, cb) : kAuto);
                    cr = cg = cb = 0;
                    colorSet = false;
                }
            break;
        case DFldInst:
            fldinst += text;
            break;
        default:
            break;
        }
        text.clear();
    };
    auto addChar = [&](wchar_t c) {
        if (skip > 0) { --skip; return; }
        flushBytes();
        text += c;
    };
    auto addByte = [&](unsigned char c) {
        if (skip > 0) { --skip; return; }
        bytes += static_cast<char>(c);
    };
    auto endGroup = [&]() {
        flush();
        const State closing = cur;
        if (!stack.empty()) { cur = stack.back(); stack.pop_back(); }
        if (closing.dest == DFldInst && cur.dest != DFldInst) {
            const size_t at = fldinst.find(L"HYPERLINK");
            if (at != std::wstring::npos && !fields.empty()) {
                const size_t q1 = fldinst.find(L'"', at);
                const size_t q2 = q1 == std::wstring::npos ? std::wstring::npos : fldinst.find(L'"', q1 + 1);
                fields.back() = Trim(q2 != std::wstring::npos ? fldinst.substr(q1 + 1, q2 - q1 - 1)
                                                              : fldinst.substr(at + 9));
            }
            fldinst.clear();
        }
        if (closing.ownsPict) {
            Run r;
            if ((cur.dest == DNormal || cur.dest == DPnText) && PictToRun(pict, r))
                doc.paras.back().runs.push_back(std::move(r));
            pict = RtfPict{};
        }
        if (closing.ownsField && !fields.empty()) fields.pop_back();
    };
    auto word = [&](const std::string& w, bool hasParam, int param) {
        const bool starred = star;
        star = false;
        if (starred) {
            flush();
            if (w == "fldinst") { cur.dest = DFldInst; fldinst.clear(); }
            else if (w != "shppict") cur.dest = DSkip; // unknown \* destination
            return;
        }
        if (cur.dest == DSkip) return;
        if (cur.dest == DObject) { if (w == "result") cur.dest = DNormal; return; }
        if (cur.dest == DPict) {
            if (w == "pngblip") pict.kind = "png";
            else if (w == "jpegblip") pict.kind = "jpeg";
            else if (w == "emfblip") pict.kind = "emf";
            else if (w == "wmetafile") { pict.kind = "wmf"; pict.param = param; }
            else if (w == "dibitmap") pict.kind = "dib";
            else if (w == "picw") pict.picw = param;
            else if (w == "pich") pict.pich = param;
            else if (w == "picwgoal") pict.goalw = param;
            else if (w == "pichgoal") pict.goalh = param;
            else if (w == "picscalex") pict.scalex = param > 0 ? param : 100;
            else if (w == "picscaley") pict.scaley = param > 0 ? param : 100;
            return;
        }

        // Words that produce a character
        static const struct { const char* name; wchar_t ch; } kChars[] = {
            { "emdash", L'\x2014' }, { "endash", L'\x2013' }, { "bullet", L'\x2022' },
            { "lquote", L'\x2018' }, { "rquote", L'\x2019' }, { "ldblquote", L'\x201C' },
            { "rdblquote", L'\x201D' }, { "emspace", L' ' }, { "enspace", L' ' }, { "qmspace", L' ' },
        };
        if (w == "tab") { addChar(cur.dest == DPnText ? L' ' : L'\t'); return; }
        if (w == "u") {
            skip = 0;
            addChar(static_cast<wchar_t>(param < 0 ? param + 65536 : param));
            skip = cur.uc;
            return;
        }
        for (const auto& k : kChars)
            if (w == k.name) { addChar(k.ch); return; }

        flush();
        skip = 0;
        if (cur.dest == DFontTbl) {
            if (w == "f") { fontEntry = param; fonts[param] = FontEntry{}; }
            else if (w == "fcharset" && fontEntry != INT_MIN) fonts[fontEntry].charset = param;
            return;
        }
        if (cur.dest == DColorTbl) {
            if (w == "red") { cr = param; colorSet = true; }
            else if (w == "green") { cg = param; colorSet = true; }
            else if (w == "blue") { cb = param; colorSet = true; }
            return;
        }
        if (w == "ansicpg") { ansi = param; return; }
        if (w == "deff") { deff = param; cur.font = param; return; }
        if (w == "fonttbl") { cur.dest = DFontTbl; fontEntry = INT_MIN; return; }
        if (w == "colortbl") { cur.dest = DColorTbl; colors.clear(); return; }
        static const char* kSkip[] = {
            "stylesheet", "info", "header", "footer", "headerl", "headerr", "headerf", "footerl",
            "footerr", "footerf", "footnote", "nonshppict", "listtable", "listoverridetable",
            "revtbl", "rsidtbl", "xmlnstbl", "themedata", "colorschememapping", "latentstyles",
            "datastore", "pgdsctbl", "filetbl", "fldtype",
        };
        for (const char* k : kSkip)
            if (w == k) { cur.dest = DSkip; return; }
        if (w == "object") { cur.dest = DObject; return; }
        if (w == "pict") { cur.dest = DPict; cur.ownsPict = true; pict = RtfPict{}; return; }
        if (w == "field") { fields.emplace_back(); cur.ownsField = true; return; }
        if (w == "fldrslt") { cur.dest = DNormal; if (!fields.empty()) cur.link = fields.back(); return; }
        if (w == "pntext") { cur.dest = DPnText; return; }
        if (cur.dest == DFldInst) return; // formatting inside a field instruction doesn't matter

        if (w == "par") {
            if (cur.dest == DNormal) {
                doc.paras.back().align = align;
                doc.paras.emplace_back();
            }
            return;
        }
        if (w == "line") {
            if (cur.dest == DNormal) { Run r; r.kind = RunKind::Break; doc.paras.back().runs.push_back(r); }
            return;
        }
        if (w == "pard") { align = Align::Left; return; }
        if (w == "ql") { align = Align::Left; return; }
        if (w == "qc") { align = Align::Center; return; }
        if (w == "qr") { align = Align::Right; return; }
        if (w == "qj") { align = Align::Justify; return; }
        if (w == "plain") {
            cur.b = cur.i = cur.u = cur.s = false;
            cur.fs = 24; cur.font = deff; cur.cf = 0; cur.hl = 0;
            return;
        }
        const bool on = !hasParam || param != 0;
        if (w == "b") cur.b = on;
        else if (w == "i") cur.i = on;
        else if (w == "strike" || w == "striked") cur.s = on;
        else if (w == "ulnone") cur.u = false;
        else if (w.compare(0, 2, "ul") == 0 && w != "ulc") cur.u = on;
        else if (w == "f") cur.font = param;
        else if (w == "fs") cur.fs = param;
        else if (w == "cf") cur.cf = param;
        else if (w == "highlight" || w == "cb" || w == "chcbpat") cur.hl = param;
        else if (w == "uc") cur.uc = param;
    };

    const size_t n = rtf.size();
    for (size_t i = 0; i < n;) {
        const char c = rtf[i];
        if (c == '{') {
            flush();
            stack.push_back(cur);
            cur.ownsField = cur.ownsPict = false;
            skip = 0;
            ++i;
            continue;
        }
        if (c == '}') { endGroup(); skip = 0; ++i; continue; }
        if (c == '\\') {
            if (i + 1 >= n) break;
            const char d = rtf[i + 1];
            if (isalpha(static_cast<unsigned char>(d))) {
                size_t j = i + 1;
                std::string w;
                while (j < n && isalpha(static_cast<unsigned char>(rtf[j]))) w += rtf[j++];
                bool hasParam = false, neg = false;
                long long v = 0;
                if (j < n && rtf[j] == '-') { neg = true; ++j; }
                while (j < n && isdigit(static_cast<unsigned char>(rtf[j]))) {
                    hasParam = true;
                    v = std::min<long long>(v * 10 + (rtf[j] - '0'), INT_MAX);
                    ++j;
                }
                const int param = static_cast<int>(neg ? -v : v);
                if (j < n && rtf[j] == ' ') ++j; // the delimiter space belongs to the word
                i = j;
                if (w == "bin" && hasParam && param > 0) { // raw binary data
                    const size_t len = std::min(static_cast<size_t>(param), n - i);
                    if (cur.dest == DPict) pict.data.insert(pict.data.end(), rtf.begin() + static_cast<long>(i),
                                                            rtf.begin() + static_cast<long>(i + len));
                    i += len;
                    star = false;
                    continue;
                }
                word(w, hasParam, param);
                continue;
            }
            i += 2;
            switch (d) {
            case '\\': case '{': case '}': addChar(static_cast<wchar_t>(d)); break;
            case '\'':
                if (i + 1 < n) {
                    const int hi = HexVal(rtf[i]), lo = HexVal(rtf[i + 1]);
                    if (hi >= 0 && lo >= 0 && cur.dest != DPict) addByte(static_cast<unsigned char>(hi * 16 + lo));
                    i += 2;
                }
                break;
            case '*': star = true; break;
            case '~': addChar(L'\x00A0'); break;
            case '_': addChar(L'\x2011'); break;
            case '\r': case '\n': word("par", false, 0); break;
            default: break;
            }
            continue;
        }
        if (c == '\r' || c == '\n') { ++i; continue; }
        if (cur.dest == DPict) {
            const int v = HexVal(c);
            if (v >= 0) {
                if (pict.hi < 0) pict.hi = v;
                else { pict.data.push_back(static_cast<unsigned char>(pict.hi * 16 + v)); pict.hi = -1; }
            }
            ++i;
            continue;
        }
        if (static_cast<unsigned char>(c) >= 128) addByte(static_cast<unsigned char>(c));
        else addChar(static_cast<wchar_t>(c));
        ++i;
    }
    flush();
    doc.paras.back().align = align;
    // Rich Edit ends every document with a paragraph mark; it isn't an extra line
    if (doc.paras.size() > 1 && doc.paras.back().runs.empty()) doc.paras.pop_back();
    return doc;
}

// ---- HTML -> Doc ----

std::wstring DecodeEntities(const std::wstring& s) {
    if (s.find(L'&') == std::wstring::npos) return s;
    static const struct { const wchar_t* name; unsigned cp; } kNamed[] = {
        { L"amp", 38 }, { L"lt", 60 }, { L"gt", 62 }, { L"quot", 34 }, { L"apos", 39 },
        { L"nbsp", 160 }, { L"copy", 169 }, { L"reg", 174 }, { L"trade", 8482 },
        { L"hellip", 8230 }, { L"mdash", 8212 }, { L"ndash", 8211 }, { L"lsquo", 8216 },
        { L"rsquo", 8217 }, { L"ldquo", 8220 }, { L"rdquo", 8221 }, { L"bull", 8226 },
        { L"middot", 183 }, { L"euro", 8364 }, { L"deg", 176 }, { L"times", 215 },
        { L"laquo", 171 }, { L"raquo", 187 }, { L"ensp", 8194 }, { L"emsp", 8195 },
        { L"thinsp", 8201 }, { L"shy", 173 }, { L"pound", 163 }, { L"cent", 162 },
    };
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != L'&') { out += s[i]; continue; }
        const size_t semi = s.find(L';', i);
        if (semi == std::wstring::npos || semi - i > 10) { out += s[i]; continue; }
        const std::wstring ent = s.substr(i + 1, semi - i - 1);
        unsigned long cp = 0;
        if (!ent.empty() && ent[0] == L'#') {
            cp = (ent.size() > 1 && (ent[1] == L'x' || ent[1] == L'X')) ? wcstoul(ent.c_str() + 2, nullptr, 16)
                                                                        : wcstoul(ent.c_str() + 1, nullptr, 10);
        } else {
            for (const auto& k : kNamed) if (ent == k.name) { cp = k.cp; break; }
        }
        if (cp == 0 || cp > 0x10FFFF) { out += s[i]; continue; }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<wchar_t>(cp);
        }
        i = semi;
    }
    return out;
}

bool ParseCssColor(const std::wstring& value, COLORREF& out) {
    const std::wstring v = ToLowerW(Trim(value));
    if (v.empty()) return false;
    int r = 0, g = 0, b = 0;
    if (v[0] == L'#') {
        std::wstring hx;
        for (size_t i = 1; i < v.size() && iswxdigit(v[i]); ++i) hx += v[i];
        if (hx.size() == 3 || hx.size() == 4) {
            r = HexVal(hx[0]) * 17; g = HexVal(hx[1]) * 17; b = HexVal(hx[2]) * 17;
        } else if (hx.size() == 6 || hx.size() == 8) {
            r = HexVal(hx[0]) * 16 + HexVal(hx[1]);
            g = HexVal(hx[2]) * 16 + HexVal(hx[3]);
            b = HexVal(hx[4]) * 16 + HexVal(hx[5]);
        } else {
            return false;
        }
    } else if (v.compare(0, 3, L"rgb") == 0) {
        const size_t open = v.find(L'(');
        if (open == std::wstring::npos) return false;
        const wchar_t* p = v.c_str() + open + 1;
        int vals[3] = {};
        for (int k = 0; k < 3; ++k) {
            while (*p == L' ' || *p == L',') ++p;
            wchar_t* end = nullptr;
            double d = wcstod(p, &end);
            if (end == p) return false;
            p = end;
            if (*p == L'%') { d = d * 255.0 / 100.0; ++p; }
            vals[k] = std::max(0, std::min(255, static_cast<int>(d + 0.5)));
        }
        r = vals[0]; g = vals[1]; b = vals[2];
    } else {
        static const struct { const wchar_t* name; COLORREF c; } kNames[] = {
            { L"black", RGB(0, 0, 0) }, { L"white", RGB(255, 255, 255) }, { L"red", RGB(255, 0, 0) },
            { L"green", RGB(0, 128, 0) }, { L"blue", RGB(0, 0, 255) }, { L"yellow", RGB(255, 255, 0) },
            { L"orange", RGB(255, 165, 0) }, { L"purple", RGB(128, 0, 128) }, { L"gray", RGB(128, 128, 128) },
            { L"grey", RGB(128, 128, 128) }, { L"silver", RGB(192, 192, 192) }, { L"maroon", RGB(128, 0, 0) },
            { L"navy", RGB(0, 0, 128) }, { L"teal", RGB(0, 128, 128) }, { L"olive", RGB(128, 128, 0) },
            { L"lime", RGB(0, 255, 0) }, { L"aqua", RGB(0, 255, 255) }, { L"cyan", RGB(0, 255, 255) },
            { L"fuchsia", RGB(255, 0, 255) }, { L"magenta", RGB(255, 0, 255) }, { L"darkred", RGB(139, 0, 0) },
            { L"darkblue", RGB(0, 0, 139) }, { L"darkgreen", RGB(0, 100, 0) }, { L"darkgray", RGB(169, 169, 169) },
        };
        for (const auto& k : kNames) if (v == k.name) { out = k.c; return true; }
        return false;
    }
    out = RGB(r, g, b);
    return true;
}

// First family of a CSS font-family list; generic families map to common fonts
std::wstring CssFontFamily(const std::wstring& value) {
    std::wstring f = value.substr(0, value.find(L','));
    f.erase(std::remove_if(f.begin(), f.end(), [](wchar_t c) { return c == L'"' || c == L'\''; }), f.end());
    f = Trim(f);
    const std::wstring l = ToLowerW(f);
    if (l == L"sans-serif" || l == L"inherit" || l == L"initial") return L"";
    if (l == L"serif") return L"Times New Roman";
    if (l == L"monospace") return L"Consolas";
    return f;
}

// CSS font-size -> half-points (0 if not understood); base = current size in half-points
int CssFontSize(const std::wstring& value, int base) {
    const std::wstring v = ToLowerW(Trim(value));
    static const struct { const wchar_t* name; int hp; } kWords[] = {
        { L"xx-small", 14 }, { L"x-small", 16 }, { L"small", 20 }, { L"medium", 24 },
        { L"large", 27 }, { L"x-large", 36 }, { L"xx-large", 48 },
    };
    for (const auto& k : kWords) if (v == k.name) return k.hp;
    if (v == L"smaller") return std::max(2, base * 5 / 6);
    if (v == L"larger") return base * 6 / 5;
    wchar_t* end = nullptr;
    const double d = wcstod(v.c_str(), &end);
    if (end == v.c_str() || d <= 0) return 0;
    const std::wstring unit = Trim(end);
    double hp = 0;
    if (unit == L"pt") hp = d * 2;
    else if (unit == L"px" || unit.empty()) hp = d * 1.5; // 1px = 0.75pt
    else if (unit == L"em") hp = d * base;
    else if (unit == L"rem") hp = d * kDefaultHalfPoints;
    else if (unit == L"%") hp = d * base / 100.0;
    else return 0;
    return std::max(2, std::min(800, static_cast<int>(hp + 0.5)));
}

void ApplyCss(const std::wstring& css, Fmt& f, Align& align) {
    size_t pos = 0;
    while (pos < css.size()) {
        size_t semi = css.find(L';', pos);
        if (semi == std::wstring::npos) semi = css.size();
        const std::wstring decl = css.substr(pos, semi - pos);
        pos = semi + 1;
        const size_t colon = decl.find(L':');
        if (colon == std::wstring::npos) continue;
        const std::wstring prop = ToLowerW(Trim(decl.substr(0, colon)));
        std::wstring val = Trim(decl.substr(colon + 1));
        const size_t imp = ToLowerW(val).find(L"!important");
        if (imp != std::wstring::npos) val = Trim(val.substr(0, imp));
        const std::wstring lv = ToLowerW(val);
        COLORREF c;
        if (prop == L"font-weight") f.b = lv == L"bold" || lv == L"bolder" || _wtoi(lv.c_str()) >= 600;
        else if (prop == L"font-style") f.i = lv == L"italic" || lv == L"oblique";
        else if (prop == L"text-decoration" || prop == L"text-decoration-line") {
            if (lv.find(L"underline") != std::wstring::npos) f.u = true;
            if (lv.find(L"line-through") != std::wstring::npos) f.s = true;
            if (lv == L"none") f.u = f.s = false;
        }
        else if (prop == L"color") { if (ParseCssColor(lv, c)) f.color = c; }
        else if (prop == L"background-color" || prop == L"background") {
            // white "backgrounds" are page color, not highlighting
            if (ParseCssColor(lv, c) && c != RGB(255, 255, 255)) f.back = c;
        }
        else if (prop == L"font-family") f.font = CssFontFamily(val);
        else if (prop == L"font-size") {
            const int hp = CssFontSize(lv, f.size ? f.size : kDefaultHalfPoints);
            if (hp > 0) f.size = hp;
        }
        else if (prop == L"text-align") {
            if (lv == L"center") align = Align::Center;
            else if (lv == L"right" || lv == L"end") align = Align::Right;
            else if (lv == L"justify") align = Align::Justify;
            else if (lv == L"left" || lv == L"start") align = Align::Left;
        }
    }
}

// Converts HTML (UTF-8) to the document model: text, bold/italic/underline/
// strikeout, fonts, sizes, colors, highlight, links, line breaks, paragraphs,
// lists (as bullets/numbers), alignment and embedded (data:) pictures.
// skippedPictures counts pictures that are only a file name or web address.
Doc HtmlToDoc(const std::string& htmlUtf8, int* skippedPictures = nullptr) {
    if (skippedPictures) *skippedPictures = 0;
    const std::wstring h = FromBytes(htmlUtf8.data(), htmlUtf8.size());
    Doc doc;
    struct El { std::wstring tag; Fmt fmt; Align align = Align::Left; bool pre = false; int list = -1; };
    std::vector<El> st(1);
    bool space = true;  // the last thing added was a space or a line start (collapse more)

    auto para = [&]() -> Para& { return doc.paras.back(); };
    auto startContent = [&]() { if (para().runs.empty()) para().align = st.back().align; };
    // A collapsed space at the end of a line isn't shown by browsers either
    auto trimLineEnd = [&]() {
        auto& runs = para().runs;
        if (!runs.empty() && runs.back().kind == RunKind::Text) {
            std::wstring& t = runs.back().text;
            while (!t.empty() && t.back() == L' ') t.pop_back();
            if (t.empty()) runs.pop_back();
        }
    };
    auto blockBreak = [&]() { trimLineEnd(); if (!para().runs.empty()) doc.paras.emplace_back(); space = true; };
    auto addBreak = [&]() { trimLineEnd(); startContent(); Run r; r.kind = RunKind::Break; para().runs.push_back(r); space = true; };
    auto addText = [&](const std::wstring& t) {
        const El& e = st.back();
        std::wstring out;
        auto put = [&]() { if (!out.empty()) { startContent(); AddText(para(), e.fmt, out); out.clear(); } };
        for (wchar_t c : t) {
            if (e.pre) {
                if (c == L'\r') continue;
                if (c == L'\n') { put(); addBreak(); continue; }
                out += c;
                continue;
            }
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f') {
                if (!space) { out += L' '; space = true; } // one space, in the formatting it appears in
                continue;
            }
            space = false;
            out += c;
        }
        put();
    };
    auto addImage = [&](const std::vector<std::pair<std::wstring, std::wstring>>& attrs) {
        auto attr = [&](const wchar_t* k) { for (const auto& a : attrs) if (a.first == k) return a.second; return std::wstring(); };
        const std::wstring src = Trim(attr(L"src"));
        const size_t comma = src.find(L',');
        const std::wstring head = ToLowerW(src.substr(0, comma == std::wstring::npos ? 0 : comma));
        Run r;
        r.kind = RunKind::Picture;
        bool ok = false;
        if (comma != std::wstring::npos && StartsWithI(head, L"data:image/") &&
            head.find(L";base64") != std::wstring::npos) {
            std::vector<unsigned char> data = Base64Decode(src.substr(comma + 1));
            int pw = 0, ph = 0;
            if (PngSize(data, pw, ph)) { r.data = std::move(data); ok = true; }
            else if (JpegSize(data, pw, ph)) { r.data = std::move(data); r.jpeg = true; ok = true; }
            else if (BytesToPng(data, r.data) && PngSize(r.data, pw, ph)) ok = true; // GIF, BMP, ...
            if (ok) {
                int w = _wtoi(attr(L"width").c_str()), hh = _wtoi(attr(L"height").c_str());
                const std::wstring style = ToLowerW(attr(L"style"));
                auto cssPx = [&](const wchar_t* prop) {
                    const size_t at = style.find(prop);
                    return at == std::wstring::npos ? 0 : _wtoi(style.c_str() + at + wcslen(prop));
                };
                if (const int sw = cssPx(L"width:")) w = sw;
                if (const int sh = cssPx(L"height:")) hh = sh;
                if (w > 0 && hh <= 0) hh = MulDiv(ph, w, pw);
                if (hh > 0 && w <= 0) w = MulDiv(pw, hh, ph);
                if (w <= 0 || hh <= 0) { w = pw; hh = ph; }
                r.w = w;
                r.h = hh;
            }
        }
        if (!ok) { if (skippedPictures && !src.empty()) ++*skippedPictures; return; }
        startContent();
        para().runs.push_back(std::move(r));
        space = false;
    };

    static const wchar_t* kBlocks[] = {
        L"p", L"div", L"h1", L"h2", L"h3", L"h4", L"h5", L"h6", L"li", L"ul", L"ol", L"table", L"tr",
        L"blockquote", L"pre", L"section", L"article", L"header", L"footer", L"dt", L"dd", L"dl",
        L"center", L"address", L"figure", L"nav", L"main", L"aside", L"tbody", L"thead", L"tfoot",
        L"caption", L"form", L"fieldset",
    };
    static const wchar_t* kVoid[] = { L"meta", L"link", L"input", L"wbr", L"col", L"area", L"base",
                                      L"source", L"param", L"embed", L"basefont" };
    static const int kHeading[] = { 48, 36, 28, 24, 22, 20 }; // h1..h6 in half-points

    std::wstring textBuf;
    auto flushText = [&]() { if (!textBuf.empty()) { addText(DecodeEntities(textBuf)); textBuf.clear(); } };
    const size_t n = h.size();
    size_t i = 0;
    while (i < n) {
        if (h[i] != L'<') { textBuf += h[i++]; continue; }
        if (h.compare(i, 4, L"<!--") == 0) {
            const size_t e = h.find(L"-->", i + 4);
            i = e == std::wstring::npos ? n : e + 3;
            continue;
        }
        if (i + 1 < n && (h[i + 1] == L'!' || h[i + 1] == L'?')) {
            const size_t e = h.find(L'>', i);
            i = e == std::wstring::npos ? n : e + 1;
            continue;
        }
        size_t j = i + 1;
        bool closing = false;
        if (j < n && h[j] == L'/') { closing = true; ++j; }
        const size_t nameStart = j;
        while (j < n && (iswalnum(h[j]) || h[j] == L':' || h[j] == L'-')) ++j;
        const std::wstring name = ToLowerW(h.substr(nameStart, j - nameStart));
        if (name.empty()) { textBuf += h[i++]; continue; } // a '<' that isn't a tag
        flushText();

        std::vector<std::pair<std::wstring, std::wstring>> attrs;
        bool selfClose = false;
        while (j < n && h[j] != L'>') {
            if (iswspace(h[j])) { ++j; continue; }
            if (h[j] == L'/') { selfClose = true; ++j; continue; }
            const size_t a = j;
            while (j < n && !iswspace(h[j]) && h[j] != L'=' && h[j] != L'>' && h[j] != L'/') ++j;
            const std::wstring an = ToLowerW(h.substr(a, j - a));
            std::wstring av;
            while (j < n && iswspace(h[j])) ++j;
            if (j < n && h[j] == L'=') {
                ++j;
                while (j < n && iswspace(h[j])) ++j;
                if (j < n && (h[j] == L'"' || h[j] == L'\'')) {
                    const wchar_t q = h[j++];
                    const size_t s = j;
                    while (j < n && h[j] != q) ++j;
                    av = h.substr(s, j - s);
                    if (j < n) ++j;
                } else {
                    const size_t s = j;
                    while (j < n && !iswspace(h[j]) && h[j] != L'>') ++j;
                    av = h.substr(s, j - s);
                }
            }
            if (!an.empty()) attrs.emplace_back(an, DecodeEntities(av));
        }
        i = j < n ? j + 1 : n;

        if (!closing && (name == L"script" || name == L"style" || name == L"title" || name == L"head")) {
            const size_t e = ToLowerW(h.substr(i)).find(L"</" + name);
            if (e == std::wstring::npos) { i = n; continue; }
            const size_t gt = h.find(L'>', i + e);
            i = gt == std::wstring::npos ? n : gt + 1;
            continue;
        }
        const bool block = std::any_of(std::begin(kBlocks), std::end(kBlocks), [&](const wchar_t* b) { return name == b; });
        if (name == L"br") { addBreak(); continue; }
        if (name == L"hr") { blockBreak(); continue; }
        if (name == L"img") { if (!closing) addImage(attrs); continue; }
        if (std::any_of(std::begin(kVoid), std::end(kVoid), [&](const wchar_t* v) { return name == v; })) continue;

        if (closing) {
            for (size_t k = st.size(); k-- > 1;)
                if (st[k].tag == name) { st.resize(k); if (block) blockBreak(); break; }
            if ((name == L"td" || name == L"th") && !space) { addText(L" "); }
            continue;
        }

        if (block) blockBreak();
        El e = st.back();
        e.tag = name;
        e.list = -1;
        Fmt& f = e.fmt;
        auto attr = [&](const wchar_t* k) { for (const auto& a : attrs) if (a.first == k) return a.second; return std::wstring(); };
        if (name == L"b" || name == L"strong") f.b = true;
        else if (name == L"i" || name == L"em" || name == L"cite" || name == L"dfn" || name == L"var") f.i = true;
        else if (name == L"u" || name == L"ins") f.u = true;
        else if (name == L"s" || name == L"strike" || name == L"del") f.s = true;
        else if (name == L"a") {
            const std::wstring href = Trim(attr(L"href"));
            if (!href.empty() && href[0] != L'#' && !StartsWithI(href, L"javascript:")) f.link = href;
        }
        else if (name.size() == 2 && name[0] == L'h' && name[1] >= L'1' && name[1] <= L'6') {
            f.b = true;
            f.size = kHeading[name[1] - L'1'];
        }
        else if (name == L"code" || name == L"kbd" || name == L"samp" || name == L"tt") f.font = L"Consolas";
        else if (name == L"pre") { f.font = L"Consolas"; e.pre = true; }
        else if (name == L"mark") f.back = RGB(255, 255, 0);
        else if (name == L"center") e.align = Align::Center;
        else if (name == L"ul") e.list = 0;
        else if (name == L"ol") e.list = 1;
        else if (name == L"font") {
            const std::wstring face = attr(L"face");
            if (!face.empty()) f.font = CssFontFamily(face);
            COLORREF c;
            if (ParseCssColor(attr(L"color"), c)) f.color = c;
            static const int kSizes[] = { 16, 20, 24, 28, 36, 48, 72 }; // <font size=1..7>
            const int sz = _wtoi(attr(L"size").c_str());
            if (sz >= 1 && sz <= 7) f.size = kSizes[sz - 1];
        }
        const std::wstring al = ToLowerW(attr(L"align"));
        if (block && !al.empty()) {
            if (al == L"center") e.align = Align::Center;
            else if (al == L"right") e.align = Align::Right;
            else if (al == L"justify") e.align = Align::Justify;
            else if (al == L"left") e.align = Align::Left;
        }
        const std::wstring style = attr(L"style");
        if (!style.empty()) ApplyCss(style, f, e.align);

        if (name == L"li") { // bullet, or number in an <ol>
            std::wstring marker = L"\x2022 ";
            for (size_t k = st.size(); k-- > 1;)
                if (st[k].list >= 0) {
                    if (st[k].list > 0) marker = std::to_wstring(st[k].list++) + L". ";
                    break;
                }
            st.push_back(e);
            Fmt mf = e.fmt;
            mf.link.clear();
            startContent();
            AddText(para(), mf, marker);
            space = true; // the marker ends with a space
            continue;
        }
        if (!selfClose) st.push_back(e);
    }
    flushText();
    trimLineEnd();
    TrimDoc(doc);
    return doc;
}

// ============================================================================
// Model: one Clip per button
// ----------------------------------------------------------------------------
// In the JSON, pasteText holds the plain text (line breaks written as \n) plus
// optional %RTF:<base64>% and %HTML:<base64>% tokens (and %IMG:<base64>% in
// clips from older versions). In the editor a clip is either plain text or
// RTF (the Rich Edit box's own format); HTML and the plain-text version of a
// formatted clip are generated from the RTF when saving.
// ============================================================================
struct Clip {
    std::wstring label;   // buttonText
    std::wstring group;   // group (empty = Clippi's "General")
    std::wstring prompt;  // inputBoxTitle  (answer replaces %XXX%)
    std::wstring prompt2; // inputBoxTitle2 (answer replaces %YYY%)
    bool rich = false;    // true: rtf is the content; false: plain text
    std::wstring plain;   // plain text as shown in an edit box (CRLF line breaks)
    std::string rtf;      // rich: the RTF shown in the box
    bool contentEdited = false; // false: pasteText is saved exactly as loaded
    JsonValue original;   // the object as loaded, so unknown fields are kept
};

std::wstring GetJsonString(const JsonValue& obj, const wchar_t* key) {
    const JsonValue* v = obj.Find(key);
    return (v && v->type == JsonValue::String) ? v->str : std::wstring();
}

bool GetJsonBool(const JsonValue& obj, const wchar_t* key) {
    const JsonValue* v = obj.Find(key);
    return v && v->type == JsonValue::Bool && v->boolean;
}

// Stored text -> editor text: \n and \t sequences become real characters (unless the
// button has "rawText": true, whose text is literal), CRLF line breaks
std::wstring StoredToDisplay(std::wstring s, bool raw) {
    if (!raw) {
        ReplaceAll(s, L"\\n", L"\n");
        ReplaceAll(s, L"\\t", L"\t");
    }
    return LfToCrlf(s);
}

// Editor text -> stored text: line breaks and tabs written as \n and \t (Clippi's
// convention), or kept as real characters for a "rawText" button
std::wstring DisplayToStored(std::wstring s, bool raw) {
    s.erase(std::remove(s.begin(), s.end(), L'\r'), s.end());
    if (!raw) {
        ReplaceAll(s, L"\n", L"\\n");
        ReplaceAll(s, L"\t", L"\\t");
    }
    return s;
}

// True if the text has a literal \n or \t, which the \\n convention can't store
bool NeedsRawText(const std::wstring& text) {
    return text.find(L"\\n") != std::wstring::npos || text.find(L"\\t") != std::wstring::npos;
}

Clip ClipFromJson(const JsonValue& obj) {
    Clip c;
    c.original = obj;
    c.label   = GetJsonString(obj, L"buttonText");
    c.group   = GetJsonString(obj, L"group");
    c.prompt  = GetJsonString(obj, L"inputBoxTitle");
    c.prompt2 = GetJsonString(obj, L"inputBoxTitle2");
    const std::wstring raw = GetJsonString(obj, L"pasteText");
    const bool literal = GetJsonBool(obj, L"rawText");

    enum class Tok { Img, Rtf, Html };
    static const struct { const wchar_t* open; Tok kind; } kTokens[] = {
        { L"%IMG:", Tok::Img }, { L"%RTF:", Tok::Rtf }, { L"%HTML:", Tok::Html },
    };
    std::string rtf, html;
    Doc legacy;          // text + %IMG% pictures, for clips from older versions
    bool hasPicture = false;
    size_t pos = 0;
    for (;;) {
        size_t a = std::wstring::npos, b = std::wstring::npos, openLen = 0;
        Tok kind = Tok::Img;
        for (const auto& t : kTokens) {
            const size_t at = raw.find(t.open, pos);
            if (at == std::wstring::npos || at >= a) continue;
            const size_t len = wcslen(t.open);
            const size_t end = raw.find(L'%', at + len);
            if (end == std::wstring::npos) continue;
            a = at; b = end; openLen = len; kind = t.kind;
        }
        const std::wstring piece = StoredToDisplay(raw.substr(pos, a == std::wstring::npos ? std::wstring::npos : a - pos), literal);
        c.plain += piece;
        AppendPlainToDoc(legacy, piece);
        if (a == std::wstring::npos) break;

        const std::vector<unsigned char> data = Base64Decode(raw.substr(a + openLen, b - a - openLen));
        bool used = false;
        if (kind == Tok::Img) {
            Run r;
            r.kind = RunKind::Picture;
            if (PngSize(data, r.w, r.h)) {
                r.data = data;
                legacy.paras.back().runs.push_back(std::move(r));
                hasPicture = used = true;
            }
        } else if (!data.empty()) {
            std::string& dest = kind == Tok::Rtf ? rtf : html;
            if (dest.empty()) { dest.assign(data.begin(), data.end()); used = true; }
        }
        if (!used) { // keep unreadable data exactly as it was
            const std::wstring keep = raw.substr(a, b - a + 1);
            c.plain += keep;
            AppendPlainToDoc(legacy, keep);
        }
        pos = b + 1;
    }

    if (!rtf.empty()) {
        c.rich = true;
        c.rtf = rtf;
    } else if (!html.empty()) {
        Doc d = HtmlToDoc(html);
        c.rich = true;
        c.rtf = DocToRtf(d);
    } else if (hasPicture) {
        TrimDoc(legacy);
        c.rich = true;
        c.rtf = DocToRtf(legacy);
    }
    return c;
}

JsonValue JsonBool(bool b) {
    JsonValue v;
    v.type = JsonValue::Bool;
    v.boolean = b;
    return v;
}

JsonValue ClipToJson(const Clip& c) {
    std::wstring paste;
    bool raw = false;
    if (!c.contentEdited) {          // unchanged: exactly as loaded
        paste = GetJsonString(c.original, L"pasteText");
        raw = GetJsonBool(c.original, L"rawText");
    } else {
        raw = NeedsRawText(c.plain);
        paste = DisplayToStored(c.plain, raw);
        if (c.rich) {
            const std::string html = DocToHtml(RtfToDoc(c.rtf));
            paste += L"%RTF:" + Base64Encode(std::vector<unsigned char>(c.rtf.begin(), c.rtf.end())) + L"%";
            paste += L"%HTML:" + Base64Encode(std::vector<unsigned char>(html.begin(), html.end())) + L"%";
        }
    }

    // Clippi's fields first ("group" only if set), then any other fields from the original object
    JsonValue obj;
    obj.type = JsonValue::Object;
    const std::wstring group = Trim(c.group);
    if (!group.empty()) obj.object.emplace_back(L"group", JsonString(group));
    obj.object.emplace_back(L"buttonText", JsonString(c.label));
    obj.object.emplace_back(L"pasteText", JsonString(paste));
    obj.object.emplace_back(L"inputBoxTitle", JsonString(c.prompt));
    if (!Trim(c.prompt2).empty()) obj.object.emplace_back(L"inputBoxTitle2", JsonString(c.prompt2));
    if (raw) obj.object.emplace_back(L"rawText", JsonBool(true));
    for (const auto& kv : c.original.object)
        if (kv.first != L"buttonText" && kv.first != L"pasteText" && kv.first != L"inputBoxTitle" &&
            kv.first != L"group" && kv.first != L"inputBoxTitle2" && kv.first != L"rawText")
            obj.object.push_back(kv);
    return obj;
}

// A placeholder that has different formatting inside it (e.g. half of %XXX%
// bold) can't be filled in by Clippi in the formatted versions.
bool HasSplitPlaceholder(const Clip& c) {
    if (!c.rich) return false;
    for (const wchar_t* ph : { L"%XXX%", L"%YYY%" })
        if (c.plain.find(ph) != std::wstring::npos && c.rtf.find(ToUtf8(ph)) == std::string::npos) return true;
    return false;
}

// ============================================================================
// Application state
// ============================================================================
struct AppState {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    HWND box = nullptr;         // the Rich Edit "Text to paste" box
    HFONT font = nullptr;
    HFONT fmtFonts[4] = {};     // bold / italic / underline / strikeout fonts for B I U S
    int dpi = 96;
    std::wstring path;          // JSON file being edited
    std::vector<Clip> clips;
    int cur = -1;               // selected clip, -1 = none
    bool dirty = false;         // unsaved changes
    bool loading = false;       // true while filling controls (ignore change notifications)
    bool boxDirty = false;      // the box changed since it was loaded or committed
    COLORREF customColors[16] = {};
} g;

int S(int v) { return MulDiv(v, g.dpi, 96); }
HWND Ctl(int id) { return GetDlgItem(g.hwnd, id); }

std::wstring GetText(HWND h) {
    const int len = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(h, &s[0], len + 1);
    s.resize(static_cast<size_t>(len));
    return s;
}

void SetStatus(const std::wstring& text) { SetWindowTextW(Ctl(IDC_STATUS), text.c_str()); }

void UpdateTitle() {
    const std::wstring title = (g.dirty ? L"*" : L"") + FileNameOf(g.path) + L" - " + kAppTitle;
    SetWindowTextW(g.hwnd, title.c_str());
}

void SetDirty() {
    if (!g.dirty) { g.dirty = true; UpdateTitle(); }
}

Clip& Current() { return g.clips[static_cast<size_t>(g.cur)]; }

// Group name as Clippi uses it
std::wstring GroupOf(const Clip& c) {
    const std::wstring name = Trim(c.group);
    return name.empty() ? std::wstring(L"General") : name;
}

// Distinct groups in order of first appearance (the order Clippi lists them)
std::vector<std::wstring> AllGroups() {
    std::vector<std::wstring> out;
    for (const Clip& c : g.clips) {
        const std::wstring name = GroupOf(c);
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    return out;
}

std::wstring ListLabel(const Clip& c) {
    std::wstring s = GroupOf(c) + L":  " + (Trim(c.label).empty() ? L"(no label)" : c.label);
    if (c.rich) s += L"   [formatted]";
    const int asks = (Trim(c.prompt).empty() ? 0 : 1) + (Trim(c.prompt2).empty() ? 0 : 1);
    if (asks == 1) s += L"   [asks]";
    else if (asks == 2) s += L"   [asks 2]";
    return s;
}

// ============================================================================
// The Rich Edit box
// ============================================================================
DWORD CALLBACK StreamOutCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* written) {
    reinterpret_cast<std::string*>(cookie)->append(reinterpret_cast<const char*>(buf), static_cast<size_t>(cb));
    *written = cb;
    return 0;
}

std::string BoxRtf() {
    std::string rtf;
    EDITSTREAM es{};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&rtf);
    es.pfnCallback = StreamOutCallback;
    SendMessageW(g.box, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
    return rtf;
}

// The box's visible text (CRLF line breaks, no pictures, no hidden link addresses)
std::wstring BoxPlainText() {
    GETTEXTLENGTHEX gl{ GTL_USECRLF | GTL_PRECISE | GTL_NUMCHARS, 1200 };
    const LRESULT len = SendMessageW(g.box, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gl), 0);
    if (len <= 0) return std::wstring();
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    GETTEXTEX gt{};
    gt.cb = static_cast<DWORD>((len + 1) * sizeof(wchar_t));
    gt.flags = GT_USECRLF | GT_NOHIDDENTEXT;
    gt.codepage = 1200;
    const LRESULT got = SendMessageW(g.box, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(&s[0]));
    s.resize(static_cast<size_t>(std::max<LRESULT>(got, 0)));
    s.erase(std::remove(s.begin(), s.end(), L'\xFFFC'), s.end()); // embedded objects
    return s;
}

// Replaces the whole box (selection = false) or the selection with RTF
void SetBoxRtf(const std::string& rtf, bool selection) {
    SETTEXTEX st{ static_cast<DWORD>(selection ? (ST_SELECTION | ST_KEEPUNDO) : ST_DEFAULT), CP_ACP };
    SendMessageW(g.box, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&st), reinterpret_cast<LPARAM>(rtf.c_str()));
}

void SetBoxPlain(const std::wstring& text) {
    SETTEXTEX st{ ST_DEFAULT, 1200 };
    SendMessageW(g.box, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&st), reinterpret_cast<LPARAM>(text.c_str()));
}

// Calibri 11 pt, automatic color: what Clippi's plain clips look like when pasted as rich text
CHARFORMAT2W DefaultCharFormat() {
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC |
                CFM_UNDERLINE | CFM_STRIKEOUT | CFM_CHARSET;
    cf.dwEffects = CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR;
    cf.yHeight = kDefaultHalfPoints * 10; // twips
    cf.bCharSet = DEFAULT_CHARSET;
    wcsncpy(cf.szFaceName, kDefaultFace, LF_FACESIZE - 1);
    return cf;
}

CHARFORMAT2W SelectionFormat() {
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    SendMessageW(g.box, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    return cf;
}

// Shows a clip in the box (nullptr = empty box)
void LoadBox(const Clip* c) {
    g.loading = true;
    SetBoxPlain(L"");
    CHARFORMAT2W cf = DefaultCharFormat();
    SendMessageW(g.box, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(g.box, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    if (c) {
        if (c->rich) SetBoxRtf(c->rtf, false);
        else SetBoxPlain(c->plain);
    }
    SendMessageW(g.box, EM_SETSEL, 0, 0);
    SendMessageW(g.box, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(g.box, EM_SETMODIFY, FALSE, 0);
    g.boxDirty = false;
    g.loading = false;
}

// Copies the box into the selected clip if it changed: plain text if it has no
// formatting, otherwise its RTF plus a generated plain-text version.
void CommitBox() {
    if (g.cur < 0) return;
    if (!g.boxDirty && !SendMessageW(g.box, EM_GETMODIFY, 0, 0)) return;
    Clip& c = Current();
    const std::string rtf = BoxRtf();
    const Doc d = RtfToDoc(rtf);
    if (IsPlainDoc(d)) {
        c.rich = false;
        c.rtf.clear();
        c.plain = BoxPlainText();
    } else {
        c.rich = true;
        c.rtf = rtf;
        c.plain = LfToCrlf(DocToPlain(d));
    }
    c.contentEdited = true;
    g.boxDirty = false;
    SendMessageW(g.box, EM_SETMODIFY, FALSE, 0);
    SetDirty();
}

void MarkBoxChanged() {
    if (g.loading || g.cur < 0) return;
    g.boxDirty = true;
    SetDirty();
}

// Shows the selection's bold / italic / underline / strikeout on the B I U S buttons
void UpdateFormatButtons() {
    const CHARFORMAT2W cf = SelectionFormat();
    auto set = [&](int id, DWORD mask, DWORD effect) {
        const bool on = (cf.dwMask & mask) && (cf.dwEffects & effect);
        SendMessageW(Ctl(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    set(IDC_FMT_BOLD, CFM_BOLD, CFE_BOLD);
    set(IDC_FMT_ITALIC, CFM_ITALIC, CFE_ITALIC);
    set(IDC_FMT_UNDERLINE, CFM_UNDERLINE, CFE_UNDERLINE);
    set(IDC_FMT_STRIKE, CFM_STRIKEOUT, CFE_STRIKEOUT);
}

void AfterFormat() {
    MarkBoxChanged();
    UpdateFormatButtons();
    SetFocus(g.box);
}

void ApplyFormat(CHARFORMAT2W& cf) {
    SendMessageW(g.box, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    AfterFormat();
}

void ToggleEffect(DWORD mask, DWORD effect) {
    const CHARFORMAT2W sel = SelectionFormat();
    const bool on = (sel.dwMask & mask) && (sel.dwEffects & effect);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = mask;
    cf.dwEffects = on ? 0 : effect;
    ApplyFormat(cf);
}

void InsertRtf(const std::string& rtf) {
    SetBoxRtf(rtf, true);
    MarkBoxChanged();
    UpdateFormatButtons();
    SetFocus(g.box);
}

void InsertIntoText(const std::wstring& s) {
    SendMessageW(g.box, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(s.c_str())); // triggers EN_CHANGE
    SetFocus(g.box);
}

// Inserts a PNG picture at the cursor, shown at its pixel size
void InsertPicture(const std::vector<unsigned char>& png) {
    Run r;
    r.kind = RunKind::Picture;
    if (!PngSize(png, r.w, r.h)) return;
    r.data = png;
    InsertRtf("{\\rtf1 " + RtfPicture(r) + "}");
    SetStatus(L"Picture added: " + std::to_wstring(r.w) + L" x " + std::to_wstring(r.h) + L" pixels, " +
              FormatSize(png.size()) + L".");
}

// Pastes what the Rich Edit box can't paste well by itself:
//   HTML only (Teams, browsers) -> converted to formatted text
//   a picture (screenshot)      -> PNG
// Returns false to let the box paste normally (RTF from Word/Outlook, plain text).
bool PasteSpecial() {
    static const UINT cfRtf  = RegisterClipboardFormatW(L"Rich Text Format");
    static const UINT cfHtml = RegisterClipboardFormatW(L"HTML Format");
    if (IsClipboardFormatAvailable(cfRtf)) return false;

    std::string html;
    std::vector<unsigned char> dib;
    if (OpenClipboard(g.hwnd)) {
        auto copy = [](UINT format, auto& out) {
            HANDLE h = GetClipboardData(format);
            if (!h) return;
            const auto* p = static_cast<const unsigned char*>(GlobalLock(h));
            if (!p) return;
            out.assign(p, p + GlobalSize(h));
            GlobalUnlock(h);
        };
        if (IsClipboardFormatAvailable(cfHtml)) copy(cfHtml, html);
        if (IsClipboardFormatAvailable(CF_DIB)) copy(CF_DIB, dib);
        CloseClipboard();
    }
    if (!html.empty()) {
        html.resize(strnlen(html.c_str(), html.size()));
        int skipped = 0;
        Doc d = HtmlToDoc(CfHtmlFragment(html), &skipped);
        if (DocHasContent(d)) {
            InsertRtf(DocToRtf(d, true));
            SetStatus(skipped ? L"Pasted. " + std::to_wstring(skipped) +
                                L" picture(s) linked to a web address or file were left out; copy them as pictures instead."
                              : L"Pasted with formatting.");
            return true;
        }
    }
    if (!dib.empty()) {
        std::vector<unsigned char> png;
        if (DibToPng(dib, 0, 0, png)) { InsertPicture(png); return true; }
    }
    return false;
}

void PastePlain() {
    SendMessageW(g.box, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
    SetFocus(g.box);
}

void ShowBoxMenu(HWND box, LPARAM lp) {
    CHARRANGE cr{};
    SendMessageW(box, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    const bool sel = cr.cpMax > cr.cpMin;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, SendMessageW(box, EM_CANUNDO, 0, 0) ? MF_STRING : MF_GRAYED, 1, L"&Undo");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, sel ? MF_STRING : MF_GRAYED, 2, L"Cu&t");
    AppendMenuW(m, sel ? MF_STRING : MF_GRAYED, 3, L"&Copy");
    AppendMenuW(m, MF_STRING, 4, L"&Paste");
    AppendMenuW(m, IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_STRING : MF_GRAYED, 5,
                L"Paste as plain te&xt\tCtrl+Shift+V");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 6, L"Select &All");
    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    if (pt.x == -1 && pt.y == -1) { GetCaretPos(&pt); ClientToScreen(box, &pt); }
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.hwnd, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case 1: SendMessageW(box, EM_UNDO, 0, 0); break;
    case 2: SendMessageW(box, WM_CUT, 0, 0); break;
    case 3: SendMessageW(box, WM_COPY, 0, 0); break;
    case 4: SendMessageW(box, WM_PASTE, 0, 0); break;
    case 5: PastePlain(); break;
    case 6: SendMessageW(box, EM_SETSEL, 0, -1); break;
    }
}

// Subclass of the box: special paste, plain paste, right-click menu
LRESULT CALLBACK BoxProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_KEYDOWN: {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0,
                   alt = GetKeyState(VK_MENU) < 0;
        if (!alt && ((ctrl && wp == 'V') || (shift && !ctrl && wp == VK_INSERT))) {
            if (ctrl && shift) { PastePlain(); return 0; }
            if (PasteSpecial()) return 0;
        }
        break;
    }
    case WM_PASTE:
        if (PasteSpecial()) return 0;
        break;
    case WM_CONTEXTMENU:
        ShowBoxMenu(h, lp);
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, BoxProc, 1);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ============================================================================
// A small text-input dialog (template built in memory)
// ============================================================================
class DialogTemplate {
public:
    DialogTemplate(const wchar_t* title, short cx, short cy) {
        PutDword(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        PutDword(0);
        countIndex_ = buf_.size();
        buf_.push_back(0);
        PutShort(0); PutShort(0); PutShort(cx); PutShort(cy);
        buf_.push_back(0);  // no menu
        buf_.push_back(0);  // default class
        PutString(title);
        buf_.push_back(9);  // font size
        PutString(L"Segoe UI");
    }
    void AddItem(WORD id, WORD classAtom, DWORD style, short x, short y, short cx, short cy, const wchar_t* text) {
        if (buf_.size() % 2) buf_.push_back(0);
        PutDword(style | WS_CHILD | WS_VISIBLE);
        PutDword(0);
        PutShort(x); PutShort(y); PutShort(cx); PutShort(cy);
        buf_.push_back(id);
        buf_.push_back(0xFFFF);
        buf_.push_back(classAtom); // 0x80 button, 0x81 edit, 0x82 static
        PutString(text);
        buf_.push_back(0);
        ++buf_[countIndex_];
    }
    LPCDLGTEMPLATEW Get() const { return reinterpret_cast<LPCDLGTEMPLATEW>(buf_.data()); }
private:
    std::vector<WORD> buf_;
    size_t countIndex_ = 0;
    void PutDword(DWORD d) { buf_.push_back(LOWORD(d)); buf_.push_back(HIWORD(d)); }
    void PutShort(short s) { buf_.push_back(static_cast<WORD>(s)); }
    void PutString(const wchar_t* s) { while (*s) buf_.push_back(*s++); buf_.push_back(0); }
};

struct AskData { std::wstring title, label, value; };

INT_PTR CALLBACK AskProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* d = reinterpret_cast<AskData*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetWindowTextW(dlg, d->title.c_str());
        SetDlgItemTextW(dlg, IDC_ASK_LABEL, d->label.c_str());
        HWND edit = GetDlgItem(dlg, IDC_ASK_EDIT);
        SetWindowTextW(edit, d->value.c_str());
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto* d = reinterpret_cast<AskData*>(GetWindowLongPtrW(dlg, DWLP_USER));
            d->value = Trim(GetText(GetDlgItem(dlg, IDC_ASK_EDIT)));
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

bool AskText(const std::wstring& title, const std::wstring& label, std::wstring& value) {
    AskData d{ title, label, value };
    DialogTemplate t(L"", 280, 62);
    t.AddItem(IDC_ASK_LABEL, 0x0082, SS_LEFT | SS_NOPREFIX, 7, 7, 266, 9, L"");
    t.AddItem(IDC_ASK_EDIT, 0x0081, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 7, 19, 266, 14, L"");
    t.AddItem(IDOK, 0x0080, WS_TABSTOP | BS_DEFPUSHBUTTON, 169, 41, 50, 14, L"OK");
    t.AddItem(IDCANCEL, 0x0080, WS_TABSTOP | BS_PUSHBUTTON, 223, 41, 50, 14, L"Cancel");
    if (DialogBoxIndirectParamW(g.inst, t.Get(), g.hwnd, AskProc, reinterpret_cast<LPARAM>(&d)) != IDOK) return false;
    value = d.value;
    return true;
}

// ============================================================================
// Toolbar actions
// ============================================================================
void ChooseFontForSelection() {
    const CHARFORMAT2W sel = SelectionFormat();
    LOGFONTW lf{};
    wcsncpy(lf.lfFaceName, sel.szFaceName, LF_FACESIZE - 1);
    HDC dc = GetDC(g.hwnd);
    lf.lfHeight = -MulDiv(sel.yHeight > 0 ? sel.yHeight : kDefaultHalfPoints * 10, GetDeviceCaps(dc, LOGPIXELSY), 1440);
    ReleaseDC(g.hwnd, dc);
    lf.lfWeight = (sel.dwEffects & CFE_BOLD) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (sel.dwEffects & CFE_ITALIC) ? TRUE : FALSE;
    lf.lfUnderline = (sel.dwEffects & CFE_UNDERLINE) ? TRUE : FALSE;
    lf.lfStrikeOut = (sel.dwEffects & CFE_STRIKEOUT) ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;

    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = g.hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;
    cf.rgbColors = (sel.dwEffects & CFE_AUTOCOLOR) ? RGB(0, 0, 0) : sel.crTextColor;
    if (!ChooseFontW(&cf)) return;

    CHARFORMAT2W n{};
    n.cbSize = sizeof(n);
    n.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_COLOR | CFM_CHARSET;
    wcsncpy(n.szFaceName, lf.lfFaceName, LF_FACESIZE - 1);
    n.bCharSet = lf.lfCharSet;
    n.yHeight = cf.iPointSize * 2; // tenths of a point -> twips
    if (lf.lfWeight >= FW_BOLD) n.dwEffects |= CFE_BOLD;
    if (lf.lfItalic) n.dwEffects |= CFE_ITALIC;
    if (lf.lfUnderline) n.dwEffects |= CFE_UNDERLINE;
    if (lf.lfStrikeOut) n.dwEffects |= CFE_STRIKEOUT;
    if (cf.rgbColors == RGB(0, 0, 0)) n.dwEffects |= CFE_AUTOCOLOR; // black = automatic (works in dark mode)
    else n.crTextColor = cf.rgbColors;
    ApplyFormat(n);
}

void ChooseColorForSelection() {
    const CHARFORMAT2W sel = SelectionFormat();
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = g.hwnd;
    cc.rgbResult = (sel.dwEffects & CFE_AUTOCOLOR) ? RGB(0, 0, 0) : sel.crTextColor;
    cc.lpCustColors = g.customColors;
    cc.Flags = CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&cc)) return;
    CHARFORMAT2W n{};
    n.cbSize = sizeof(n);
    n.dwMask = CFM_COLOR;
    if (cc.rgbResult == RGB(0, 0, 0)) n.dwEffects = CFE_AUTOCOLOR;
    else n.crTextColor = cc.rgbResult;
    ApplyFormat(n);
}

void ClearFormat() {
    CHARFORMAT2W cf = DefaultCharFormat();
    ApplyFormat(cf);
    SetStatus(L"Formatting cleared from the selection.");
}

void InsertLink() {
    CHARRANGE cr{};
    SendMessageW(g.box, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&cr));
    std::wstring sel;
    if (cr.cpMax > cr.cpMin) {
        sel.resize(static_cast<size_t>(cr.cpMax - cr.cpMin) + 1);
        const LRESULT n = SendMessageW(g.box, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(&sel[0]));
        sel.resize(static_cast<size_t>(std::max<LRESULT>(n, 0)));
        sel.erase(std::remove(sel.begin(), sel.end(), L'\xFFFC'), sel.end());
        std::replace(sel.begin(), sel.end(), L'\r', L' ');
        std::replace(sel.begin(), sel.end(), L'\n', L' ');
    }
    const std::wstring trimmed = Trim(sel);
    const bool selIsUrl = StartsWithI(trimmed, L"http://") || StartsWithI(trimmed, L"https://") ||
                          StartsWithI(trimmed, L"www.") || StartsWithI(trimmed, L"mailto:");
    std::wstring url = selIsUrl ? trimmed : L"https://";
    if (!AskText(L"Insert link", trimmed.empty() || selIsUrl
                                     ? L"Web address (the link shows this address):"
                                     : L"Web address for \"" + trimmed + L"\":", url)) return;
    if (url.empty() || url == L"https://") return;
    if (StartsWithI(url, L"www.")) url = L"https://" + url;

    // Same look as the selection, plus the link
    const CHARFORMAT2W cf = SelectionFormat();
    Fmt f;
    f.font = cf.szFaceName;
    if (cf.yHeight > 0) f.size = cf.yHeight / 10;
    f.b = (cf.dwEffects & CFE_BOLD) != 0;
    f.i = (cf.dwEffects & CFE_ITALIC) != 0;
    f.link = url;
    Doc d;
    AddText(d.paras.back(), f, trimmed.empty() ? url : sel);
    InsertRtf(DocToRtf(d, true));
    SetStatus(L"Link added.");
}

bool AskOpenFile(const wchar_t* title, const wchar_t* filter, std::wstring& path) {
    wchar_t buf[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = ARRAYSIZE(buf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = buf;
    return true;
}

// A file name made from a button label (characters Windows doesn't allow become _)
std::wstring SafeFileName(std::wstring name) {
    for (wchar_t& c : name)
        if (wcschr(L"\\/:*?\"<>|", c) || c < 32) c = L'_';
    name = Trim(name);
    return name.empty() ? std::wstring(L"Clip") : name;
}

bool AskSaveFile(const wchar_t* filter, const wchar_t* ext, const std::wstring& name, std::wstring& path) {
    wchar_t buf[MAX_PATH * 2] = L"";
    name.copy(buf, ARRAYSIZE(buf) - 1); // buf is zero-filled, so it stays terminated
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = ext;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = ARRAYSIZE(buf);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buf;
    return true;
}

void AddPictureFromFile() {
    std::wstring path;
    if (!AskOpenFile(L"Add a picture",
                     L"Pictures (*.png;*.jpg;*.jpeg;*.gif;*.bmp)\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0All files (*.*)\0*.*\0",
                     path)) return;
    std::vector<unsigned char> png;
    std::wstring error;
    if (!ReadFileBytes(path, png, error)) { MessageBoxW(g.hwnd, error.c_str(), kAppTitle, MB_OK | MB_ICONERROR); return; }
    if (!IsPng(png) && !ConvertFileToPng(path, png)) {
        MessageBoxW(g.hwnd, (L"This file could not be read as a picture:\n" + path).c_str(), kAppTitle, MB_OK | MB_ICONERROR);
        return;
    }
    int w = 0, h = 0;
    if (!PngSize(png, w, h)) { MessageBoxW(g.hwnd, L"This picture could not be read.", kAppTitle, MB_OK | MB_ICONERROR); return; }
    if (png.size() > 100 * 1024) {
        const std::wstring msg = L"This picture is " + FormatSize(png.size()) +
            L". Large pictures make the button file big and slow to paste; a few KB is usual for a logo.\n\nAdd it anyway?";
        if (MessageBoxW(g.hwnd, msg.c_str(), kAppTitle, MB_YESNO | MB_ICONWARNING) != IDYES) return;
    }
    InsertPicture(png);
}

// Inserts an .rtf or .html file at the cursor
void InsertFromFile() {
    std::wstring path;
    if (!AskOpenFile(L"Insert formatted text from a file",
                     L"Formatted text (*.rtf;*.html;*.htm)\0*.rtf;*.html;*.htm\0All files (*.*)\0*.*\0", path)) return;
    std::vector<unsigned char> bytes;
    std::wstring error;
    if (!ReadFileBytes(path, bytes, error)) { MessageBoxW(g.hwnd, error.c_str(), kAppTitle, MB_OK | MB_ICONERROR); return; }
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) bytes.erase(bytes.begin(), bytes.begin() + 3);
    if (bytes.size() >= 5 && memcmp(bytes.data(), "{\\rtf", 5) == 0) {
        InsertRtf(std::string(bytes.begin(), bytes.end()));
        SetStatus(L"Inserted " + FileNameOf(path) + L".");
        return;
    }
    int skipped = 0;
    const Doc d = HtmlToDoc(ExtractHtmlFragment(ToUtf8(BytesToText(bytes))), &skipped);
    if (!DocHasContent(d)) {
        MessageBoxW(g.hwnd, L"This file has no text or pictures to insert. Use an .rtf file (Word: Save As > "
                            L"Rich Text Format) or an .html file.", kAppTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }
    InsertRtf(DocToRtf(d, true));
    SetStatus(L"Inserted " + FileNameOf(path) + L"." +
              (skipped ? L" " + std::to_wstring(skipped) + L" picture(s) linked to a file or web address were left out." : L""));
}

void ExportFormatted(bool html) {
    if (g.cur < 0) return;
    CommitBox();
    const std::string rtf = BoxRtf();
    const std::wstring name = SafeFileName(Current().label) + (html ? L".html" : L".rtf");
    std::wstring path;
    if (!(html ? AskSaveFile(L"Web page (*.html)\0*.html\0", L"html", name, path)
               : AskSaveFile(L"Rich Text (*.rtf)\0*.rtf\0", L"rtf", name, path))) return;
    std::string out = rtf;
    if (html) {
        wchar_t prev = L'\n';
        out = "<!DOCTYPE html>\r\n<html><head><meta charset=\"utf-8\"><title>" + ToUtf8(HtmlText(Current().label, prev)) +
              "</title></head><body>\r\n" + DocToHtml(RtfToDoc(rtf)) + "\r\n</body></html>\r\n";
    }
    if (WriteFileBytes(path, out.data(), out.size())) SetStatus(L"Saved " + path);
    else MessageBoxW(g.hwnd, (L"Could not write:\n" + path).c_str(), kAppTitle, MB_OK | MB_ICONERROR);
}

// ============================================================================
// Filling controls from the model
// ============================================================================
void UpdateButtonStates() {
    const bool has = g.cur >= 0;
    const int n = static_cast<int>(g.clips.size());
    for (int id : { IDC_LABEL, IDC_GROUP, IDC_PROMPT, IDC_PROMPT2, IDC_TEXT, IDC_INSERT_XXX, IDC_INSERT_YYY,
                    IDC_FMT_BOLD, IDC_FMT_ITALIC, IDC_FMT_UNDERLINE, IDC_FMT_STRIKE, IDC_FMT_FONT,
                    IDC_FMT_COLOR, IDC_FMT_LINK, IDC_FMT_PICTURE, IDC_FMT_CLEAR,
                    IDC_INSERT_FILE, IDC_EXPORT_RTF, IDC_EXPORT_HTML, IDC_DUP, IDC_DEL })
        EnableWindow(Ctl(id), has);
    EnableWindow(Ctl(IDC_UP), has && g.cur > 0);
    EnableWindow(Ctl(IDC_DOWN), has && g.cur < n - 1);
}

void ShowClip(int index) {
    g.cur = index;
    g.loading = true;
    const Clip* c = index >= 0 ? &g.clips[static_cast<size_t>(index)] : nullptr;
    SetWindowTextW(Ctl(IDC_LABEL), c ? c->label.c_str() : L"");
    SetWindowTextW(Ctl(IDC_GROUP), c ? c->group.c_str() : L"");
    SetWindowTextW(Ctl(IDC_PROMPT), c ? c->prompt.c_str() : L"");
    SetWindowTextW(Ctl(IDC_PROMPT2), c ? c->prompt2.c_str() : L"");
    g.loading = false;
    LoadBox(c);
    UpdateFormatButtons();
    UpdateButtonStates();
}

void RefreshList(int select) {
    HWND list = Ctl(IDC_LIST);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const Clip& c : g.clips)
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ListLabel(c).c_str()));
    if (select >= static_cast<int>(g.clips.size())) select = static_cast<int>(g.clips.size()) - 1;
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    ShowClip(select);
}

// Re-draws one list entry after its label, prompts or formatting changed (keeps the selection)
void RefreshListItem(int index) {
    if (index < 0 || index >= static_cast<int>(g.clips.size())) return;
    HWND list = Ctl(IDC_LIST);
    const LRESULT sel = SendMessageW(list, LB_GETCURSEL, 0, 0);
    SendMessageW(list, LB_DELETESTRING, static_cast<WPARAM>(index), 0);
    SendMessageW(list, LB_INSERTSTRING, static_cast<WPARAM>(index),
                 reinterpret_cast<LPARAM>(ListLabel(g.clips[static_cast<size_t>(index)]).c_str()));
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
}

// ============================================================================
// Loading and saving
// ============================================================================

// Returns false if the user chose to quit after a load error.
bool LoadFile(const std::wstring& path) {
    g.path = path;
    g.clips.clear();
    g.dirty = false;
    if (!FileExists(path)) {
        SetStatus(L"New file. It will be created when you save: " + path);
        return true;
    }
    std::vector<unsigned char> bytes;
    std::wstring error;
    JsonValue root;
    if (ReadFileBytes(path, bytes, error)) {
        try {
            root = JsonParser(BytesToText(bytes)).Parse();
            if (root.type != JsonValue::Array)
                error = L"The file must contain a JSON array [ ... ] of buttons.";
        } catch (const JsonError& e) {
            error = L"The file has a JSON error:\n" + e.message;
        }
    }
    if (!error.empty()) {
        const std::wstring msg = error + L"\n\n" + path +
            L"\n\nStart with an empty list? The file is only changed when you save, "
            L"and a backup copy (.bak) is made first.\n\nChoose No to close the editor.";
        return MessageBoxW(g.hwnd, msg.c_str(), kAppTitle, MB_YESNO | MB_ICONWARNING) == IDYES;
    }
    for (const JsonValue& item : root.array)
        if (item.type == JsonValue::Object) g.clips.push_back(ClipFromJson(item));
    const size_t groups = AllGroups().size();
    SetStatus(L"Loaded " + std::to_wstring(g.clips.size()) + L" buttons in " + std::to_wstring(groups) +
              (groups == 1 ? L" group from " : L" groups from ") + path);
    return true;
}

bool SaveFile() {
    CommitBox();
    RefreshListItem(g.cur);
    for (size_t i = 0; i < g.clips.size(); ++i) {
        if (Trim(g.clips[i].label).empty()) {
            SendMessageW(Ctl(IDC_LIST), LB_SETCURSEL, i, 0);
            ShowClip(static_cast<int>(i));
            MessageBoxW(g.hwnd, L"Every button needs a label. Please type one for the selected button.",
                        kAppTitle, MB_OK | MB_ICONINFORMATION);
            SetFocus(Ctl(IDC_LABEL));
            return false;
        }
    }

    JsonValue root;
    root.type = JsonValue::Array;
    for (const Clip& c : g.clips) root.array.push_back(ClipToJson(c));
    std::wstring json;
    JsonWrite(json, root, 0);
    json += L"\n";
    const std::string bytes = ToUtf8(json);

    const size_t slash = g.path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        SHCreateDirectoryExW(nullptr, g.path.substr(0, slash).c_str(), nullptr); // ok if it exists

    // Write to a temporary file first, keep a backup, then replace the original
    const std::wstring tmp = g.path + L".tmp";
    if (!WriteFileBytes(tmp, bytes.data(), bytes.size())) {
        MessageBoxW(g.hwnd, (L"Could not write:\n" + tmp).c_str(), kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    if (FileExists(g.path)) CopyFileW(g.path.c_str(), (g.path + L".bak").c_str(), FALSE);
    if (!MoveFileExW(tmp.c_str(), g.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        MessageBoxW(g.hwnd, (L"Could not save:\n" + g.path + L"\n\nIs the file read-only or open in another program?").c_str(),
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    g.dirty = false;
    UpdateTitle();
    std::wstring status = L"Saved " + std::to_wstring(g.clips.size()) + L" buttons. Previous version kept as " +
                          FileNameOf(g.path) + L".bak";
    for (const Clip& c : g.clips)
        if (HasSplitPlaceholder(c)) {
            status = L"Saved. Check \"" + c.label + L"\": %XXX% or %YYY% has mixed formatting inside it, so Clippi "
                     L"can't fill it in the formatted version. Select it and format it all the same way.";
            break;
        }
    SetStatus(status);
    return true;
}

// Asks about unsaved changes. Returns false if the user cancelled.
bool ConfirmDiscard() {
    CommitBox();
    if (!g.dirty) return true;
    switch (MessageBoxW(g.hwnd, L"Save your changes to the buttons?", kAppTitle,
                        MB_YESNOCANCEL | MB_ICONQUESTION)) {
    case IDYES: return SaveFile();
    case IDNO:  return true;
    default:    return false;
    }
}

// ============================================================================
// Button list actions
// ============================================================================
void AddClip(bool duplicate) {
    CommitBox();
    Clip c;
    if (duplicate && g.cur >= 0) {
        c = g.clips[static_cast<size_t>(g.cur)];
        c.label += L" (copy)";
    } else {
        c.label = L"New button";
        if (g.cur >= 0) c.group = g.clips[static_cast<size_t>(g.cur)].group; // same group as the selection
        c.original.type = JsonValue::Object;
        c.contentEdited = true;
    }
    const int at = g.cur >= 0 ? g.cur + 1 : static_cast<int>(g.clips.size());
    g.clips.insert(g.clips.begin() + at, std::move(c));
    SetDirty();
    RefreshList(at);
    SetFocus(Ctl(IDC_LABEL));
    SendMessageW(Ctl(IDC_LABEL), EM_SETSEL, 0, -1);
    SetStatus(duplicate ? L"Button duplicated." : L"Button added. Type its label and the text to paste.");
}

void DeleteClip() {
    if (g.cur < 0) return;
    const std::wstring msg = L"Delete the button \"" + g.clips[static_cast<size_t>(g.cur)].label + L"\"?";
    if (MessageBoxW(g.hwnd, msg.c_str(), kAppTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g.boxDirty = false;
    g.clips.erase(g.clips.begin() + g.cur);
    SetDirty();
    RefreshList(std::min(g.cur, static_cast<int>(g.clips.size()) - 1));
    SetStatus(L"Button deleted.");
}

void MoveClip(int delta) {
    const int to = g.cur + delta;
    if (g.cur < 0 || to < 0 || to >= static_cast<int>(g.clips.size())) return;
    CommitBox();
    std::swap(g.clips[static_cast<size_t>(g.cur)], g.clips[static_cast<size_t>(to)]);
    SetDirty();
    RefreshList(to);
    SetFocus(Ctl(delta < 0 ? IDC_UP : IDC_DOWN));
    if (!IsWindowEnabled(GetFocus())) SetFocus(Ctl(IDC_LIST));
}

// ============================================================================
// Window: controls, layout, messages
// ============================================================================
HWND MakeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, g.hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
    return h;
}

void CreateControls() {
    // Created in Tab order. A label's &-shortcut focuses the control created after it.
    const DWORD btn = WS_TABSTOP | BS_PUSHBUTTON;
    MakeControl(L"STATIC", L"Butto&ns:", SS_LEFT, IDC_LIST_LABEL);
    MakeControl(L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, IDC_LIST, WS_EX_CLIENTEDGE);
    MakeControl(L"BUTTON", L"&Add", btn, IDC_ADD);
    MakeControl(L"BUTTON", L"Dup&licate", btn, IDC_DUP);
    MakeControl(L"BUTTON", L"&Delete", btn, IDC_DEL);
    MakeControl(L"BUTTON", L"Move &Up", btn, IDC_UP);
    MakeControl(L"BUTTON", L"Move Do&wn", btn, IDC_DOWN);

    MakeControl(L"STATIC", L"&Button label (the text on the button in Clippi):", SS_LEFT, IDC_LABEL_LABEL);
    MakeControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_LABEL, WS_EX_CLIENTEDGE);
    MakeControl(L"STATIC", L"&Group (optional; Clippi shows one group at a time; empty = \"General\"):",
                SS_LEFT, IDC_GROUP_LABEL);
    MakeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, IDC_GROUP);
    MakeControl(L"STATIC", L"Pr&ompt title (optional; if filled in, Clippi asks for a value and puts it where %XXX% is):",
                SS_LEFT, IDC_PROMPT_LABEL);
    MakeControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_PROMPT, WS_EX_CLIENTEDGE);
    MakeControl(L"STATIC", L"S&econd prompt title (optional; Clippi asks for this value next and puts it where %YYY% is):",
                SS_LEFT, IDC_PROMPT2_LABEL);
    MakeControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_PROMPT2, WS_EX_CLIENTEDGE);

    MakeControl(L"BUTTON", L"&Insert %XXX%", btn, IDC_INSERT_XXX);
    MakeControl(L"BUTTON", L"Insert %&YYY%", btn, IDC_INSERT_YYY);

    // Formatting toolbar: B I U S show the selection's state (push-like check boxes)
    LOGFONTW lf{};
    GetObjectW(g.font, sizeof(lf), &lf);
    const int toggles[] = { IDC_FMT_BOLD, IDC_FMT_ITALIC, IDC_FMT_UNDERLINE, IDC_FMT_STRIKE };
    const wchar_t* toggleText[] = { L"B", L"I", L"U", L"S" };
    for (int k = 0; k < 4; ++k) {
        LOGFONTW f = lf;
        if (k == 0) f.lfWeight = FW_BOLD;
        if (k == 1) f.lfItalic = TRUE;
        if (k == 2) f.lfUnderline = TRUE;
        if (k == 3) f.lfStrikeOut = TRUE;
        g.fmtFonts[k] = CreateFontIndirectW(&f);
        HWND b = MakeControl(L"BUTTON", toggleText[k], WS_TABSTOP | BS_CHECKBOX | BS_PUSHLIKE, toggles[k]);
        SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(g.fmtFonts[k]), FALSE);
    }
    MakeControl(L"BUTTON", L"&Font...", btn, IDC_FMT_FONT);
    MakeControl(L"BUTTON", L"Colo&r...", btn, IDC_FMT_COLOR);
    MakeControl(L"BUTTON", L"Lin&k...", btn, IDC_FMT_LINK);
    MakeControl(L"BUTTON", L"&Picture...", btn, IDC_FMT_PICTURE);
    MakeControl(L"BUTTON", L"Clear format", btn, IDC_FMT_CLEAR);

    MakeControl(L"STATIC", L"&Text to paste (shown as it will paste):", SS_LEFT, IDC_TEXT_LABEL);
    g.box = MakeControl(MSFTEDIT_CLASS, L"",
                        WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_NOHIDESEL,
                        IDC_TEXT, WS_EX_CLIENTEDGE);
    SendMessageW(g.box, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SendMessageW(g.box, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    SetWindowSubclass(g.box, BoxProc, 1, 0);

    MakeControl(L"BUTTON", L"Insert from &HTML/RTF file...", btn, IDC_INSERT_FILE);
    MakeControl(L"BUTTON", L"E&xport RTF...", btn, IDC_EXPORT_RTF);
    MakeControl(L"BUTTON", L"Export HT&ML...", btn, IDC_EXPORT_HTML);
    MakeControl(L"STATIC",
                L"Outlook and Word paste the RTF version, Teams and browsers the HTML, other programs the "
                L"plain text. Ctrl+Shift+V pastes without formatting; Ctrl+L / E / R align.",
                SS_LEFT | SS_NOPREFIX, IDC_HINT);

    MakeControl(L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_STATUS);
    MakeControl(L"BUTTON", L"&Save", btn, IDC_SAVE);
    MakeControl(L"BUTTON", L"&Close", btn, IDC_CLOSE);
}

void Place(int id, int x, int y, int w, int h) { MoveWindow(Ctl(id), x, y, std::max(w, 0), std::max(h, 0), TRUE); }

void Layout() {
    RECT rc{};
    GetClientRect(g.hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int m = S(10), gap = S(6), lblH = S(18), editH = S(24), btnH = S(28);
    const int leftW = S(270);

    // Bottom bar: status (left), Save and Close (right)
    const int barY = H - m - btnH, smallW = S(90);
    Place(IDC_CLOSE, W - m - smallW, barY, smallW, btnH);
    Place(IDC_SAVE, W - m - 2 * smallW - gap, barY, smallW, btnH);
    Place(IDC_STATUS, m, barY + (btnH - lblH) / 2, W - 2 * m - 2 * smallW - 2 * gap, lblH);
    const int bottom = barY - S(12);

    // Left: button list with two rows of buttons under it
    Place(IDC_LIST_LABEL, m, m, leftW, lblH);
    const int row2 = bottom - btnH, row1 = row2 - gap - btnH;
    const int third = (leftW - 2 * gap) / 3, half = (leftW - gap) / 2;
    Place(IDC_ADD, m, row1, third, btnH);
    Place(IDC_DUP, m + third + gap, row1, third, btnH);
    Place(IDC_DEL, m + 2 * (third + gap), row1, leftW - 2 * (third + gap), btnH);
    Place(IDC_UP, m, row2, half, btnH);
    Place(IDC_DOWN, m + half + gap, row2, leftW - half - gap, btnH);
    const int listY = m + lblH + S(2);
    Place(IDC_LIST, m, listY, leftW, row1 - gap - listY);

    // Right: fields, the toolbar, the text box (takes the spare height), file buttons and hint
    const int x = m + leftW + S(18), w = W - x - m;
    int y = m;
    Place(IDC_LABEL_LABEL, x, y, w, lblH);     y += lblH + S(2);
    Place(IDC_LABEL, x, y, w, editH);          y += editH + S(10);
    Place(IDC_GROUP_LABEL, x, y, w, lblH);     y += lblH + S(2);
    Place(IDC_GROUP, x, y, w, S(220));         // height includes the drop-down list
    {
        RECT cr{};
        GetWindowRect(Ctl(IDC_GROUP), &cr);
        y += std::max(editH, static_cast<int>(cr.bottom - cr.top)) + S(10);
    }
    Place(IDC_PROMPT_LABEL, x, y, w, lblH);    y += lblH + S(2);
    Place(IDC_PROMPT, x, y, w, editH);         y += editH + S(10);
    Place(IDC_PROMPT2_LABEL, x, y, w, lblH);   y += lblH + S(2);
    Place(IDC_PROMPT2, x, y, w, editH);        y += editH + S(10);

    // "Text to paste" label with the placeholder buttons on the right
    const int insW = S(110);
    Place(IDC_TEXT_LABEL, x, y + (btnH - lblH) / 2, w - 2 * insW - 2 * gap, lblH);
    Place(IDC_INSERT_XXX, x + w - 2 * insW - gap, y, insW, btnH);
    Place(IDC_INSERT_YYY, x + w - insW, y, insW, btnH);
    y += btnH + S(4);

    // Toolbar: B I U S, then Font, Color, Link, Picture, Clear format
    const int toggleW = S(30);
    int tx = x;
    for (int id : { IDC_FMT_BOLD, IDC_FMT_ITALIC, IDC_FMT_UNDERLINE, IDC_FMT_STRIKE }) {
        Place(id, tx, y, toggleW, btnH);
        tx += toggleW + gap;
    }
    const int tools[] = { IDC_FMT_FONT, IDC_FMT_COLOR, IDC_FMT_LINK, IDC_FMT_PICTURE, IDC_FMT_CLEAR };
    const int toolW = (x + w - tx - 4 * gap) / 5;
    for (int k = 0; k < 5; ++k)
        Place(tools[k], tx + k * (toolW + gap), y, k == 4 ? x + w - (tx + 4 * (toolW + gap)) : toolW, btnH);
    y += btnH + S(4);

    const int hintH = S(36);
    int ay = bottom - hintH;
    Place(IDC_HINT, x, ay, w, hintH);
    ay -= gap + btnH;
    const int fileW = (w - 2 * gap) / 3;
    Place(IDC_INSERT_FILE, x, ay, fileW, btnH);
    Place(IDC_EXPORT_RTF, x + fileW + gap, ay, fileW, btnH);
    Place(IDC_EXPORT_HTML, x + 2 * (fileW + gap), ay, w - 2 * (fileW + gap), btnH);
    Place(IDC_TEXT, x, y, w, ay - S(8) - y);
}

// Fills the Group drop-down with the groups in use, keeping the typed text
void FillGroupChoices() {
    HWND combo = Ctl(IDC_GROUP);
    const std::wstring text = GetText(combo);
    g.loading = true;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& name : AllGroups())
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SetWindowTextW(combo, text.c_str());
    g.loading = false;
}

// The selected clip's group was typed or picked
void OnGroupChanged(const std::wstring& value) {
    if (g.loading || g.cur < 0) return;
    Clip& c = Current();
    if (c.group == value) return;
    c.group = value;
    RefreshListItem(g.cur);
    SetDirty();
}

// Copies an edit box's text into the selected clip
void OnFieldChanged(int id) {
    if (g.loading || g.cur < 0) return;
    Clip& c = Current();
    const std::wstring value = GetText(Ctl(id));
    switch (id) {
    case IDC_LABEL:   c.label = value;   break;
    case IDC_PROMPT:  c.prompt = value;  break;
    case IDC_PROMPT2: c.prompt2 = value; break;
    }
    RefreshListItem(g.cur);
    SetDirty();
}

void OnCommand(int id, int code) {
    // Accelerators (code 1) for formatting only act while the text box has the focus
    const bool formatKey = code == 1;
    if (formatKey && id >= IDC_FMT_BOLD && id <= IDC_FMT_CLEAR && GetFocus() != g.box) return;
    const bool click = code == BN_CLICKED || formatKey;

    switch (id) {
    case IDC_LIST:
        if (code == LBN_SELCHANGE) {
            const int sel = static_cast<int>(SendMessageW(Ctl(IDC_LIST), LB_GETCURSEL, 0, 0));
            if (sel == g.cur) break;
            CommitBox();
            RefreshListItem(g.cur);
            ShowClip(sel);
        }
        break;
    case IDC_LABEL: case IDC_PROMPT: case IDC_PROMPT2:
        if (code == EN_CHANGE) OnFieldChanged(id);
        break;
    case IDC_TEXT:
        if (code == EN_CHANGE) MarkBoxChanged();
        break;
    case IDC_GROUP:
        if (code == CBN_DROPDOWN) {
            FillGroupChoices();
        } else if (code == CBN_EDITCHANGE) {
            OnGroupChanged(GetText(Ctl(IDC_GROUP)));
        } else if (code == CBN_SELCHANGE) {
            // The edit text isn't updated yet; read the picked item instead
            HWND combo = Ctl(IDC_GROUP);
            const LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) {
                const LRESULT len = SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(sel), 0);
                std::wstring value(static_cast<size_t>(len > 0 ? len : 0) + 1, L'\0');
                SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(&value[0]));
                value.resize(static_cast<size_t>(len > 0 ? len : 0));
                OnGroupChanged(value);
            }
        }
        break;
    case IDC_ADD:  AddClip(false); break;
    case IDC_DUP:  AddClip(true); break;
    case IDC_DEL:  DeleteClip(); break;
    case IDC_UP:   MoveClip(-1); break;
    case IDC_DOWN: MoveClip(+1); break;
    case IDC_INSERT_XXX:
        if (g.cur < 0) break;
        InsertIntoText(L"%XXX%");
        if (Trim(Current().prompt).empty())
            SetStatus(L"Tip: also fill in the Prompt title, so Clippi knows to ask for the value.");
        break;
    case IDC_INSERT_YYY:
        if (g.cur < 0) break;
        InsertIntoText(L"%YYY%");
        if (Trim(Current().prompt2).empty())
            SetStatus(L"Tip: also fill in the Second prompt title, so Clippi knows to ask for the value.");
        break;
    case IDC_FMT_BOLD:      if (click && g.cur >= 0) ToggleEffect(CFM_BOLD, CFE_BOLD); break;
    case IDC_FMT_ITALIC:    if (click && g.cur >= 0) ToggleEffect(CFM_ITALIC, CFE_ITALIC); break;
    case IDC_FMT_UNDERLINE: if (click && g.cur >= 0) ToggleEffect(CFM_UNDERLINE, CFE_UNDERLINE); break;
    case IDC_FMT_STRIKE:    if (click && g.cur >= 0) ToggleEffect(CFM_STRIKEOUT, CFE_STRIKEOUT); break;
    case IDC_FMT_FONT:      if (click && g.cur >= 0) ChooseFontForSelection(); break;
    case IDC_FMT_COLOR:     if (click && g.cur >= 0) ChooseColorForSelection(); break;
    case IDC_FMT_LINK:      if (click && g.cur >= 0) InsertLink(); break;
    case IDC_FMT_PICTURE:   if (click && g.cur >= 0) AddPictureFromFile(); break;
    case IDC_FMT_CLEAR:     if (click && g.cur >= 0) ClearFormat(); break;
    case IDC_INSERT_FILE:   if (g.cur >= 0) InsertFromFile(); break;
    case IDC_EXPORT_RTF:    ExportFormatted(false); break;
    case IDC_EXPORT_HTML:   ExportFormatted(true); break;
    case IDC_SAVE:          SaveFile(); break;
    case IDC_CLOSE:         SendMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        CreateControls();
        return 0;
    case WM_SIZE:
        Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = S(940);
        mmi->ptMinTrackSize.y = S(700);
        return 0;
    }
    case WM_COMMAND:
        OnCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case WM_NOTIFY: {
        const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
        if (hdr->idFrom == IDC_TEXT && hdr->code == EN_SELCHANGE && !g.loading) {
            UpdateFormatButtons();
            // Alignment (Ctrl+L/E/R) doesn't send EN_CHANGE; the modify flag catches it
            if (SendMessageW(g.box, EM_GETMODIFY, 0, 0)) MarkBoxChanged();
        }
        break;
    }
    case WM_CLOSE:
        if (ConfirmDiscard()) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HFONT CreateUiFont() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        return CreateFontIndirectW(&ncm.lfMessageFont);
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); // for WIC, dialogs, Rich Edit

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    if (!LoadLibraryW(L"Msftedit.dll")) {
        MessageBoxW(nullptr, L"Rich Edit (Msftedit.dll) could not be loaded.", kAppName, MB_OK | MB_ICONERROR);
        return 1;
    }

    g.inst = hInstance;
    HDC screen = GetDC(nullptr);
    g.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    g.font = CreateUiFont();

    // File to edit: first command-line argument, or Clippi's default location
    std::wstring path = DefaultJsonPath();
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1 && argv[1][0]) path = argv[1];
        LocalFree(argv);
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon   = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!wc.hIcon)   wc.hIcon   = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, kWndClass, kAppTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, S(1040), S(800),
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;
    if (!LoadFile(path)) { DestroyWindow(hwnd); return 0; }
    UpdateTitle();
    RefreshList(g.clips.empty() ? -1 : 0);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    SetFocus(Ctl(IDC_LIST));

    ACCEL accel[] = {
        { FVIRTKEY | FCONTROL, 'S', IDC_SAVE },
        { FVIRTKEY | FCONTROL, 'N', IDC_ADD },
        { FVIRTKEY | FCONTROL, 'B', IDC_FMT_BOLD },
        { FVIRTKEY | FCONTROL, 'I', IDC_FMT_ITALIC },
        { FVIRTKEY | FCONTROL, 'U', IDC_FMT_UNDERLINE },
    };
    HACCEL hAccel = CreateAcceleratorTableW(accel, ARRAYSIZE(accel));

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, hAccel, &m)) continue;
        if (IsDialogMessageW(hwnd, &m)) continue; // Tab, Alt+letter shortcuts
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    DestroyAcceleratorTable(hAccel);
    if (g.font) DeleteObject(g.font);
    for (HFONT f : g.fmtFonts) if (f) DeleteObject(f);
    CoUninitialize();
    return static_cast<int>(m.wParam);
}
