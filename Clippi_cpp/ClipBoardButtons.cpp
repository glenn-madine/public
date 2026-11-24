// ClipBoardButtons.cpp  (Clippi) - version from APP_VERSION_* in resource.h
//
// Reads ClipBoardButtonDefinitions.json and shows one push button per entry,
// labeled with "buttonText". Clicking a button copies "pasteText" to the
// clipboard. If "inputBoxTitle" is non-empty, the user is prompted first and
// the answer replaces every %XXX% in the text. "inputBoxTitle2" works the
// same way for %YYY% (asked second, after inputBoxTitle). The first answer is
// required; the second may be left empty.
//
// The button definitions are always loaded from
//   %USERPROFILE%\Clippi_Storage\ClipBoardButtonDefinitions.json
// On first run the folder is created and the default JSON (embedded in the
// .exe from ClipBoardButtons.rc) is written there. An existing file is never
// overwritten, so your edits are kept.
//
// pasteText markup:
//   \\n  \\t        Line break / tab (written with a double backslash in the JSON)
//   %XXX%          Replaced by the answer typed into the inputBoxTitle box
//   %YYY%          Replaced by the answer typed into the inputBoxTitle2 box
//   %IMG:<base64>% A PNG picture. The clipboard then also gets RTF (Outlook,
//                  Word) and HTML (Teams, browsers) versions that include the
//                  picture; plain-text apps get the text without it.
//   %RTF:<base64>% A ready-made RTF document (e.g. a formatted signature),
//                  put on the clipboard as-is for Outlook and Word.
//   %HTML:<base64>% A ready-made HTML fragment, put on the clipboard as-is
//                  for Teams, browsers and web mail.
//   Line breaks at the end of the text are not copied.
//
// "rawText": true  (optional, written by Clippi Editor when needed)
//   pasteText is copied literally: \n and \t are NOT turned into a line break
//   or tab, so text such as \\server\tools\new pastes exactly as written.
//   Line breaks are then ordinary JSON line breaks ("\n" with one backslash).
//
// Groups:
//   Each entry may have a "group" (e.g. "Remote Support"). Entries without
//   one go into "General". Groups are listed in the order they first appear
//   in the JSON; entries of a group don't need to be next to each other.
//
// Window:
//   Groups pane    List on the left. Selecting a group shows its buttons.
//                  Ctrl+Tab / Ctrl+Shift+Tab (or Ctrl+PgDn / Ctrl+PgUp)
//                  switch groups from anywhere. The last group used is
//                  remembered in HKCU\Software\ClipBoardButtons\LastGroup.
//   Buttons        One per JSON entry in the selected group, sized to the
//                  longest label; wraps into extra columns if the group is
//                  taller than 80% of the screen. The window is sized for the
//                  largest group, so it doesn't jump when you switch.
//   Always on top  Checkbox (Alt+T) or System menu. Remembered in
//                  HKCU\Software\ClipBoardButtons\AlwaysOnTop.
//   Paste          Switches back to the window you used before Clippi and
//                  presses Ctrl+V there, pasting what's on the clipboard (Alt+P).
//                  Clippi Editor's window is never used as the target.
//   Edit Clips     Opens the JSON file in Clippi Editor (ClippiEditor.exe in
//                  the same folder as Clippi), or in Notepad if it isn't there
//                  (Alt+E). When the editor is closed, or when you switch back
//                  to Clippi, the buttons are reloaded if the file was saved
//                  with changes.
//   Exit           Closes the program (Alt+X).
//   Status line    Shows how many buttons loaded / what was last copied.
//   F5             Reloads the JSON file (also System menu > Reload Buttons).
//   About          Link at the right end of the status line (also System menu >
//                  About Clippi). Shows the author, version and date, which
//                  are set in resource.h.
//   Title bar      "Clippi" plus the version from resource.h.
//   High DPI       Per-monitor aware: the window is re-laid out at the new
//                  scale when it is moved to a monitor with a different DPI.
//
// Pure Win32 / C++17, no external libraries.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <cstdio>
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr wchar_t kJsonFileName[] = L"ClipBoardButtonDefinitions.json";
constexpr wchar_t kPlaceholder[]  = L"%XXX%";  // answer to inputBoxTitle
constexpr wchar_t kPlaceholder2[] = L"%YYY%";  // answer to inputBoxTitle2
constexpr wchar_t kWndClass[]     = L"ClipBoardButtonsWnd";
constexpr wchar_t kAppTitle[]     = L"Clippi " APP_VERSION_WSTR;

constexpr int  IDA_RELOAD       = 900;   // F5 accelerator
constexpr int  IDC_STATUS       = 901;
constexpr int  IDC_EXIT         = 902;
constexpr int  IDC_TOPMOST      = 903;
constexpr int  IDC_EDIT         = 904;
constexpr int  IDC_PASTE        = 905;
constexpr int  IDC_ABOUT        = 906;
constexpr int  IDC_GROUPS       = 907;   // group list (navigation pane)
constexpr int  IDA_NEXT_GROUP   = 910;   // Ctrl+Tab / Ctrl+PgDn
constexpr int  IDA_PREV_GROUP   = 911;   // Ctrl+Shift+Tab / Ctrl+PgUp
constexpr wchar_t kDefaultGroup[] = L"General"; // group for entries without "group"
constexpr UINT WM_APP_EDITOR_CLOSED = WM_APP + 1; // posted when the editor process exits
constexpr UINT WM_APP_CHECK_FILE    = WM_APP + 2; // re-check the JSON file for changes
constexpr wchar_t kRegKey[]     = L"Software\\ClipBoardButtons"; // HKCU, saves "AlwaysOnTop"
constexpr int  IDC_FIRST_BUTTON = 1000;  // button i has ID 1000 + i
constexpr int  IDC_INPUT_LABEL  = 100;
constexpr int  IDC_INPUT_EDIT   = 101;
constexpr UINT IDM_TOPMOST      = 0x0010; // system-menu IDs: < 0xF000, multiples of 16
constexpr UINT IDM_RELOAD       = 0x0020;
constexpr UINT IDM_ABOUT        = 0x0030;

constexpr DWORD kMainStyle   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr DWORD kMainExStyle = 0;

// ============================================================================
// Minimal JSON parser (objects, arrays, strings, numbers, true/false/null)
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
                out += static_cast<wchar_t>(cp); // UTF-16: surrogate pairs pass through as-is
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

// ============================================================================
// Button definitions
// ============================================================================
struct ButtonDef {
    std::wstring text;        // buttonText
    std::wstring pasteText;   // pasteText
    std::wstring inputTitle;  // inputBoxTitle (empty = no prompt)  -> %XXX%
    std::wstring inputTitle2; // inputBoxTitle2 (empty = no prompt) -> %YYY%
    std::wstring group;       // group (never empty: defaults to kDefaultGroup)
    bool rawText = false;     // rawText: pasteText is literal (no \n / \t conversion)
};

std::wstring Trim(const std::wstring& s);

std::wstring GetString(const JsonValue& obj, const wchar_t* key) {
    const JsonValue* v = obj.Find(key);
    return (v && v->type == JsonValue::String) ? v->str : std::wstring();
}

// Reads a text file as UTF-8 (BOM optional) or UTF-16LE (with BOM).
bool ReadTextFile(const std::wstring& path, std::wstring& out, std::wstring& error) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = L"Cannot open file:\n" + path;
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        error = L"File is too large:\n" + path;
        return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = bytes.empty() ||
                    ReadFile(h, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) {
        error = L"Cannot read file:\n" + path;
        return false;
    }
    bytes.resize(read);

    // UTF-16LE with BOM
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) {
        out.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
        return true;
    }
    // UTF-8, optional BOM
    size_t offset = 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        offset = 3;

    const char* src = bytes.data() + offset;
    const int srcLen = static_cast<int>(bytes.size() - offset);
    out.clear();
    if (srcLen == 0) return true;

    UINT codePage = CP_UTF8;
    int len = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, src, srcLen, nullptr, 0);
    if (len == 0) { codePage = CP_ACP; len = MultiByteToWideChar(codePage, 0, src, srcLen, nullptr, 0); }
    out.resize(static_cast<size_t>(len));
    MultiByteToWideChar(codePage, 0, src, srcLen, &out[0], len);
    return true;
}

