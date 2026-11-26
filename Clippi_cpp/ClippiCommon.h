// ClippiCommon.h - code shared by Clippi (ClipBoardButtons.cpp) and Clippi Editor
// (ClippiEditor.cpp), so both programs read the button file the same way.
//
//   JSON        JsonValue, JsonError, JsonParser (objects, arrays, strings,
//               numbers, true/false/null; errors report line and column)
//   Files       kJsonFileName, StorageFolder, FileExists, ReadFileBytes,
//               BytesToText (UTF-8 with/without BOM, UTF-16LE with BOM, ANSI)
//   Text        ReplaceAll, Trim, ToUtf8, FromBytes
//   Data        Base64Encode, Base64Decode, IsPng, PngSize
//   Dialogs     DialogTemplate (dialog template built in memory)
//
// Include after <windows.h>. Everything is in namespace clippi; each .cpp file
// says "using namespace clippi;". Header-only (inline), so nothing extra to add
// to the Visual Studio projects besides this file.

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace clippi {

constexpr wchar_t kJsonFileName[] = L"ClipBoardButtonDefinitions.json";

// Largest button file either program reads (pictures make the file big)
constexpr long long kMaxFileBytes = 32LL * 1024 * 1024;

// Deepest nesting of [ ] / { } the JSON parser accepts (a button file needs 2)
constexpr int kMaxJsonDepth = 100;

// ============================================================================
// JSON parser (objects, arrays, strings, numbers, true/false/null)
// Numbers follow the JSON grammar exactly (no +1, .5, 1., inf, nan, hex), and
// nesting deeper than kMaxJsonDepth is an error instead of a stack overflow.
// Control characters inside strings are accepted (strict JSON would reject
// them), so hand-edited files with a raw tab still load.
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
    int depth_ = 0; // current [ ] / { } nesting

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
        case L'{': Enter(); v.type = JsonValue::Object; ParseObject(v); --depth_; break;
        case L'[': Enter(); v.type = JsonValue::Array;  ParseArray(v);  --depth_; break;
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

    void Enter() {
        if (++depth_ > kMaxJsonDepth) Fail(L"Nested too deeply");
    }

    bool IsDigitAt(size_t i) const { return i < s_.size() && s_[i] >= L'0' && s_[i] <= L'9'; }
    void SkipDigits() { while (IsDigitAt(p_)) ++p_; }

    // number = [ "-" ] ( "0" / digit1-9 *digit ) [ "." 1*digit ] [ ( "e" / "E" ) [ "+" / "-" ] 1*digit ]
    void ParseNumber(JsonValue& v) {
        const size_t start = p_;
        if (p_ < s_.size() && s_[p_] == L'-') ++p_;
        if (!IsDigitAt(p_)) { p_ = start; Fail(L"Unexpected character"); }
        if (s_[p_] == L'0') {
            ++p_;
            if (IsDigitAt(p_)) Fail(L"Invalid number: leading zero");
        } else {
            SkipDigits();
        }
        if (p_ < s_.size() && s_[p_] == L'.') {
            ++p_;
            if (!IsDigitAt(p_)) Fail(L"Invalid number: digits expected after '.'");
            SkipDigits();
        }
        if (p_ < s_.size() && (s_[p_] == L'e' || s_[p_] == L'E')) {
            ++p_;
            if (p_ < s_.size() && (s_[p_] == L'+' || s_[p_] == L'-')) ++p_;
            if (!IsDigitAt(p_)) Fail(L"Invalid number: digits expected in the exponent");
            SkipDigits();
        }
        v.type = JsonValue::Number;
        v.number = wcstod(s_.substr(start, p_ - start).c_str(), nullptr); // already validated
    }
};

// ============================================================================
// Text
// ============================================================================
inline void ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::wstring::npos; pos += to.size())
        s.replace(pos, from.size(), to);
}

inline std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

inline std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// Bytes as UTF-8, or the ANSI code page if they aren't valid UTF-8
inline std::wstring FromBytes(const char* src, size_t size) {
    if (size == 0) return std::wstring();
    UINT codePage = CP_UTF8;
    int len = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, src, static_cast<int>(size), nullptr, 0);
    if (len == 0) { codePage = CP_ACP; len = MultiByteToWideChar(codePage, 0, src, static_cast<int>(size), nullptr, 0); }
    std::wstring out(static_cast<size_t>(len), L'\0');
    if (len > 0) MultiByteToWideChar(codePage, 0, src, static_cast<int>(size), &out[0], len);
    return out;
}

// ============================================================================
// Files
// ============================================================================
inline bool FileExists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// %USERPROFILE%\Clippi_Storage without a trailing backslash (empty if USERPROFILE isn't set)
inline std::wstring StorageFolder() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(L"%USERPROFILE%\\Clippi_Storage", buf, ARRAYSIZE(buf));
    if (n == 0 || n > ARRAYSIZE(buf) || wcschr(buf, L'%')) return std::wstring();
    return buf;
}

// Reads a whole file (up to kMaxFileBytes). On failure, error says why.
inline bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out, std::wstring& error) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { error = L"Cannot open file:\n" + path; return false; }
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > kMaxFileBytes) {
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

// Text file contents: UTF-16LE (with BOM), UTF-8 (BOM optional), or ANSI
inline std::wstring BytesToText(const std::vector<unsigned char>& b) {
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE)
        return std::wstring(reinterpret_cast<const wchar_t*>(b.data() + 2), (b.size() - 2) / 2);
    const size_t off = (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
    return FromBytes(reinterpret_cast<const char*>(b.data()) + off, b.size() - off);
}

// ============================================================================
// Base64 and PNG
// ============================================================================
inline std::wstring Base64Encode(const std::vector<unsigned char>& data) {
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

inline std::vector<unsigned char> Base64Decode(const std::wstring& s) {
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

inline bool IsPng(const std::vector<unsigned char>& d) {
    static const unsigned char kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    return d.size() >= 8 && memcmp(d.data(), kSig, 8) == 0;
}

// Width/height from a PNG's IHDR chunk. Returns false if it isn't a PNG.
inline bool PngSize(const std::vector<unsigned char>& png, int& w, int& h) {
    if (png.size() < 24 || !IsPng(png) || memcmp(&png[12], "IHDR", 4) != 0) return false;
    auto be32 = [&](size_t i) {
        return static_cast<int>((png[i] << 24) | (png[i + 1] << 16) | (png[i + 2] << 8) | png[i + 3]);
    };
    w = be32(16);
    h = be32(20);
    return w > 0 && h > 0;
}

// ============================================================================
// Dialog template built in memory, so no .rc dialog resource is needed
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

} // namespace clippi
