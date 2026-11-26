// ClippiRichText.h - formatted text shared by Clippi and Clippi Editor
//
// One document model (Doc: paragraphs of runs) that is read from RTF or HTML
// and written as RTF, HTML or plain text:
//   RtfToDoc   reads the RTF that Rich Edit, Word and Outlook write
//   HtmlToDoc  reads HTML from Teams, browsers, web mail and .html files
//   DocToRtf / DocToHtml / DocToPlain / IsPlainDoc
// Clippi Editor keeps a formatted button as RTF (the Rich Edit box's own
// format). Clippi builds the HTML version from that RTF when a button is
// clicked, so each picture is stored only once in the button file.
//
// Also here: picture helpers (WIC: any picture -> PNG; GDI: metafiles and
// DIBs drawn into a PNG). The process must call CoInitializeEx first.
//
// Include after <windows.h> and ClippiCommon.h. Everything is in namespace clippi.

#pragma once

#include <wincodec.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include "ClippiCommon.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "uuid.lib")

namespace clippi {

inline std::wstring ToLowerW(std::wstring s) {
    for (wchar_t& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    return s;
}

inline std::string ToLowerA(std::string s) {
    for (char& ch : s) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    return s;
}

inline bool IEquals(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

inline bool StartsWithI(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

inline int HexVal(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// CRLF line breaks (the form edit controls and the plain-text clip use)
inline std::wstring LfToCrlf(std::wstring s) {
    s.erase(std::remove(s.begin(), s.end(), L'\r'), s.end());
    ReplaceAll(s, L"\n", L"\r\n");
    return s;
}

// Reduces a whole HTML page to the part that should be pasted: the clipboard
// fragment if it has one, otherwise the contents of <body>.
inline std::string ExtractHtmlFragment(const std::string& html) {
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
inline std::string CfHtmlFragment(const std::string& cf) {
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

inline bool JpegSize(const std::vector<unsigned char>& d, int& w, int& h) {
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

inline IWICImagingFactory* CreateWic() {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, reinterpret_cast<void**>(&f)))) return nullptr;
    return f;
}

// Encodes any WIC bitmap source as a 32-bit PNG
inline bool EncodePng(IWICImagingFactory* factory, IWICBitmapSource* src, std::vector<unsigned char>& png) {
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
inline bool ConvertFileToPng(const std::wstring& path, std::vector<unsigned char>& png) {
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
inline bool BytesToPng(const std::vector<unsigned char>& in, std::vector<unsigned char>& png) {
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
inline bool RenderToPng(int w, int h, const std::function<void(HDC, const RECT&)>& draw, std::vector<unsigned char>& png) {
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
inline size_t DibBitsOffset(const BITMAPINFOHEADER& h) {
    size_t off = h.biSize;
    if (h.biCompression == BI_BITFIELDS && h.biSize == sizeof(BITMAPINFOHEADER)) off += 3 * sizeof(DWORD);
    const DWORD colors = h.biClrUsed ? h.biClrUsed : (h.biBitCount <= 8 ? (1u << h.biBitCount) : 0);
    return off + colors * sizeof(RGBQUAD);
}

// Packed DIB (clipboard CF_DIB, RTF \dibitmap) -> PNG at w x h (0 = its own size)
inline bool DibToPng(const std::vector<unsigned char>& dib, int w, int h, std::vector<unsigned char>& png) {
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

inline bool IsDefaultFace(const std::wstring& f) { return f.empty() || IEquals(f, kDefaultFace); }
inline bool IsDefaultSize(int halfPoints) { return halfPoints == 0 || halfPoints == kDefaultHalfPoints; }
inline bool IsDefaultColor(COLORREF c) { return c == kAuto || c == RGB(0, 0, 0); }

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

// Break = line break inside a paragraph; Cell = end of a table cell (only in table rows)
enum class RunKind { Text, Break, Picture, Cell };
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
    int spaceBefore = 0, spaceAfter = 0; // twips (1/20 pt)
    int indent = 0;                      // left indent, twips
    bool row = false;                    // a table row: runs hold the cells, each ended by a Cell run
    std::vector<Run> runs;
};

struct Doc {
    std::vector<Para> paras{ Para{} }; // always at least one
};

inline void AddText(Para& p, const Fmt& f, const std::wstring& t) {
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
inline void AppendPlainToDoc(Doc& d, const std::wstring& t) {
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

inline bool DocHasContent(const Doc& d) {
    for (const Para& p : d.paras)
        for (const Run& r : p.runs)
            if (r.kind != RunKind::Text || !Trim(r.text).empty()) return true;
    return false;
}

// Drops empty paragraphs at the start and the end
inline void TrimDoc(Doc& d) {
    while (d.paras.size() > 1 && d.paras.back().runs.empty()) d.paras.pop_back();
    while (d.paras.size() > 1 && d.paras.front().runs.empty()) d.paras.erase(d.paras.begin());
}

// True if nothing in the doc needs RTF/HTML: default font, size and color,
// no bold/italic/..., no links, no pictures, no tables, left-aligned paragraphs
// without extra spacing or indentation.
inline bool IsPlainDoc(const Doc& d) {
    for (const Para& p : d.paras) {
        if (p.align != Align::Left || p.row || p.spaceBefore || p.spaceAfter || p.indent) return false;
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
inline std::wstring DocToPlain(const Doc& d) {
    std::wstring out;
    for (size_t p = 0; p < d.paras.size(); ++p) {
        if (p) out += L'\n';
        const bool row = d.paras[p].row;
        const auto& runs = d.paras[p].runs;
        for (size_t r = 0; r < runs.size();) {
            const Run& run = runs[r];
            if (run.kind == RunKind::Cell) { out += L'\t'; ++r; continue; } // cells separated by tabs
            if (run.kind == RunKind::Break) { out += row ? L' ' : L'\n'; ++r; continue; }
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
        if (row && !out.empty() && out.back() == L'\t') out.pop_back(); // no tab after the last cell
    }
    std::replace(out.begin(), out.end(), L'\x00A0', L' ');
    return out;
}

// ---- Doc -> RTF ----

inline std::string RtfEscape(const std::wstring& text) {
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

inline void AppendHex(std::string& out, const std::vector<unsigned char>& data) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < data.size(); ++i) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 0x0F];
        if (i % 64 == 63) out += "\r\n";
    }
}

// {\pict ...} for a PNG/JPEG picture run (picw/pich in pixels, goal in twips)
inline std::string RtfPicture(const Run& r) {
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
inline std::string DocToRtf(const Doc& d, bool inlineFirst = false) {
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
        const auto& runs = para.runs;
        const int cells = static_cast<int>(std::count_if(runs.begin(), runs.end(),
                                           [](const Run& x) { return x.kind == RunKind::Cell; }));
        const bool row = para.row && cells > 0;
        if (row) {
            // A table row: equal-width cells with thin borders, 6.5 inches wide in all
            const int width = 9360 / cells;
            body += "\\trowd\\trgaph108\\trleft0";
            for (int k = 1; k <= cells; ++k)
                body += "\\clbrdrt\\brdrs\\brdrw10\\clbrdrl\\brdrs\\brdrw10\\clbrdrb\\brdrs\\brdrw10"
                        "\\clbrdrr\\brdrs\\brdrw10\\cellx" + std::to_string(width * k);
            body += "\\pard\\intbl ";
        } else if (!(inlineFirst && p == 0)) {
            static const char* kAlign[] = { "\\ql", "\\qc", "\\qr", "\\qj" };
            body += std::string("\\pard") + kAlign[static_cast<int>(para.align)];
            if (para.spaceBefore) body += "\\sb" + std::to_string(para.spaceBefore);
            if (para.spaceAfter) body += "\\sa" + std::to_string(para.spaceAfter);
            if (para.indent) body += "\\li" + std::to_string(para.indent);
            body += " ";
        }
        for (size_t r = 0; r < runs.size();) {
            const Run& run = runs[r];
            if (run.kind == RunKind::Cell) { if (row) body += "\\cell "; ++r; continue; }
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
        if (row) body += "\\row\r\n";                          // \row ends the paragraph itself
        else if (p + 1 < d.paras.size()) body += "\\par\r\n";
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

inline std::wstring HtmlAttr(const std::wstring& s) {
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
inline std::wstring HtmlText(const std::wstring& text, wchar_t& prev) {
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

inline std::wstring CssColor(COLORREF c) {
    wchar_t buf[8];
    swprintf(buf, 8, L"#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

inline std::wstring CssFor(const Fmt& f) {
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

// Twips as a CSS length in points ("7.5pt")
inline std::wstring CssPt(int twips) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1fpt", twips / 20.0);
    std::wstring s = buf;
    if (s.size() > 4 && s.compare(s.size() - 4, 4, L".0pt") == 0) s.erase(s.size() - 4, 2);
    return s;
}

// HTML for runs [from, to) of a paragraph or table cell. any = something visible was written.
inline std::wstring HtmlRuns(const std::vector<Run>& runs, size_t from, size_t to, bool& any) {
    std::wstring h;
    wchar_t prev = L'\n';
    std::wstring openLink;
    any = false;
    for (size_t i = from; i < to; ++i) {
        const Run& run = runs[i];
        if (run.kind == RunKind::Cell) continue;
        if (run.kind == RunKind::Break) { h += L"<br>"; prev = L'\n'; continue; }
        const std::wstring link = run.kind == RunKind::Text ? run.fmt.link : openLink;
        if (link != openLink) {
            if (!openLink.empty()) h += L"</a>";
            if (!link.empty()) h += L"<a href=\"" + HtmlAttr(link) + L"\">";
            openLink = link;
        }
        if (run.kind == RunKind::Picture) {
            h += std::wstring(L"<img src=\"data:image/") + (run.jpeg ? L"jpeg" : L"png") + L";base64," +
                 Base64Encode(run.data) + L"\" width=\"" + std::to_wstring(run.w) + L"\" height=\"" +
                 std::to_wstring(run.h) + L"\" alt=\"\">";
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
    return h;
}

// UTF-8 HTML fragment in the style Clippi uses (Calibri 11 pt), one <div> per
// paragraph (with its alignment, spacing and indentation) and a bordered
// <table> for table rows. Black and automatic text color are left out, so the
// text follows the reader's theme (dark mode in Teams).
inline std::string DocToHtml(const Doc& d) {
    std::wstring h = L"<div style=\"font-family:Calibri,Arial,sans-serif;font-size:11pt\">";
    bool inTable = false;
    for (const Para& para : d.paras) {
        const bool row = para.row &&
            std::any_of(para.runs.begin(), para.runs.end(), [](const Run& r) { return r.kind == RunKind::Cell; });
        if (row) {
            if (!inTable) { h += L"<table style=\"border-collapse:collapse\">"; inTable = true; }
            h += L"<tr>";
            size_t start = 0;
            for (size_t i = 0; i < para.runs.size(); ++i) {
                if (para.runs[i].kind != RunKind::Cell) continue;
                bool any = false;
                const std::wstring cell = HtmlRuns(para.runs, start, i, any);
                h += L"<td style=\"border:1px solid #bfbfbf;padding:2px 6px;vertical-align:top\">" +
                     (any ? cell : std::wstring(L"&nbsp;")) + L"</td>";
                start = i + 1;
            }
            h += L"</tr>";
            continue;
        }
        if (inTable) { h += L"</table>"; inTable = false; }

        std::wstring style;
        if (para.align == Align::Center) style += L"text-align:center;";
        else if (para.align == Align::Right) style += L"text-align:right;";
        else if (para.align == Align::Justify) style += L"text-align:justify;";
        if (para.spaceBefore) style += L"margin-top:" + CssPt(para.spaceBefore) + L";";
        if (para.spaceAfter) style += L"margin-bottom:" + CssPt(para.spaceAfter) + L";";
        if (para.indent) style += L"margin-left:" + CssPt(para.indent) + L";";
        h += style.empty() ? L"<div>" : L"<div style=\"" + style + L"\">";
        bool any = false;
        h += HtmlRuns(para.runs, 0, para.runs.size(), any);
        if (!any) h += L"<br>";
        h += L"</div>";
    }
    if (inTable) h += L"</table>";
    return ToUtf8(h + L"</div>");
}

// ---- RTF -> Doc ----

inline int CodePageForCharset(int charset, int ansi) {
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

inline std::wstring DecodeBytes(const std::string& b, int cp) {
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
inline bool PictToRun(const RtfPict& p, Run& run) {
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
inline Doc RtfToDoc(const std::string& rtf) {
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
    Align align = Align::Left;          // paragraph properties, reset by \pard
    int sb = 0, sa = 0, li = 0;         // space before / after, left indent (twips)
    bool inTable = false;               // \intbl: the paragraph is in a table cell
    // Gives the current paragraph the paragraph properties read so far
    auto setParaProps = [&]() {
        Para& p = doc.paras.back();
        p.align = align;
        p.spaceBefore = std::max(0, sb);
        p.spaceAfter = std::max(0, sa);
        p.indent = std::max(0, li);
    };
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
                if (inTable) { // a paragraph break inside a table cell: a line break in the cell
                    Run r; r.kind = RunKind::Break; doc.paras.back().runs.push_back(r);
                } else {
                    setParaProps();
                    doc.paras.emplace_back();
                }
            }
            return;
        }
        if (w == "cell" || w == "nestcell") {
            if (cur.dest == DNormal) {
                auto& runs = doc.paras.back().runs;
                if (!runs.empty() && runs.back().kind == RunKind::Break) runs.pop_back(); // cell ended with \par
                Run r; r.kind = RunKind::Cell; runs.push_back(r);
            }
            return;
        }
        if (w == "row" || w == "nestrow") {
            if (cur.dest == DNormal && !doc.paras.back().runs.empty()) {
                doc.paras.back().row = true;
                doc.paras.emplace_back();
            }
            return;
        }
        if (w == "intbl") { inTable = true; return; }
        if (w == "sb") { sb = param; return; }
        if (w == "sa") { sa = param; return; }
        if (w == "li") { li = param; return; }
        if (w == "line") {
            if (cur.dest == DNormal) { Run r; r.kind = RunKind::Break; doc.paras.back().runs.push_back(r); }
            return;
        }
        if (w == "pard") { align = Align::Left; sb = sa = li = 0; inTable = false; return; }
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
    if (!doc.paras.back().row) setParaProps();
    // Rich Edit ends every document with a paragraph mark; it isn't an extra line
    if (doc.paras.size() > 1 && doc.paras.back().runs.empty()) doc.paras.pop_back();
    return doc;
}

// ---- HTML -> Doc ----

inline std::wstring DecodeEntities(const std::wstring& s) {
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

inline bool ParseCssColor(const std::wstring& value, COLORREF& out) {
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
inline std::wstring CssFontFamily(const std::wstring& value) {
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
inline int CssFontSize(const std::wstring& value, int base) {
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

// CSS length -> twips (0 if not understood). em is taken as 11 pt.
inline int CssTwips(const std::wstring& value) {
    const std::wstring v = ToLowerW(Trim(value));
    wchar_t* end = nullptr;
    const double d = wcstod(v.c_str(), &end);
    if (end == v.c_str()) return 0;
    const std::wstring unit = Trim(end);
    double tw = 0;
    if (unit == L"pt") tw = d * 20;
    else if (unit == L"px" || unit.empty()) tw = d * 15;
    else if (unit == L"in") tw = d * 1440;
    else if (unit == L"cm") tw = d * 567;
    else if (unit == L"mm") tw = d * 56.7;
    else if (unit == L"em" || unit == L"rem") tw = d * 220;
    else return 0;
    return std::max(0, std::min(20000, static_cast<int>(tw + 0.5)));
}

// Paragraph spacing read from CSS margins (block elements only)
struct BlockSpacing {
    int spaceBefore = 0, spaceAfter = 0;
    int indent = 0;  // added to the enclosing indent
};

inline void ApplyCss(const std::wstring& css, Fmt& f, Align& align, BlockSpacing* block = nullptr) {
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
        else if (block && prop == L"margin") { // 1 to 4 values: top [right [bottom [left]]]
            std::vector<std::wstring> parts;
            size_t i = 0;
            while (i < lv.size()) {
                while (i < lv.size() && iswspace(lv[i])) ++i;
                const size_t j = i;
                while (i < lv.size() && !iswspace(lv[i])) ++i;
                if (i > j) parts.push_back(lv.substr(j, i - j));
            }
            if (!parts.empty()) {
                const size_t n = parts.size();
                block->spaceBefore = CssTwips(parts[0]);
                block->spaceAfter = CssTwips(parts[n >= 3 ? 2 : 0]);
                block->indent = CssTwips(parts[n == 4 ? 3 : (n >= 2 ? 1 : 0)]);
            }
        }
        else if (block && prop == L"margin-top") block->spaceBefore = CssTwips(lv);
        else if (block && prop == L"margin-bottom") block->spaceAfter = CssTwips(lv);
        else if (block && (prop == L"margin-left" || prop == L"padding-left")) block->indent += CssTwips(lv);
        else if (prop == L"text-align") {
            if (lv == L"center") align = Align::Center;
            else if (lv == L"right" || lv == L"end") align = Align::Right;
            else if (lv == L"justify") align = Align::Justify;
            else if (lv == L"left" || lv == L"start") align = Align::Left;
        }
    }
}

// "a%20b" -> "a b" (percent-encoded UTF-8, as in file: URLs)
inline std::wstring PercentDecode(const std::wstring& s) {
    const std::string u = ToUtf8(s);
    std::string out;
    for (size_t i = 0; i < u.size(); ++i) {
        if (u[i] == '%' && i + 2 < u.size() && HexVal(u[i + 1]) >= 0 && HexVal(u[i + 2]) >= 0) {
            out += static_cast<char>(HexVal(u[i + 1]) * 16 + HexVal(u[i + 2]));
            i += 2;
        } else {
            out += u[i];
        }
    }
    return FromBytes(out.data(), out.size());
}

// The local file an <img src> refers to: file:///C:/..., file://server/share/...,
// C:\..., or a path relative to baseDir. Empty for web addresses and anything else.
inline std::wstring LocalPicturePath(const std::wstring& src, const std::wstring& baseDir) {
    std::wstring path;
    if (StartsWithI(src, L"file:///")) path = src.substr(8);
    else if (StartsWithI(src, L"file://")) path = L"//" + src.substr(7);  // UNC: file://server/share
    else if (src.size() > 2 && src[1] == L':' && (src[2] == L'\\' || src[2] == L'/')) path = src;
    else if (src.find(L':') == std::wstring::npos && !StartsWithI(src, L"//") && !baseDir.empty())
        path = baseDir + L"\\" + src;  // relative, e.g. "Page_files/image001.png" from Word
    else return std::wstring();
    path = PercentDecode(path.substr(0, path.find_first_of(L"?#")));
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

// Converts HTML (UTF-8) to the document model: text, bold/italic/underline/
// strikeout, fonts, sizes, colors, highlight, links, line breaks, paragraphs
// with their alignment, spacing (CSS margins) and indentation (margins,
// <blockquote>, lists), lists (as bullets/numbers), tables (rows and cells),
// and pictures that are embedded (data:) or local files (file: URLs, and
// paths relative to baseDir - the folder of an .html file).
// skippedPictures counts pictures that are only a web address (not downloaded).
inline Doc HtmlToDoc(const std::string& htmlUtf8, int* skippedPictures = nullptr,
                     const std::wstring& baseDir = std::wstring()) {
    if (skippedPictures) *skippedPictures = 0;
    const std::wstring h = FromBytes(htmlUtf8.data(), htmlUtf8.size());
    Doc doc;
    struct El {
        std::wstring tag;
        Fmt fmt;
        Align align = Align::Left;
        bool pre = false;
        int list = -1;                    // ul: 0; ol: number of the next item
        int sb = 0, sa = 0, li = 0;       // paragraph spacing / indent (twips)
        bool inCell = false;              // inside a table cell
    };
    std::vector<El> st(1);
    int nestedTables = 0;                 // tables inside a cell are flattened
    bool space = true;  // the last thing added was a space or a line start (collapse more)

    auto para = [&]() -> Para& { return doc.paras.back(); };
    auto startContent = [&]() {
        if (!para().runs.empty() || para().row) return;
        const El& e = st.back();
        para().align = e.inCell ? Align::Left : e.align;
        if (!e.inCell) { para().spaceBefore = e.sb; para().spaceAfter = e.sa; para().indent = e.li; }
    };
    // A collapsed space at the end of a line isn't shown by browsers either
    auto trimLineEnd = [&]() {
        auto& runs = para().runs;
        if (!runs.empty() && runs.back().kind == RunKind::Text) {
            std::wstring& t = runs.back().text;
            while (!t.empty() && t.back() == L' ') t.pop_back();
            if (t.empty()) runs.pop_back();
        }
    };
    auto blockBreak = [&]() {
        trimLineEnd();
        if (st.back().inCell) { // a block inside a table cell: a line break within the cell
            auto& runs = para().runs;
            if (!runs.empty() && runs.back().kind != RunKind::Break && runs.back().kind != RunKind::Cell) {
                Run r; r.kind = RunKind::Break; runs.push_back(r);
            }
        } else if (!para().runs.empty()) {
            doc.paras.emplace_back();
        }
        space = true;
    };
    // Ends the current table cell
    auto endCell = [&]() {
        trimLineEnd();
        auto& runs = para().runs;
        if (!runs.empty() && runs.back().kind == RunKind::Break) runs.pop_back();
        startContent();
        Run r; r.kind = RunKind::Cell; runs.push_back(r);
        space = true;
    };
    // Index of the open <td>/<th> of the current row (0 if none)
    auto openCell = [&]() -> size_t {
        for (size_t k = st.size(); k-- > 1;) {
            if (st[k].tag == L"td" || st[k].tag == L"th") return k;
            if (st[k].tag == L"tr" || st[k].tag == L"table") return 0;
        }
        return 0;
    };
    // Ends the current table row (also when </td> or </tr> was left out)
    auto finishRow = [&]() {
        if (const size_t k = openCell()) { st.resize(k); endCell(); }
        for (size_t k = st.size(); k-- > 1;) {
            if (st[k].tag == L"tr") { st.resize(k); break; }
            if (st[k].tag == L"table") break;
        }
        const auto& runs = para().runs;
        if (std::any_of(runs.begin(), runs.end(), [](const Run& r) { return r.kind == RunKind::Cell; }))
            para().row = true;
    };
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
        int pw = 0, ph = 0;
        if (comma != std::wstring::npos && StartsWithI(head, L"data:image/") &&
            head.find(L";base64") != std::wstring::npos) {
            std::vector<unsigned char> data = Base64Decode(src.substr(comma + 1));
            if (PngSize(data, pw, ph)) { r.data = std::move(data); ok = true; }
            else if (JpegSize(data, pw, ph)) { r.data = std::move(data); r.jpeg = true; ok = true; }
            else if (BytesToPng(data, r.data) && PngSize(r.data, pw, ph)) ok = true; // GIF, BMP, ...
        } else {
            const std::wstring path = LocalPicturePath(src, baseDir);
            std::vector<unsigned char> data;
            std::wstring error;
            if (!path.empty() && FileExists(path) && ReadFileBytes(path, data, error)) {
                if (PngSize(data, pw, ph)) { r.data = std::move(data); ok = true; }
                else if (JpegSize(data, pw, ph)) { r.data = std::move(data); r.jpeg = true; ok = true; }
                else if (ConvertFileToPng(path, r.data) && PngSize(r.data, pw, ph)) ok = true;
            }
        }
        {
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

        // ---- Tables: rows become table rows, cells end with a Cell run. A table
        // inside a cell is flattened (its cells are separated by spaces). ----
        const bool tablePart = name == L"table" || name == L"tr" || name == L"td" || name == L"th" ||
                               name == L"tbody" || name == L"thead" || name == L"tfoot";
        if (tablePart && (nestedTables > 0 || (st.back().inCell && name == L"table" && !closing))) {
            if (name == L"table") nestedTables += closing ? -1 : 1;
            else if ((name == L"td" || name == L"th" || name == L"tr") && !space) addText(L" ");
            if (nestedTables < 0) nestedTables = 0;
            continue;
        }
        if (name == L"td" || name == L"th") {
            if (const size_t k = openCell()) { st.resize(k); endCell(); } // previous cell left open
            if (closing) continue;
        }
        if (name == L"tr" || (closing && tablePart && name != L"td" && name != L"th")) {
            finishRow(); // </tr>, a new <tr>, or the end of the table
            if (name == L"tr" && closing) { blockBreak(); continue; }
        }

        if (closing) {
            for (size_t k = st.size(); k-- > 1;)
                if (st[k].tag == name) { st.resize(k); if (block) blockBreak(); break; }
            continue;
        }

        if (block) blockBreak();
        El e = st.back();
        e.tag = name;
        e.list = -1;
        if (block) { e.sb = e.sa = 0; } // margins aren't inherited; the indent adds up
        Fmt& f = e.fmt;
        if (name == L"td" || name == L"th") {
            e.inCell = true;
            if (name == L"th") f.b = true;
        }
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
        else if (name == L"blockquote") e.li += 720;         // 0.5 inch
        else if (name == L"ul") { e.list = 0; e.li += 360; }  // nested lists indent further
        else if (name == L"ol") { e.list = 1; e.li += 360; }
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
        if (!style.empty()) {
            BlockSpacing sp;
            const bool spaced = block && !e.inCell;
            ApplyCss(style, f, e.align, spaced ? &sp : nullptr);
            if (spaced) { e.sb = sp.spaceBefore; e.sa = sp.spaceAfter; e.li += sp.indent; }
        }

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

} // namespace clippi