bool LoadDefinitions(const std::wstring& path, std::vector<ButtonDef>& defs, std::wstring& error) {
    std::wstring text;
    if (!ReadTextFile(path, text, error)) return false;

    JsonValue root;
    try {
        root = JsonParser(text).Parse();
    } catch (const JsonError& e) {
        error = L"JSON error in " + path + L":\n\n" + e.message;
        return false;
    }
    if (root.type != JsonValue::Array) {
        error = L"The top level of " + path + L" must be a JSON array [ ... ].";
        return false;
    }

    defs.clear();
    for (const JsonValue& item : root.array) {
        if (item.type != JsonValue::Object) continue;
        ButtonDef d;
        d.text       = GetString(item, L"buttonText");
        d.pasteText  = GetString(item, L"pasteText");
        d.inputTitle = GetString(item, L"inputBoxTitle");
        d.inputTitle2 = GetString(item, L"inputBoxTitle2");
        d.group      = Trim(GetString(item, L"group"));
        const JsonValue* raw = item.Find(L"rawText");
        d.rawText    = raw && raw->type == JsonValue::Bool && raw->boolean;
        if (d.text.empty()) d.text = L"(unnamed)";
        if (d.group.empty()) d.group = kDefaultGroup;
        defs.push_back(std::move(d));
    }
    return true;
}

bool FileExists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// %USERPROFILE%\Clippi_Storage (empty string if USERPROFILE isn't set)
std::wstring StorageFolder() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(L"%USERPROFILE%\\Clippi_Storage", buf, ARRAYSIZE(buf));
    if (n == 0 || n > ARRAYSIZE(buf) || wcschr(buf, L'%')) return std::wstring();
    return buf;
}

// Creates the storage folder and writes the default JSON embedded in the .exe.
// CREATE_NEW guarantees an existing file is never overwritten.
bool CreateDefaultJsonFile(const std::wstring& folder, const std::wstring& path) {
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;

    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_DEFAULT_JSON), RT_RCDATA);
    HGLOBAL loaded = res ? LoadResource(nullptr, res) : nullptr;
    const void* data = loaded ? LockResource(loaded) : nullptr;
    const DWORD size = res ? SizeofResource(nullptr, res) : 0;
    if (!data || size == 0) return false;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data, size, &written, nullptr) && written == size;
    CloseHandle(h);
    if (!ok) DeleteFileW(path.c_str()); // don't leave a half-written file behind
    return ok;
}

// The definitions file always lives in %USERPROFILE%\Clippi_Storage.
// On first run (folder or file missing) it is created from the default JSON
// embedded in the .exe. An existing file is never overwritten.
std::wstring FindJsonPath() {
    const std::wstring folder = StorageFolder();
    if (folder.empty())
        return std::wstring(L"%USERPROFILE%\\Clippi_Storage\\") + kJsonFileName; // USERPROFILE not set

    const std::wstring path = folder + L"\\" + kJsonFileName;
    if (!FileExists(path))
        CreateDefaultJsonFile(folder, path); // if this fails, loading reports the path
    return path;
}

// ============================================================================
// Text helpers
// ============================================================================
void ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::wstring::npos; pos += to.size())
        s.replace(pos, from.size(), to);
}

// Replaces %XXX% with x and %YYY% with y in one pass, so text typed into one
// box is never changed by the other replacement.
template <class Str>
Str ReplacePlaceholders(const Str& s, const Str& xxx, const Str& x, const Str& yyy, const Str& y) {
    Str out;
    size_t pos = 0;
    for (;;) {
        const size_t a = s.find(xxx, pos), b = s.find(yyy, pos);
        const size_t at = std::min(a, b);
        if (at == Str::npos) { out.append(s, pos, Str::npos); return out; }
        out.append(s, pos, at - pos);
        out += (at == a) ? x : y;
        pos = at + ((at == a) ? xxx.size() : yyy.size());
    }
}

