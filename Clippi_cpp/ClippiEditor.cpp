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
//   HTML (Teams, browsers, web mail) is converted - text styles, links, lists,
//   tables, paragraph spacing and indentation, and pictures that are embedded
//   or local files - and a copied picture (e.g. a screenshot) is added as PNG.
//   Pictures that are only a web address are not downloaded.
//   Ctrl+Shift+V pastes plain text only.
//   "Insert from HTML/RTF file" adds a file's contents at the cursor (pictures
//   next to an .html file, as Word's "Save as Web Page" makes, come along);
//   "Export RTF" / "Export HTML" save the current clip's formatted version.
//
// Saving (Save button or Ctrl+S) writes UTF-8 JSON, keeps any fields Clippi
// doesn't use, and first copies the previous file to *.bak.
//   - A clip without formatting is stored as plain text, as before (line
//     breaks as \\n; "rawText": true if the text itself contains \n or \t).
//   - A formatted clip is stored as:
//       plain text  for Notepad and other programs (links as "text <url>")
//       %RTF:...%   for Outlook and Word (the box's own RTF)
//     Clippi builds the HTML version for Teams and browsers from the RTF when
//     the button is clicked, so each picture is stored only once.
//   - A clip whose text wasn't changed is saved exactly as it was loaded.
//     Clips from older versions (%IMG% pictures, %HTML% only, or %RTF% plus
//     %HTML%) are shown formatted and saved as text + %RTF% when edited.
// Closing with unsaved changes asks whether to save.
//
// Shared with Clippi: ClippiCommon.h (JSON parser, file/text helpers, Base64,
// PNG, DialogTemplate) and ClippiRichText.h (document model, RTF/HTML/plain
// converters, picture helpers).
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
#include "ClippiCommon.h"   // JSON parser, file/text helpers, Base64, PNG, DialogTemplate
#include "ClippiRichText.h" // document model, RTF/HTML/plain converters, picture helpers

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

using namespace clippi;

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
// JSON writer (the parser is in ClippiCommon.h, shared with Clippi)
// ============================================================================
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
bool WriteFileBytes(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
    CloseHandle(h);
    return ok;
}

std::wstring FormatSize(size_t bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + L" bytes";
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    return Widen(buf);
}

std::wstring DefaultJsonPath() {
    const std::wstring folder = StorageFolder(); // same place Clippi uses
    return folder.empty() ? std::wstring(kJsonFileName) : folder + L"\\" + kJsonFileName;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}


// ============================================================================
// Model: one Clip per button
// ----------------------------------------------------------------------------
// In the JSON, pasteText holds the plain text (line breaks written as \n) plus,
// for a formatted clip, a %RTF:<base64>% token. Clippi makes the HTML version
// from that RTF. (Older files may also have %HTML:<base64>% or %IMG:<base64>%.)
// In the editor a clip is either plain text or RTF (the Rich Edit box's own
// format); the plain-text version of a formatted clip is generated when saving.
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
        if (c.rich) // HTML isn't stored: Clippi builds it from the RTF, so pictures are stored once
            paste += L"%RTF:" + Base64Encode(std::vector<unsigned char>(c.rtf.begin(), c.rtf.end())) + L"%";
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
    // Plain paragraphs: left-aligned, no spacing or indent (anything else counts as formatting)
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_ALIGNMENT | PFM_SPACEBEFORE | PFM_SPACEAFTER | PFM_STARTINDENT | PFM_OFFSET | PFM_RIGHTINDENT;
    pf.wAlignment = PFA_LEFT;
    SendMessageW(g.box, EM_SETSEL, 0, -1);
    SendMessageW(g.box, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
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
                                L" picture(s) on the web or not found were left out; copy them as pictures instead."
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
    // Paste is available when the box can paste the clipboard itself, or when PasteSpecial
    // can (HTML from Teams/browsers, a picture)
    static const UINT cfHtml = RegisterClipboardFormatW(L"HTML Format");
    const bool canPaste = SendMessageW(box, EM_CANPASTE, 0, 0) != 0 ||
                          IsClipboardFormatAvailable(cfHtml) || IsClipboardFormatAvailable(CF_DIB);
    AppendMenuW(m, canPaste ? MF_STRING : MF_GRAYED, 4, L"&Paste");
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
// A small text-input dialog (DialogTemplate from ClippiCommon.h)
// ============================================================================
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
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring folder = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    const Doc d = HtmlToDoc(ExtractHtmlFragment(ToUtf8(BytesToText(bytes))), &skipped, folder);
    if (!DocHasContent(d)) {
        MessageBoxW(g.hwnd, L"This file has no text or pictures to insert. Use an .rtf file (Word: Save As > "
                            L"Rich Text Format) or an .html file.", kAppTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }
    InsertRtf(DocToRtf(d, true));
    SetStatus(L"Inserted " + FileNameOf(path) + L"." +
              (skipped ? L" " + std::to_wstring(skipped) + L" picture(s) on the web or not found were left out." : L""));
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
                L"Outlook and Word paste the RTF, Teams and browsers an HTML copy Clippi makes from it, other "
                L"programs plain text. Ctrl+Shift+V pastes without formatting; Ctrl+L/E/R align.",
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