// Turns the stored pasteText into what goes on the clipboard:
//   - literal "\n" / "\t" sequences (written as "\\n" in the JSON) become a newline / tab,
//     unless the entry has "rawText": true (then the text is taken literally)
//   - all line breaks are normalized to CRLF
//   - %XXX% / %YYY% are replaced by the user's input (done last, so input is pasted verbatim)
std::wstring PrepareText(std::wstring text, bool rawText, const std::wstring& input,
                         const std::wstring& input2) {
    if (!rawText) {
        ReplaceAll(text, L"\\n", L"\n");
        ReplaceAll(text, L"\\t", L"\t");
    }
    text.erase(std::remove(text.begin(), text.end(), L'\r'), text.end());
    ReplaceAll(text, L"\n", L"\r\n");
    return ReplacePlaceholders<std::wstring>(text, kPlaceholder, input, kPlaceholder2, input2);
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// ============================================================================
// Rich content in pasteText
// ----------------------------------------------------------------------------
// Besides plain text, a pasteText may contain these tokens (base64 inside):
//   %IMG:<PNG>%    A picture. Clippi builds RTF and HTML versions of the whole
//                  clip with the picture in place (text in Calibri 11 pt).
//   %RTF:<RTF>%    A complete, ready-made RTF document, such as a formatted
//                  signature saved from Word or Outlook. It goes on the
//                  clipboard as-is and is used by Outlook and Word.
//   %HTML:<HTML>%  A ready-made UTF-8 HTML fragment. It goes on the clipboard
//                  as-is and is used by Teams, browsers and web mail.
// The text outside the tokens is always the plain-text version (Notepad etc.).
// A ready-made RTF/HTML takes priority over one built from %IMG% tokens, and
// %XXX% / %YYY% inside it are replaced too (escaped for that format).
// Without any token, only plain text is copied.
// ============================================================================
enum class SegKind { Text, Image, Rtf, Html };

struct PasteSegment {
    SegKind kind = SegKind::Text;
    std::wstring text;               // Text: the text
    std::wstring base64;             // Image: original base64 (reused in the HTML)
    std::vector<unsigned char> data; // Image: PNG bytes; Rtf/Html: document bytes
    int width = 0, height = 0;       // Image: pixels, from the PNG header
};

std::vector<unsigned char> Base64Decode(const std::wstring& s) {
    static const char kChars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> out;
    unsigned buffer = 0;
    int bits = 0;
    for (wchar_t c : s) {
        if (c == L'=') break;
        const char* hit = (c > 0 && c < 128) ? strchr(kChars, static_cast<char>(c)) : nullptr;
        if (!hit) continue; // ignore whitespace / line breaks
        buffer = (buffer << 6) | static_cast<unsigned>(hit - kChars);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

// Reads width/height from a PNG's IHDR chunk. Returns false if it isn't a PNG.
bool PngSize(const std::vector<unsigned char>& png, int& w, int& h) {
    static const unsigned char kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (png.size() < 24 || memcmp(png.data(), kSig, 8) != 0 || memcmp(&png[12], "IHDR", 4) != 0)
        return false;
    auto be32 = [&](size_t i) {
        return static_cast<int>((png[i] << 24) | (png[i + 1] << 16) | (png[i + 2] << 8) | png[i + 3]);
    };
    w = be32(16);
    h = be32(20);
    return w > 0 && h > 0;
}

// Splits prepared text into plain-text pieces and %IMG:% / %RTF:% / %HTML:% tokens.
std::vector<PasteSegment> SplitSegments(const std::wstring& text) {
    static const struct { const wchar_t* open; SegKind kind; } kTokens[] = {
        { L"%IMG:",  SegKind::Image },
        { L"%RTF:",  SegKind::Rtf   },
        { L"%HTML:", SegKind::Html  },
    };
    std::vector<PasteSegment> segs;
    size_t pos = 0;
    for (;;) {
        // Earliest complete token (opening marker plus a closing '%')
        size_t a = std::wstring::npos, b = std::wstring::npos, openLen = 0;
        SegKind kind = SegKind::Text;
        for (const auto& t : kTokens) {
            const size_t at = text.find(t.open, pos);
            if (at == std::wstring::npos || at >= a) continue;
            const size_t len = wcslen(t.open);
            const size_t end = text.find(L'%', at + len);
            if (end == std::wstring::npos) continue;
            a = at; b = end; openLen = len; kind = t.kind;
        }
        if (a == std::wstring::npos) {
            PasteSegment t; t.text = text.substr(pos); segs.push_back(t);
            break;
        }
        PasteSegment t; t.text = text.substr(pos, a - pos); segs.push_back(t);

        PasteSegment s;
        s.kind = kind;
        s.base64 = text.substr(a + openLen, b - a - openLen);
        s.data = Base64Decode(s.base64);
        const bool valid = (kind == SegKind::Image) ? PngSize(s.data, s.width, s.height)
                                                    : !s.data.empty();
        if (valid) segs.push_back(std::move(s)); // invalid tokens are dropped
        pos = b + 1;
    }
    return segs;
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// Text -> HTML (UTF-8): escapes special characters, line breaks become <br>.
// A space at the start of a line or after another space becomes &nbsp;, so
// indentation and runs of spaces survive (HTML would collapse them).
std::string HtmlEscape(const std::wstring& text) {
    std::wstring out;
    wchar_t prev = L'\n';
    for (wchar_t c : text) {
        const wchar_t before = prev;
        if (c != L'\r') prev = c;
        switch (c) {
        case L' ':
            out += (before == L' ' || before == L'\n') ? L"&nbsp;" : L" ";
            break;
        case L'&':  out += L"&amp;";  break;
        case L'<':  out += L"&lt;";   break;
        case L'>':  out += L"&gt;";   break;
        case L'"':  out += L"&quot;"; break;
        case L'\t': out += L"&nbsp;&nbsp;&nbsp;&nbsp;"; break;
        case L'\r': break;
        case L'\n': out += L"<br>\r\n"; break;
        default:    out += c; break;
        }
    }
    return ToUtf8(out);
}

// Text -> RTF: escapes \ { }, line breaks become \par, non-ASCII becomes \uN?
std::string RtfEscape(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) {
        switch (c) {
        case L'\\': out += "\\\\"; break;
        case L'{':  out += "\\{";  break;
        case L'}':  out += "\\}";  break;
        case L'\t': out += "\\tab "; break;
        case L'\r': break;
        case L'\n': out += "\\par\r\n"; break;
        default:
            if (c < 128) out += static_cast<char>(c);
            else out += "\\u" + std::to_string(static_cast<short>(c)) + "?";
        }
    }
    return out;
}

// Font used when Clippi builds the rich versions itself (for %IMG% clips)
constexpr char kRichFontCss[] = "font-family:Calibri,Arial,sans-serif;font-size:11pt";
constexpr char kRichFontRtf[] = "Calibri";
constexpr int  kRichFontHalfPoints = 22; // 11 pt

// HTML fragment (UTF-8) for a clip made of text and %IMG% pictures
std::string BuildHtmlFragment(const std::vector<PasteSegment>& segs) {
    std::string frag = std::string("<div style=\"") + kRichFontCss + "\">";
    for (const auto& s : segs) {
        if (s.kind == SegKind::Text) {
            frag += HtmlEscape(s.text);
        } else if (s.kind == SegKind::Image) {
            frag += "<img src=\"data:image/png;base64," + ToUtf8(s.base64) + "\" width=\"" +
                    std::to_string(s.width) + "\" height=\"" + std::to_string(s.height) + "\" alt=\"\">";
        }
    }
    return frag + "</div>";
}

// RTF document for a clip made of text and %IMG% pictures
std::string BuildRtfDocument(const std::vector<PasteSegment>& segs) {
    std::string rtf = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss " +
                      std::string(kRichFontRtf) + ";}}\\f0\\fs" +
                      std::to_string(kRichFontHalfPoints) + " ";
    static const char kHex[] = "0123456789abcdef";
    for (const auto& s : segs) {
        if (s.kind == SegKind::Text) {
            rtf += RtfEscape(s.text);
        } else if (s.kind == SegKind::Image) {
            // picw/pich in pixels, picwgoal/pichgoal in twips (15 twips per pixel at 96 DPI)
            rtf += "{\\pict\\pngblip\\picw" + std::to_string(s.width) + "\\pich" + std::to_string(s.height) +
                   "\\picwgoal" + std::to_string(s.width * 15) + "\\pichgoal" + std::to_string(s.height * 15) + "\r\n";
            for (size_t i = 0; i < s.data.size(); ++i) {
                rtf += kHex[s.data[i] >> 4];
                rtf += kHex[s.data[i] & 0x0F];
                if (i % 64 == 63) rtf += "\r\n";
            }
            rtf += "}";
        }
    }
    return rtf + "}";
}

// Wraps a UTF-8 HTML fragment in the CF_HTML clipboard format: a header with
// byte offsets of the HTML document and of the fragment inside it.
std::string WrapCfHtml(const std::string& fragment) {
    const std::string pre  = "<html><body>\r\n<!--StartFragment-->";
    const std::string post = "<!--EndFragment-->\r\n</body></html>";
    const char* headerFmt = "Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\n"
                            "StartFragment:%010zu\r\nEndFragment:%010zu\r\n";
    char header[160];
    const size_t headerLen = static_cast<size_t>(snprintf(header, sizeof(header), headerFmt,
                                                          size_t(0), size_t(0), size_t(0), size_t(0)));
    const size_t startHtml = headerLen;
    const size_t startFrag = startHtml + pre.size();
    const size_t endFrag   = startFrag + fragment.size();
    const size_t endHtml   = endFrag + post.size();
    snprintf(header, sizeof(header), headerFmt, startHtml, endHtml, startFrag, endFrag);
    return std::string(header) + pre + fragment + post;
}

// Puts a copy of the bytes on the (already open) clipboard.
bool SetClipboardBytes(UINT format, const void* data, size_t bytes) {
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    bool ok = false;
    if (void* p = GlobalLock(mem)) {
        memcpy(p, data, bytes);
        GlobalUnlock(mem);
        ok = SetClipboardData(format, mem) != nullptr;
    }
    if (!ok) GlobalFree(mem); // on success the clipboard owns the memory
    return ok;
}

// text:   the prepared pasteText (escapes converted, %XXX% / %YYY% already filled in)
// input:  the value typed into the first prompt, used for %XXX% inside %RTF%/%HTML%
// input2: the value typed into the second prompt, used for %YYY% inside %RTF%/%HTML%
bool CopyToClipboard(HWND owner, const std::wstring& text, const std::wstring& input,
                     const std::wstring& input2) {
    std::vector<PasteSegment> segs = SplitSegments(text);
    std::wstring plain;
    std::string rtf, html; // ready-made rich versions, if the clip has them
    bool hasImage = false;
    for (const auto& s : segs) {
        switch (s.kind) {
        case SegKind::Text:  plain += s.text; break;
        case SegKind::Image: hasImage = true; break;
        case SegKind::Rtf:   if (rtf.empty())  rtf.assign(s.data.begin(), s.data.end());  break;
        case SegKind::Html:  if (html.empty()) html.assign(s.data.begin(), s.data.end()); break;
        }
    }
    // Line breaks at the end are never copied: they would add blank lines after the
    // pasted text (and a token at the end would otherwise leave one dangling).
    while (!plain.empty() && (plain.back() == L'\r' || plain.back() == L'\n')) plain.pop_back();
    // Same for the text pieces the RTF/HTML versions are built from
    // (stop at a picture: a line break before a picture belongs to the text)
    for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
        if (it->kind == SegKind::Image) break;
        if (it->kind != SegKind::Text) continue; // %RTF% / %HTML% tokens aren't drawn
        std::wstring& t = it->text;
        while (!t.empty() && (t.back() == L'\r' || t.back() == L'\n')) t.pop_back();
        if (!t.empty()) break;
    }
    rtf  = ReplacePlaceholders<std::string>(rtf,  "%XXX%", RtfEscape(input),  "%YYY%", RtfEscape(input2));
    html = ReplacePlaceholders<std::string>(html, "%XXX%", HtmlEscape(input), "%YYY%", HtmlEscape(input2));
    if (rtf.empty() && hasImage)  rtf  = BuildRtfDocument(segs);
    if (html.empty() && hasImage) html = BuildHtmlFragment(segs);

    bool opened = false;
    for (int attempt = 0; attempt < 10 && !opened; ++attempt) {
        opened = OpenClipboard(owner) != FALSE;
        if (!opened) Sleep(30); // another app may briefly hold the clipboard
    }
    if (!opened) return false;

    EmptyClipboard();
    const bool ok = SetClipboardBytes(CF_UNICODETEXT, plain.c_str(), (plain.size() + 1) * sizeof(wchar_t));
    if (ok && !rtf.empty())
        SetClipboardBytes(RegisterClipboardFormatW(L"Rich Text Format"), rtf.c_str(), rtf.size() + 1);
    if (ok && !html.empty()) {
        const std::string cf = WrapCfHtml(html);
        SetClipboardBytes(RegisterClipboardFormatW(L"HTML Format"), cf.c_str(), cf.size() + 1);
    }
    CloseClipboard();
    return ok;
}

// ============================================================================
// Input box (dialog template built in memory, so no .rc file is needed)
// ============================================================================
class DialogTemplate {
public:
    DialogTemplate(const wchar_t* title, short cx, short cy) {
        PutDword(DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        PutDword(0);                       // extended style
        countIndex_ = buf_.size();
        buf_.push_back(0);                 // item count (filled in by AddItem)
        PutShort(0); PutShort(0); PutShort(cx); PutShort(cy);
        buf_.push_back(0);                 // no menu
        buf_.push_back(0);                 // default dialog class
        PutString(title);
        buf_.push_back(9);                 // font point size
        PutString(L"Segoe UI");
    }

    void AddItem(WORD id, WORD classAtom, DWORD style,
                 short x, short y, short cx, short cy, const wchar_t* text) {
        if (buf_.size() % 2) buf_.push_back(0); // DWORD-align each item
        PutDword(style | WS_CHILD | WS_VISIBLE);
        PutDword(0);
        PutShort(x); PutShort(y); PutShort(cx); PutShort(cy);
        buf_.push_back(id);
        buf_.push_back(0xFFFF);
        buf_.push_back(classAtom);         // 0x80 button, 0x81 edit, 0x82 static
        PutString(text);
        buf_.push_back(0);                 // no creation data
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

struct InputDlgData {
    std::wstring title;
    std::wstring value;      // in: initial text, out: entered text
    bool allowEmpty = false; // true: OK with an empty box is accepted
};

INT_PTR CALLBACK InputDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* data = reinterpret_cast<InputDlgData*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetWindowTextW(dlg, data->title.c_str());
        SetDlgItemTextW(dlg, IDC_INPUT_LABEL,
                        (L"Enter " + data->title + (data->allowEmpty ? L" (optional):" : L":")).c_str());
        HWND edit = GetDlgItem(dlg, IDC_INPUT_EDIT);
        SetWindowTextW(edit, data->value.c_str());
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        return FALSE; // we set the focus ourselves
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: {
            auto* data = reinterpret_cast<InputDlgData*>(GetWindowLongPtrW(dlg, DWLP_USER));
            HWND edit = GetDlgItem(dlg, IDC_INPUT_EDIT);
            const int len = GetWindowTextLengthW(edit);
            std::wstring value(static_cast<size_t>(len) + 1, L'\0');
            GetWindowTextW(edit, &value[0], len + 1);
            value.resize(static_cast<size_t>(len));
            value = Trim(value);
            if (value.empty() && !data->allowEmpty) {
                MessageBeep(MB_ICONWARNING);
                SetFocus(edit);
                return TRUE;
            }
            data->value = value;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

bool PromptForInput(HWND owner, InputDlgData& data) {
    DialogTemplate t(L"Input", 240, 62);
    t.AddItem(IDC_INPUT_LABEL, 0x0082, SS_LEFT | SS_NOPREFIX,                7,  7, 226,  9, L"");
    t.AddItem(IDC_INPUT_EDIT,  0x0081, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 7, 19, 226, 14, L"");
    t.AddItem(IDOK,            0x0080, WS_TABSTOP | BS_DEFPUSHBUTTON,       129, 41,  50, 14, L"OK");
    t.AddItem(IDCANCEL,        0x0080, WS_TABSTOP | BS_PUSHBUTTON,          183, 41,  50, 14, L"Cancel");
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), t.Get(), owner,
                                   InputDlgProc, reinterpret_cast<LPARAM>(&data)) == IDOK;
}

// ============================================================================
// Main window
// ============================================================================
struct AppState {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    HWND status = nullptr;
    HWND exitButton = nullptr;
    HWND topmostCheck = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    bool topmost = false;
    std::wstring jsonPath;
    std::vector<ButtonDef> defs;
    std::vector<HWND> buttons;         // buttons of the selected group only
    std::map<std::wstring, std::wstring> lastInput; // remembers last entry per prompt

    // Groups / navigation pane
    HWND groupList = nullptr;          // list box on the left
    std::vector<std::wstring> groups;  // in order of first appearance in the JSON
    size_t group = 0;                  // index of the selected group
    std::wstring groupName;            // its name (kept across reloads)

    // Button grid geometry, computed by LayoutWindow, used by ShowGroupButtons
    int gridX = 0, gridY = 0, gridBtnW = 0, gridBtnH = 0, gridGap = 0, gridMaxRows = 1;

    // "Edit Clips" support
    HWND editButton = nullptr;

    // "Paste" support
    HWND pasteButton = nullptr;
    HWND pasteTarget = nullptr;        // last window used before Clippi
    HWINEVENTHOOK foregroundHook = nullptr;

    // "About" link
    HWND aboutLink = nullptr;
    HFONT linkFont = nullptr;          // underlined copy of the UI font
    HANDLE editorProcess = nullptr;    // editor process we launched (if still running)
    HANDLE editorWait = nullptr;       // thread-pool wait on that process
    UINT_PTR editorSession = 0;        // counts editor launches; tags WM_APP_EDITOR_CLOSED
    ULONGLONG editorStartTick = 0;
    bool editorIsNotepad = false;      // true if ClippiEditor.exe wasn't found
    FILETIME loadedWriteTime{};        // JSON file's last-write time when it was loaded
    ULONGLONG loadedSize = 0;          // ... and its size
    bool haveStamp = false;
    FILETIME editStartWriteTime{};     // the file's state when the editor was opened
    ULONGLONG editStartSize = 0;
    bool haveEditStartStamp = false;
} g;

int Scale(int v) { return MulDiv(v, g.dpi, 96); }

// ---- Per-monitor DPI ----
// The *ForDpi functions exist on Windows 10 1607 and later; they are looked up
// at run time so Clippi still starts (system-DPI aware) on older Windows.
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

template <typename Fn> Fn User32Proc(const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), name)));
}

int g_systemDpi = 96; // DPI the plain (non-ForDpi) metrics are reported in

// Process-wide: per-monitor v2 if available, otherwise system-DPI aware.
void SetDpiAwareness() {
    using Fn = BOOL(WINAPI*)(HANDLE);
    if (auto set = User32Proc<Fn>("SetProcessDpiAwarenessContext")) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (DPI_AWARENESS_CONTEXT)-4
        if (set(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) return;
    }
    SetProcessDPIAware();
}

int WindowDpi(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static const Fn get = User32Proc<Fn>("GetDpiForWindow");
    const UINT dpi = get ? get(hwnd) : 0;
    return dpi ? static_cast<int>(dpi) : g_systemDpi;
}

// GetSystemMetrics at the window's DPI
int Metric(int index) {
    using Fn = int(WINAPI*)(int, UINT);
    static const Fn get = User32Proc<Fn>("GetSystemMetricsForDpi");
    return get ? get(index, static_cast<UINT>(g.dpi))
               : MulDiv(GetSystemMetrics(index), g.dpi, g_systemDpi);
}

// Client rectangle -> window rectangle at the window's DPI
void AdjustRectForDpi(RECT& rc) {
    using Fn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static const Fn adjust = User32Proc<Fn>("AdjustWindowRectExForDpi");
    if (!adjust || !adjust(&rc, kMainStyle, FALSE, kMainExStyle, static_cast<UINT>(g.dpi)))
        AdjustWindowRectEx(&rc, kMainStyle, FALSE, kMainExStyle);
}

// The Windows message font at the window's DPI
HFONT CreateUiFont() {
    using Fn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static const Fn spiForDpi = User32Proc<Fn>("SystemParametersInfoForDpi");
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (spiForDpi && spiForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, static_cast<UINT>(g.dpi)))
        return CreateFontIndirectW(&ncm.lfMessageFont);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, g.dpi, g_systemDpi);
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

// Creates the UI font and the underlined link font for the current g.dpi and
// gives them to the fixed controls (group buttons get g.font when created).
// The previous fonts are deleted by the caller once nothing uses them.
void CreateFonts() {
    g.font = CreateUiFont();
    LOGFONTW lf{};
    GetObjectW(g.font, sizeof(lf), &lf);
    lf.lfUnderline = TRUE;
    g.linkFont = CreateFontIndirectW(&lf);
    for (HWND w : { g.groupList, g.status, g.exitButton, g.editButton, g.pasteButton, g.topmostCheck })
        if (w) SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    if (g.aboutLink) SendMessageW(g.aboutLink, WM_SETFONT, reinterpret_cast<WPARAM>(g.linkFont), TRUE);
}

void DeleteFont(HFONT f) {
    if (f && f != static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))) DeleteObject(f);
}

void SetStatus(const std::wstring& text) { SetWindowTextW(g.status, text.c_str()); }

// ---- Groups (navigation pane) ----

bool LoadLastGroup(std::wstring& name) {
    wchar_t buf[256] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"LastGroup", RRF_RT_REG_SZ,
                     nullptr, buf, &size) != ERROR_SUCCESS) return false;
    name = buf;
    return true;
}

void SaveLastGroup(const std::wstring& name) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"LastGroup", 0, REG_SZ, reinterpret_cast<const BYTE*>(name.c_str()),
                       static_cast<DWORD>((name.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

// Rebuilds g.groups from g.defs and fills the list box. Keeps the selected
// group (by name) if it still exists, otherwise selects the first one.
void RebuildGroups() {
    g.groups.clear();
    for (const auto& d : g.defs)
        if (std::find(g.groups.begin(), g.groups.end(), d.group) == g.groups.end())
            g.groups.push_back(d.group);

    SendMessageW(g.groupList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.groupList, LB_RESETCONTENT, 0, 0);
    for (const auto& name : g.groups)
        SendMessageW(g.groupList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendMessageW(g.groupList, WM_SETREDRAW, TRUE, 0);

    const auto it = std::find(g.groups.begin(), g.groups.end(), g.groupName);
    g.group = it == g.groups.end() ? 0 : static_cast<size_t>(it - g.groups.begin());
    if (!g.groups.empty()) g.groupName = g.groups[g.group];
    SendMessageW(g.groupList, LB_SETCURSEL, g.groups.empty() ? -1 : static_cast<WPARAM>(g.group), 0);
    InvalidateRect(g.groupList, nullptr, TRUE);
}

// Creates the buttons of the selected group in the grid set up by LayoutWindow.
// Button IDs are IDC_FIRST_BUTTON + the entry's index in g.defs.
void ShowGroupButtons(bool focusFirstButton) {
    for (HWND b : g.buttons) DestroyWindow(b);
    g.buttons.clear();

    if (g.group < g.groups.size()) {
        const std::wstring& name = g.groups[g.group];
        int n = 0;
        for (size_t i = 0; i < g.defs.size(); ++i) {
            if (g.defs[i].group != name) continue;
            const int col = n / g.gridMaxRows, row = n % g.gridMaxRows;
            ++n;
            // Buttons treat & as a shortcut marker; && shows a real & ("Q&A" stays "Q&A")
            std::wstring label = g.defs[i].text;
            ReplaceAll(label, L"&", L"&&");
            HWND b = CreateWindowExW(0, L"BUTTON", label.c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     g.gridX + col * (g.gridBtnW + g.gridGap),
                                     g.gridY + row * (g.gridBtnH + g.gridGap),
                                     g.gridBtnW, g.gridBtnH, g.hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FIRST_BUTTON + i)),
                                     g.inst, nullptr);
            SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
            g.buttons.push_back(b);
        }
    }

    // New windows go to the end of the Tab order; move the bottom row after
    // them again, so the order is: groups, buttons, checkbox, Paste, Edit Clips, Exit.
    for (HWND w : { g.topmostCheck, g.pasteButton, g.editButton, g.exitButton })
        SetWindowPos(w, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (focusFirstButton && !g.buttons.empty()) SetFocus(g.buttons.front());
}

// Selects group i (list box, buttons and saved setting).
void SelectGroup(size_t i, bool focusFirstButton) {
    if (i >= g.groups.size()) return;
    g.group = i;
    g.groupName = g.groups[i];
    SendMessageW(g.groupList, LB_SETCURSEL, static_cast<WPARAM>(i), 0);
    SaveLastGroup(g.groupName);
    ShowGroupButtons(focusFirstButton);
    SetStatus(g.groupName + L": " + std::to_wstring(g.buttons.size()) +
              (g.buttons.size() == 1 ? L" button" : L" buttons"));
}

// Ctrl+Tab / Ctrl+Shift+Tab: next / previous group, wrapping around.
void StepGroup(int delta) {
    const size_t n = g.groups.size();
    if (n < 2) return;
    const size_t next = delta > 0 ? (g.group + 1) % n : (g.group + n - 1) % n;
    // Keep the focus in the list if it's there; otherwise go to the new buttons.
    SelectGroup(next, GetFocus() != g.groupList);
}

// Sizes the window and places every control. The window is sized for the
// largest group so it keeps its size when switching groups.
void LayoutWindow() {
    RebuildGroups();

    HDC dc = GetDC(g.hwnd);
    HGDIOBJ oldFont = SelectObject(dc, g.font);
    int maxText = 0, maxGroupText = 0;
    for (const auto& d : g.defs) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, d.text.c_str(), static_cast<int>(d.text.size()), &sz);
        maxText = std::max(maxText, static_cast<int>(sz.cx));
    }
    for (const auto& name : g.groups) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, name.c_str(), static_cast<int>(name.size()), &sz);
        maxGroupText = std::max(maxGroupText, static_cast<int>(sz.cx));
    }
    SIZE linkSz{};
    SelectObject(dc, g.linkFont ? g.linkFont : g.font); // measured in the font it is drawn in
    GetTextExtentPoint32W(dc, L"About", 5, &linkSz);
    SelectObject(dc, oldFont);
    ReleaseDC(g.hwnd, dc);

    const int margin  = Scale(10);
    const int gap     = Scale(6);
    const int btnH    = Scale(30);
    const int lineH   = Scale(18);             // one line of status text
    const int statusH = lineH + Scale(4);      // status line only
    const int exitW   = Scale(80);
    const int editW   = Scale(90);
    const int pasteW  = Scale(80);
    const int checkW  = Scale(130);

    // Largest group decides the grid size
    int maxCount = 0;
    for (const auto& name : g.groups) {
        const int n = static_cast<int>(std::count_if(g.defs.begin(), g.defs.end(),
                                       [&](const ButtonDef& d) { return d.group == name; }));
        maxCount = std::max(maxCount, n);
    }

    // Wrap into extra columns if a group would be taller than ~80% of the screen
    // (the monitor the window is on)
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT work = mi.rcWork;
    const int usableH = (work.bottom - work.top) * 8 / 10 - 2 * margin - statusH - btnH - 2 * gap;
    const int maxRows = std::max(1, (usableH + gap) / (btnH + gap));
    const int rows    = std::min(maxCount, maxRows);
    const int cols    = maxCount == 0 ? 1 : (maxCount + maxRows - 1) / maxRows;

    // Groups pane: wide enough for the longest group name (plus a scroll bar)
    const int navW = std::max(Scale(120), maxGroupText + Scale(16) + Metric(SM_CXVSCROLL));
    const int gridX = margin + navW + margin;

    // Window width: wide enough for the grid and for the bottom row.
    // Message buttons are then stretched so they fill the full width.
    const int labelW  = std::max(Scale(220), maxText + Scale(32));
    const int clientW = std::max(gridX + cols * labelW + (cols - 1) * gap + margin,
                                 2 * margin + checkW + gap + pasteW + gap + editW + gap + exitW);
    const int btnW    = (clientW - gridX - margin - (cols - 1) * gap) / cols;

    // Content height: the button grid, or enough to list every group (within limits)
    const int itemH    = static_cast<int>(SendMessageW(g.groupList, LB_GETITEMHEIGHT, 0, 0));
    const int listNeed = itemH * static_cast<int>(g.groups.size()) + Scale(4);
    const int buttonsH = rows > 0 ? rows * (btnH + gap) - gap : 0;
    const int contentH = std::max({ buttonsH, std::min(listNeed, std::max(usableH, buttonsH)), btnH * 3 });

    g.gridX = gridX; g.gridY = margin; g.gridBtnW = btnW; g.gridBtnH = btnH;
    g.gridGap = gap; g.gridMaxRows = maxRows;

    MoveWindow(g.groupList, margin, margin, navW, contentH, TRUE);

    const int exitY   = margin + contentH + 2 * gap; // extra space below the list
    const int clientH = exitY + btnH + margin + statusH;

    // Bottom row: "Always on top" checkbox (left), then "Paste", "Edit Clips" and "Exit" (right).
    const int exitX  = clientW - margin - exitW;
    const int editX  = exitX - gap - editW;
    const int pasteX = editX - gap - pasteW;
    MoveWindow(g.topmostCheck, margin, exitY, checkW, btnH, TRUE);
    MoveWindow(g.pasteButton, pasteX, exitY, pasteW, btnH, TRUE);
    MoveWindow(g.editButton, editX, exitY, editW, btnH, TRUE);
    MoveWindow(g.exitButton, exitX, exitY, exitW, btnH, TRUE);

    const int statusY = clientH - statusH;
    // Status line on the left, "About" link at the right end of the same row
    const int linkW = linkSz.cx + Scale(4);
    MoveWindow(g.aboutLink, clientW - margin - linkW, statusY, linkW, lineH, TRUE);
    MoveWindow(g.status, margin, statusY, clientW - 2 * margin - linkW - gap, lineH, TRUE);

    RECT rc{ 0, 0, clientW, clientH };
    AdjustRectForDpi(rc);
    SetWindowPos(g.hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    // Keep the focus in the groups list if it's there (e.g. after F5 or a reload)
    ShowGroupButtons(GetFocus() != g.groupList);
}

// Last-write time and size of a file; false if the file can't be read.
bool GetFileStamp(const std::wstring& path, FILETIME& writeTime, ULONGLONG& size) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    writeTime = fad.ftLastWriteTime;
    size = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    return true;
}

// True if the JSON file has been saved since Clippi last loaded it.
bool FileChangedSinceLoad() {
    FILETIME t{};
    ULONGLONG size = 0;
    if (!g.haveStamp || !GetFileStamp(g.jsonPath, t, size)) return false;
    return CompareFileTime(&t, &g.loadedWriteTime) != 0 || size != g.loadedSize;
}

bool ReloadDefinitions() {
    g.jsonPath = FindJsonPath();
    // Remember the file's state even if loading fails, so a broken file is
    // reported once per save rather than every time Clippi is activated.
    g.haveStamp = GetFileStamp(g.jsonPath, g.loadedWriteTime, g.loadedSize);

    std::vector<ButtonDef> defs;
    std::wstring error;
    if (!LoadDefinitions(g.jsonPath, defs, error)) {
        MessageBoxW(g.hwnd, error.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
        LayoutWindow(); // keep whatever was loaded before
        SetStatus(L"Failed to load button definitions.");
        return false;
    }
    g.defs = std::move(defs);
    LayoutWindow();
    SetStatus(g.defs.empty() ? L"No buttons defined in the JSON file."
                             : std::to_wstring(g.defs.size()) + L" buttons in " +
                               std::to_wstring(g.groups.size()) +
                               (g.groups.size() == 1 ? L" group" : L" groups") + L" loaded.  F5 = reload");
    return true;
}

// ---- "Edit Clips": open the JSON file in the editor, reload when it is closed ----

// Runs on a thread-pool thread when the editor process exits. The context is
// the launch number, so a late message from an earlier editor can be ignored.
VOID CALLBACK OnEditorExited(PVOID context, BOOLEAN) {
    PostMessageW(g.hwnd, WM_APP_EDITOR_CLOSED, reinterpret_cast<WPARAM>(context), 0);
}

void StopWatchingEditor() {
    if (g.editorWait) {
        UnregisterWaitEx(g.editorWait, INVALID_HANDLE_VALUE); // waits for a running callback to finish
        g.editorWait = nullptr;
    }
    if (g.editorProcess) {
        CloseHandle(g.editorProcess);
        g.editorProcess = nullptr;
    }
}

// Folder containing Clippi's .exe, with a trailing backslash
std::wstring ExeDirectory() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);
    }
    const size_t slash = buf.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : buf.substr(0, slash + 1);
}

// Brings the editor's window to the front if the editor we started is still open.
bool ActivateRunningEditor() {
    if (!g.editorProcess || WaitForSingleObject(g.editorProcess, 0) != WAIT_TIMEOUT) return false;
    struct Find { DWORD pid; HWND found; } f{ GetProcessId(g.editorProcess), nullptr };
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* f = reinterpret_cast<Find*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != f->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
        f->found = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&f));
    if (f.found) {
        if (IsIconic(f.found)) ShowWindow(f.found, SW_RESTORE);
        SetForegroundWindow(f.found);
    }
    SetStatus(L"The editor is already open.");
    return true;
}

// Opens the button file in Clippi Editor (ClippiEditor.exe next to Clippi's .exe),
// or in Notepad if the editor isn't there.
void OpenEditor() {
    if (ActivateRunningEditor()) return;
    StopWatchingEditor();
    g.jsonPath = FindJsonPath(); // also recreates the file if it was deleted

    const std::wstring editor = ExeDirectory() + L"ClippiEditor.exe";
    g.editorIsNotepad = !FileExists(editor);
    const std::wstring params = L"\"" + g.jsonPath + L"\"";
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd         = g.hwnd;
    sei.lpVerb       = L"open";
    sei.lpFile       = g.editorIsNotepad ? L"notepad.exe" : editor.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        MessageBoxW(g.hwnd, (L"Could not open the editor for:\n" + g.jsonPath).c_str(),
                    kAppTitle, MB_OK | MB_ICONWARNING);
        return;
    }

    g.editorStartTick = GetTickCount64();
    g.haveEditStartStamp = GetFileStamp(g.jsonPath, g.editStartWriteTime, g.editStartSize);
    g.editorProcess = sei.hProcess;
    ++g.editorSession;
    if (g.editorProcess &&
        !RegisterWaitForSingleObject(&g.editorWait, g.editorProcess, OnEditorExited,
                                     reinterpret_cast<PVOID>(g.editorSession),
                                     INFINITE, WT_EXECUTEONLYONCE)) {
        g.editorWait = nullptr;
    }
    SetStatus(g.editorIsNotepad ? L"Editing clips in Notepad... buttons reload when it is closed."
                                : L"Editing clips... buttons reload when the editor is closed.");
}

// Called when the editor process we launched has exited.
void OnEditorClosed() {
    const bool quickExit = GetTickCount64() - g.editorStartTick < 3000;
    StopWatchingEditor();

    // Saved during this editing session? (Switching back to Clippi may already
    // have reloaded the buttons, so compare with the file as it was when the
    // editor opened, not only with the last load.)
    FILETIME t{};
    ULONGLONG size = 0;
    const bool savedDuringEdit = g.haveEditStartStamp && GetFileStamp(g.jsonPath, t, size) &&
        (CompareFileTime(&t, &g.editStartWriteTime) != 0 || size != g.editStartSize);

    if (FileChangedSinceLoad()) {
        if (ReloadDefinitions())
            SetStatus(L"Clips updated: " + std::to_wstring(g.defs.size()) + L" buttons loaded.");
    } else if (savedDuringEdit) {
        SetStatus(L"Clips updated: " + std::to_wstring(g.defs.size()) + L" buttons loaded.");
    } else if (quickExit && g.editorIsNotepad) {
        // Windows 11 Notepad hands the file to an already-open Notepad window and
        // the process we started exits at once. Changes are then picked up when
        // you switch back to Clippi (see WM_ACTIVATEAPP).
        SetStatus(L"Editing in Notepad... changes load when you return to Clippi.");
    } else {
        SetStatus(L"Editor closed. No changes.");
    }
}

void OnButtonClicked(size_t index) {
    if (index >= g.defs.size()) return;
    const ButtonDef& def = g.defs[index];

    // Asks for one value (remembering the last answer per prompt title).
    // Returns false if the user cancelled.
    auto ask = [](const std::wstring& title, bool optional, std::wstring& value) {
        if (title.empty()) return true;
        InputDlgData data{ title, g.lastInput[title], optional };
        if (!PromptForInput(g.hwnd, data)) return false;
        g.lastInput[title] = data.value;
        value = data.value;
        return true;
    };
    std::wstring input, input2;
    // The second prompt is optional (e.g. a version number that isn't always known)
    if (!ask(def.inputTitle, false, input) || !ask(def.inputTitle2, true, input2)) {
        SetStatus(L"Cancelled.");
        return;
    }

    if (CopyToClipboard(g.hwnd, PrepareText(def.pasteText, def.rawText, input, input2), input, input2))
        SetStatus(L"Copied: " + def.text);
    else
        MessageBoxW(g.hwnd, L"Could not copy text to the clipboard.", kAppTitle, MB_OK | MB_ICONWARNING);
}

// ---- "Paste": paste the clipboard into the window used before Clippi ----

// Windows that should never be the paste target: the taskbar, desktop,
// Start menu / search and the Alt+Tab switcher.
bool IsShellWindow(HWND hwnd) {
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    static const wchar_t* kShell[] = {
        L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW",
        L"Windows.UI.Core.CoreWindow", L"MultitaskingViewFrame", L"TaskSwitcherWnd",
        L"XamlExplorerHostIslandWindow", L"ForegroundStaging", L"NotifyIconOverflowWindow",
    };
    for (const wchar_t* name : kShell)
        if (wcscmp(cls, name) == 0) return true;
    return false;
}

// True for Clippi Editor's windows (and anything else in the editor process we
// started, such as Notepad when it is used instead), which hold the button file
// rather than the text you want to paste into.
bool IsEditorWindow(HWND hwnd, DWORD pid) {
    HWND root = GetAncestor(hwnd, GA_ROOTOWNER);
    wchar_t cls[64] = {};
    GetClassNameW(root ? root : hwnd, cls, ARRAYSIZE(cls));
    if (wcscmp(cls, L"ClippiEditorWnd") == 0) return true; // also if started outside Clippi
    return g.editorProcess && pid == GetProcessId(g.editorProcess);
}

// Remembers hwnd as the paste target if it's a normal window of another program.
void ConsiderPasteTarget(HWND hwnd) {
    if (!hwnd || !IsWindowVisible(hwnd) || IsShellWindow(hwnd)) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return; // Clippi itself or its dialogs
    if (IsEditorWindow(hwnd, pid)) return;    // keep the previous target
    g.pasteTarget = hwnd;
}

// Called by Windows whenever a different window comes to the front.
void CALLBACK OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (idObject == OBJID_WINDOW) ConsiderPasteTarget(hwnd);
}

std::wstring WindowTitle(HWND hwnd) {
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, ARRAYSIZE(title));
    return title[0] ? title : L"(untitled window)";
}

void PasteIntoTarget() {
    HWND target = g.pasteTarget;
    if (!target || !IsWindow(target)) {
        g.pasteTarget = nullptr;
        SetStatus(L"Nothing to paste into. Click in the target window first.");
        return;
    }
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) && !IsClipboardFormatAvailable(CF_BITMAP) &&
        !IsClipboardFormatAvailable(CF_DIB) && !IsClipboardFormatAvailable(CF_HDROP)) {
        SetStatus(L"The clipboard is empty.");
        return;
    }

    // If Alt+P was used, wait until Alt/Ctrl/Shift/Win are released so they
    // don't combine with the Ctrl+V we send (up to 1 second).
    for (int i = 0; i < 50; ++i) {
        if (!(GetAsyncKeyState(VK_MENU) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_LWIN) & 0x8000) &&
            !(GetAsyncKeyState(VK_RWIN) & 0x8000))
            break;
        Sleep(20);
    }

    // Bring the target to the front (Clippi is in front, so Windows allows this)
    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
    SetForegroundWindow(target);
    for (int i = 0; i < 50 && GetForegroundWindow() == g.hwnd; ++i) Sleep(10);
    if (GetForegroundWindow() == g.hwnd) {
        SetStatus(L"Could not switch to " + WindowTitle(target) + L".");
        return;
    }
    Sleep(50); // let the target restore keyboard focus to its text box

    // Ctrl+V
    INPUT in[4] = {};
    for (INPUT& i : in) i.type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = 'V';
    in[2].ki.wVk = 'V';     in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(ARRAYSIZE(in), in, sizeof(INPUT)) != ARRAYSIZE(in)) {
        SetStatus(L"Paste was blocked by Windows.");
        return;
    }
    SetStatus(L"Pasted into: " + WindowTitle(target));
}

// ---- About box (opened by the "About" link or System menu > About Clippi) ----
void ShowAbout() {
    const wchar_t* text =
        L"Clippi - Clipboard Buttons\n\n"
        L"Version:\t" APP_VERSION_WSTR L"\n"
        L"Author:\t"  APP_AUTHOR_WSTR  L"\n"
        L"Date:\t"    APP_DATE_WSTR;
    MSGBOXPARAMSW mp{};
    mp.cbSize      = sizeof(mp);
    mp.hwndOwner   = g.hwnd;
    mp.hInstance   = g.inst;
    mp.lpszText    = text;
    mp.lpszCaption = L"About Clippi";
    mp.dwStyle     = MB_OK | MB_USERICON;
    mp.lpszIcon    = MAKEINTRESOURCEW(IDI_APPICON); // Clippi.ico
    MessageBoxIndirectW(&mp);
}

bool LoadTopmostSetting() {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"AlwaysOnTop", RRF_RT_REG_DWORD,
                        nullptr, &value, &size) == ERROR_SUCCESS && value != 0;
}

void SaveTopmostSetting(bool on) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) == ERROR_SUCCESS) {
        const DWORD value = on ? 1 : 0;
        RegSetValueExW(key, L"AlwaysOnTop", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
}

// Keeps the window above all normal windows (or not), and keeps the checkbox,
// the system-menu check mark and the saved setting in sync.
void SetTopmost(bool on) {
    g.topmost = on;
    SetWindowPos(g.hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    CheckMenuItem(GetSystemMenu(g.hwnd, FALSE), IDM_TOPMOST,
                  MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    SendMessageW(g.topmostCheck, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    SaveTopmostSetting(on);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        g.dpi = WindowDpi(hwnd); // DPI of the monitor the window was created on
        CreateFonts();           // controls don't exist yet; each gets the font below
        // Groups pane, created first so it is first in the Tab order.
        g.groupList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                      LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                      0, 0, 0, 0, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_GROUPS)),
                                      g.inst, nullptr);
        SendMessageW(g.groupList, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
        LoadLastGroup(g.groupName); // restore last session's group

        g.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX |
                                   SS_ENDELLIPSIS, 0, 0, 0, 0, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)),
                                   g.inst, nullptr);
        SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);

        g.exitButton = CreateWindowExW(0, L"BUTTON", L"E&xit",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       0, 0, 0, 0, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXIT)),
                                       g.inst, nullptr);
        SendMessageW(g.exitButton, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);

        g.editButton = CreateWindowExW(0, L"BUTTON", L"&Edit Clips",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       0, 0, 0, 0, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT)),
                                       g.inst, nullptr);
        SendMessageW(g.editButton, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);

        g.pasteButton = CreateWindowExW(0, L"BUTTON", L"&Paste",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                        0, 0, 0, 0, hwnd,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PASTE)),
                                        g.inst, nullptr);
        SendMessageW(g.pasteButton, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);

        // Track the window in front so Paste knows where to go. The window that
        // was in front when Clippi started is the first target.
        ConsiderPasteTarget(GetForegroundWindow());
        g.foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                           OnForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);

        // "About" link: a static control drawn like a hyperlink (blue, underlined,
        // hand cursor). SS_NOTIFY makes it send STN_CLICKED when clicked.
        g.aboutLink = CreateWindowExW(0, L"STATIC", L"About",
                                      WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_RIGHT | SS_NOPREFIX,
                                      0, 0, 0, 0, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ABOUT)),
                                      g.inst, nullptr);
        SendMessageW(g.aboutLink, WM_SETFONT, reinterpret_cast<WPARAM>(g.linkFont), FALSE);

        g.topmostCheck = CreateWindowExW(0, L"BUTTON", L"Always on &top",
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         0, 0, 0, 0, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TOPMOST)),
                                         g.inst, nullptr);
        SendMessageW(g.topmostCheck, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);

        HMENU sys = GetSystemMenu(hwnd, FALSE);
        AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sys, MF_STRING, IDM_TOPMOST, L"Always on &Top");
        AppendMenuW(sys, MF_STRING, IDM_RELOAD, L"&Reload Buttons\tF5");
        AppendMenuW(sys, MF_STRING, IDM_ABOUT, L"&About Clippi...");

        ReloadDefinitions();
        SetTopmost(LoadTopmostSetting()); // restore last session's choice
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == IDA_RELOAD) { ReloadDefinitions(); return 0; }
        if (id == IDA_NEXT_GROUP) { StepGroup(+1); return 0; }
        if (id == IDA_PREV_GROUP) { StepGroup(-1); return 0; }
        if (id == IDC_GROUPS && HIWORD(wp) == LBN_SELCHANGE) {
            const LRESULT sel = SendMessageW(g.groupList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && static_cast<size_t>(sel) != g.group)
                SelectGroup(static_cast<size_t>(sel), false); // keep focus in the list
            return 0;
        }
        if (id == IDC_EXIT)   { DestroyWindow(hwnd); return 0; }
        if (id == IDC_EDIT && HIWORD(wp) == BN_CLICKED) { OpenEditor(); return 0; }
        if (id == IDC_PASTE && HIWORD(wp) == BN_CLICKED) { PasteIntoTarget(); return 0; }
        if (id == IDC_ABOUT && HIWORD(wp) == STN_CLICKED) { ShowAbout(); return 0; }
        if (id == IDC_TOPMOST && HIWORD(wp) == BN_CLICKED) {
            SetTopmost(SendMessageW(g.topmostCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            return 0;
        }
        if (id >= IDC_FIRST_BUTTON && HIWORD(wp) == BN_CLICKED) {
            OnButtonClicked(static_cast<size_t>(id - IDC_FIRST_BUTTON));
            return 0;
        }
        break;
    }
    case WM_SYSCOMMAND:
        switch (wp & 0xFFF0) {
        case IDM_TOPMOST: SetTopmost(!g.topmost); return 0;
        case IDM_RELOAD:  ReloadDefinitions(); return 0;
        case IDM_ABOUT:   ShowAbout(); return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lp) == g.aboutLink) { // draw the About link in link blue
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, RGB(0, 102, 204)); // standard Windows hyperlink blue
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wp) == g.aboutLink) { // hand cursor over the link
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_DPICHANGED: {
        // Moved to a monitor with a different scale: new fonts, then re-layout
        // (LayoutWindow sizes the window itself; use the suggested position).
        HFONT oldFont = g.font, oldLink = g.linkFont;
        g.dpi = HIWORD(wp);
        CreateFonts();
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutWindow(); // recreates the group buttons with the new font
        DeleteFont(oldFont);
        DeleteFont(oldLink);
        return 0;
    }
    case WM_APP_EDITOR_CLOSED:
        // Ignore it if it's from an editor that was replaced by a newer one
        if (static_cast<UINT_PTR>(wp) == g.editorSession && g.editorProcess) OnEditorClosed();
        return 0;
    case WM_ACTIVATEAPP:
        // Returning to Clippi: catch edits saved in an editor we aren't tracking.
        // Posted so any error box appears after activation has finished.
        if (wp) PostMessageW(hwnd, WM_APP_CHECK_FILE, 0, 0);
        break;
    case WM_APP_CHECK_FILE:
        if (FileChangedSinceLoad() && ReloadDefinitions())
            SetStatus(L"Clips updated: " + std::to_wstring(g.defs.size()) + L" buttons loaded.");
        return 0;
    case WM_DESTROY:
        if (g.foregroundHook) UnhookWinEvent(g.foregroundHook);
        StopWatchingEditor();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetDpiAwareness(); // per-monitor v2 where available

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g.inst = hInstance;
    HDC screen = GetDC(nullptr);
    g_systemDpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    g.dpi = g_systemDpi; // updated in WM_CREATE to the window's monitor

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    // Clippi.ico (from ClipBoardButtons.rc), loaded at the right sizes for
    // the Alt+Tab/taskbar icon and the small title-bar icon.
    wc.hIcon   = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!wc.hIcon)   wc.hIcon   = LoadIconW(nullptr, IDI_APPLICATION); // fallback if the resource is missing
    if (!wc.hIconSm) wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(kMainExStyle, kWndClass, kAppTitle, kMainStyle,
                                CW_USEDEFAULT, CW_USEDEFAULT, Scale(260), Scale(200),
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    ACCEL accel[] = {
        { FVIRTKEY,                     VK_F5,    IDA_RELOAD     },
        { FVIRTKEY | FCONTROL,          VK_TAB,   IDA_NEXT_GROUP },
        { FVIRTKEY | FCONTROL | FSHIFT, VK_TAB,   IDA_PREV_GROUP },
        { FVIRTKEY | FCONTROL,          VK_NEXT,  IDA_NEXT_GROUP },
        { FVIRTKEY | FCONTROL,          VK_PRIOR, IDA_PREV_GROUP },
    };
    HACCEL hAccel = CreateAcceleratorTableW(accel, ARRAYSIZE(accel));

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, hAccel, &m)) continue;
        if (IsDialogMessageW(hwnd, &m)) continue; // Tab / arrow keys / Enter between buttons
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    DestroyAcceleratorTable(hAccel);
    DeleteFont(g.font);
    DeleteFont(g.linkFont);
    return static_cast<int>(m.wParam);
}
