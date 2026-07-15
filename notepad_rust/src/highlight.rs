// A small, dependency-free syntax highlighter for the rustpad editor.
//
// This is intentionally simple (a single-pass tokenizer, not a real parser),
// but it's enough to color keywords, strings, numbers, comments, and
// C-preprocessor directives for C/C++, Rust, Python, Visual Basic/VBScript,
// Batch, HTML, and ASP.NET — without pulling in a heavyweight crate like
// `syntect` and its large dependency tree.

use eframe::egui::{self, Color32, FontFamily, FontId};
use std::collections::HashSet;
use std::path::Path;
use std::sync::Arc;

#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Language {
    #[default]
    Plain,
    C,
    Cpp,
	Cs,
    Rust,
    Python,
    VisualBasic,
    Batch,
    Html,
    Aspx,
}

impl Language {
    pub const ALL: [Language; 10] = [
        Language::Plain,
        Language::C,
        Language::Cpp,
        Language::Cs,
        Language::Rust,
        Language::Python,
        Language::VisualBasic,
        Language::Batch,
        Language::Html,
        Language::Aspx,
    ];

    pub fn label(&self) -> &'static str {
        match self {
            Language::Plain => "Plain Text",
            Language::C => "C",
            Language::Cpp => "C++",
            Language::Cs => "C#",
            Language::Rust => "Rust",
            Language::Python => "Python",
            Language::VisualBasic => "Visual Basic",
            Language::Batch => "Batch",
            Language::Html => "HTML",
            Language::Aspx => "ASP.NET",
        }
    }
}

/// Guess a language from a file's extension. Returns `Language::Plain` for
/// anything unrecognized.
pub fn detect_language(path: &Path) -> Language {
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("")
        .to_lowercase();
    match ext.as_str() {
        "c" | "h" => Language::C,
        "cpp" | "cc" | "cxx" | "hpp" | "hh" | "hxx" => Language::Cpp,
        "cs" | "hs" => Language::Cs,
        "rs" => Language::Rust,
        "py" | "pyw" => Language::Python,
        "vb" | "vbs" => Language::VisualBasic,
        "bat" | "cmd" => Language::Batch,
        "htm" | "html" => Language::Html,
        "aspx" => Language::Aspx,
        _ => Language::Plain,
    }
}

const RUST_KEYWORDS: &[&str] = &[
    "as", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern", "false", "fn",
    "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref",
    "return", "self", "Self", "static", "struct", "super", "trait", "true", "type", "unsafe",
    "use", "where", "while", "async", "await", "union", "yield", "try",
];

const C_KEYWORDS: &[&str] = &[
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
    "enum", "extern", "float", "for", "goto", "if", "int", "long", "register", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while", "inline", "restrict", "_Bool", "true", "false", "NULL",
];

const CPP_ONLY_KEYWORDS: &[&str] = &[
    "class", "public", "private", "protected", "virtual", "namespace", "new", "delete",
    "template", "typename", "this", "using", "nullptr", "bool", "friend", "operator",
    "explicit", "mutable", "throw", "try", "catch", "constexpr", "override", "final",
    "static_cast", "dynamic_cast", "const_cast", "reinterpret_cast", "noexcept", "decltype",
    "auto",
];

const CS_KEYWORDS: &[&str] = &[
    "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked",
    "class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else",
    "enum", "event", "explicit", "extern", "false", "finally", "fixed", "float", "for",
    "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock",
    "long", "namespace", "new", "null", "object", "operator", "out", "override", "params",
    "private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
    "sizeof", "stackalloc", "static", "string", "struct", "switch", "this", "throw", "true",
    "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using", "virtual",
    "void", "volatile", "while",
    "add", "alias", "and", "ascending", "async", "await", "by", "descending", "dynamic",
    "equals", "from", "get", "global", "group", "init", "into", "join", "let", "nameof",
    "nint", "not", "notnull", "nuint", "on", "or", "orderby", "partial", "record", "remove",
    "select", "set", "unmanaged", "value", "var", "when", "where", "with", "yield",
];

const PYTHON_KEYWORDS: &[&str] = &[
    "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class",
    "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global",
    "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return",
    "try", "while", "with", "yield", "self",
];

// Stored upper-case; VB is case-insensitive, so lookups upper-case the word first.
const VB_KEYWORDS: &[&str] = &[
    "ADDHANDLER", "ADDRESSOF", "ALIAS", "AND", "ANDALSO", "AS", "BOOLEAN", "BYREF", "BYTE",
    "BYVAL", "CALL", "CASE", "CATCH", "CBOOL", "CBYTE", "CCHAR", "CDATE", "CDEC", "CDBL", "CHAR",
    "CINT", "CLASS", "CLNG", "COBJ", "CONST", "CONTINUE", "CSBYTE", "CSHORT", "CSNG", "CSTR",
    "CTYPE", "CUINT", "CULNG", "CUSHORT", "DATE", "DECIMAL", "DECLARE", "DEFAULT", "DELEGATE",
    "DIM", "DIRECTCAST", "DO", "DOUBLE", "EACH", "ELSE", "ELSEIF", "END", "ENDIF", "ENUM",
    "ERASE", "ERROR", "EVENT", "EXIT", "FALSE", "FINALLY", "FOR", "FRIEND", "FUNCTION", "GET",
    "GETTYPE", "GLOBAL", "GOSUB", "GOTO", "HANDLES", "IF", "IMPLEMENTS", "IMPORTS", "IN",
    "INHERITS", "INTEGER", "INTERFACE", "IS", "ISNOT", "LET", "LIB", "LIKE", "LONG", "LOOP",
    "ME", "MOD", "MODULE", "MUSTINHERIT", "MUSTOVERRIDE", "MYBASE", "MYCLASS", "NAMESPACE",
    "NARROWING", "NEW", "NEXT", "NOT", "NOTHING", "NOTINHERITABLE", "NOTOVERRIDABLE", "OBJECT",
    "OF", "ON", "OPERATOR", "OPTION", "OPTIONAL", "OR", "ORELSE", "OVERLOADS", "OVERRIDABLE",
    "OVERRIDES", "PARAMARRAY", "PARTIAL", "PRIVATE", "PROPERTY", "PROTECTED", "PUBLIC",
    "RAISEEVENT", "READONLY", "REDIM", "REM", "REMOVEHANDLER", "RESUME", "RETURN", "SBYTE",
    "SELECT", "SET", "SHADOWS", "SHARED", "SHORT", "SINGLE", "STATIC", "STEP", "STOP", "STRING",
    "STRUCTURE", "SUB", "SYNCLOCK", "THEN", "THROW", "TO", "TRUE", "TRY", "TRYCAST", "TYPEOF",
    "UINTEGER", "ULONG", "USHORT", "USING", "VARIANT", "WEND", "WHEN", "WHILE", "WIDENING",
    "WITH", "WITHEVENTS", "WRITEONLY", "XOR", "EXECUTEGLOBAL", "EXECUTE", "EVAL", "RANDOMIZE",
];

// Stored upper-case; batch is case-insensitive, so lookups upper-case the word first.
const BATCH_KEYWORDS: &[&str] = &[
    "ECHO", "OFF", "ON", "SET", "SETLOCAL", "ENDLOCAL", "IF", "ELSE", "NOT", "EXIST", "DEFINED",
    "ERRORLEVEL", "GOTO", "CALL", "FOR", "IN", "DO", "REM", "PAUSE", "CLS", "EXIT", "SHIFT",
    "START", "CD", "CHDIR", "MD", "MKDIR", "RD", "RMDIR", "DEL", "ERASE", "COPY", "XCOPY",
    "ROBOCOPY", "MOVE", "REN", "RENAME", "TYPE", "FIND", "FINDSTR", "PATH", "PUSHD", "POPD",
    "TITLE", "VER", "VOL", "ATTRIB", "ASSOC", "FTYPE", "COLOR", "CHOICE", "TIMEOUT", "TASKKILL",
    "TASKLIST", "NET", "REG", "WMIC", "POWERSHELL", "EQU", "NEQ", "LSS", "LEQ", "GTR", "GEQ",
];

struct Palette;
impl Palette {
    const KEYWORD: Color32 = Color32::from_rgb(197, 134, 192);
    const STRING: Color32 = Color32::from_rgb(206, 145, 120);
    const COMMENT: Color32 = Color32::from_rgb(106, 153, 85);
    const NUMBER: Color32 = Color32::from_rgb(181, 206, 168);
    const TYPE: Color32 = Color32::from_rgb(78, 201, 176);
    const MACRO: Color32 = Color32::from_rgb(212, 149, 106);
    const DEFAULT: Color32 = Color32::from_rgb(212, 212, 212);
}

fn keyword_set(language: Language) -> HashSet<&'static str> {
    match language {
        Language::Rust => RUST_KEYWORDS.iter().copied().collect(),
        Language::C => C_KEYWORDS.iter().copied().collect(),
        Language::Cpp => C_KEYWORDS
            .iter()
            .chain(CPP_ONLY_KEYWORDS.iter())
            .copied()
            .collect(),
		Language::Cs => CS_KEYWORDS.iter().copied().collect(),
        Language::Python => PYTHON_KEYWORDS.iter().copied().collect(),
        Language::VisualBasic => VB_KEYWORDS.iter().copied().collect(),
        Language::Batch => BATCH_KEYWORDS.iter().copied().collect(),
        Language::Html | Language::Aspx => HashSet::new(),
        Language::Plain => HashSet::new(),
    }
}

/// Builds a colored `Galley` for the given source text. Meant to be used as
/// an `egui::TextEdit` `layouter`.
pub fn highlight(ui: &egui::Ui, text: &str, language: Language, wrap_width: f32) -> Arc<egui::Galley> {
    let font_id = FontId::new(15.0, FontFamily::Monospace);
    let mut job = egui::text::LayoutJob::default();
    job.wrap.max_width = wrap_width;

    if language == Language::Plain {
        push(&mut job, text, Palette::DEFAULT, font_id);
        return ui.fonts(|f| f.layout_job(job));
    }

    let keywords = keyword_set(language);
    let has_block_comments = matches!(language, Language::C | Language::Cpp | Language::Cs | Language::Rust);
    let is_python = language == Language::Python;
    let is_c_family = matches!(language, Language::C | Language::Cpp | Language::Cs);
    let is_vb = language == Language::VisualBasic;
    let is_batch = language == Language::Batch;
    let is_html = matches!(language, Language::Html | Language::Aspx);
    let is_aspx = language == Language::Aspx;
    // VB and Batch are case-insensitive languages, so keyword lookups compare
    // upper-cased words against the (upper-case) keyword tables above.
    let case_insensitive_keywords = is_vb || is_batch;

    let chars: Vec<char> = text.chars().collect();
    let n = chars.len();
    let mut i = 0usize;

    while i < n {
        let c = chars[i];

        // ASP.NET server-side code blocks: <% ... %>
        if is_aspx && c == '<' && i + 1 < n && chars[i + 1] == '%' {
            let start = i;
            i += 2;
            while i < n && !(chars[i] == '%' && i + 1 < n && chars[i + 1] == '>') {
                i += 1;
            }
            i = (i + 2).min(n);
            push(&mut job, &collect(&chars, start, i), Palette::MACRO, font_id.clone());
            continue;
        }

        // HTML/ASPX comments: <!-- ... -->
        if is_html && c == '<' && i + 3 < n && chars[i + 1] == '!' && chars[i + 2] == '-' && chars[i + 3] == '-' {
            let start = i;
            i += 4;
            while i < n && !(chars[i] == '-' && i + 2 < n && chars[i + 1] == '-' && chars[i + 2] == '>') {
                i += 1;
            }
            i = (i + 3).min(n);
            push(&mut job, &collect(&chars, start, i), Palette::COMMENT, font_id.clone());
            continue;
        }

        // HTML/ASPX tags: <tag attr="value"> — colored as a single unit since
        // this is a tokenizer, not a real markup parser.
        if is_html && c == '<' {
            let start = i;
            i += 1;
            while i < n && chars[i] != '>' {
                i += 1;
            }
            i = (i + 1).min(n);
            push(&mut job, &collect(&chars, start, i), Palette::TYPE, font_id.clone());
            continue;
        }

        // VB / VBScript comments: ' to end of line (or the REM keyword, left
        // to the identifier/keyword branch below).
        if is_vb && c == '\'' {
            let start = i;
            while i < n && chars[i] != '\n' {
                i += 1;
            }
            push(&mut job, &collect(&chars, start, i), Palette::COMMENT, font_id.clone());
            continue;
        }

        // Batch comments: lines starting with ::
        if is_batch && c == ':' && i + 1 < n && chars[i + 1] == ':' {
            let start = i;
            while i < n && chars[i] != '\n' {
                i += 1;
            }
            push(&mut job, &collect(&chars, start, i), Palette::COMMENT, font_id.clone());
            continue;
        }

        // Batch variables: %VAR% or %1, %~dp0, etc.
        if is_batch && c == '%' {
            let start = i;
            i += 1;
            while i < n && chars[i] != '%' && chars[i] != '\n' && !chars[i].is_whitespace() {
                i += 1;
            }
            if i < n && chars[i] == '%' {
                i += 1;
            }
            push(&mut job, &collect(&chars, start, i), Palette::TYPE, font_id.clone());
            continue;
        }

        // Block comments: /* ... */
        if has_block_comments && c == '/' && i + 1 < n && chars[i + 1] == '*' {
            let start = i;
            i += 2;
            while i < n && !(chars[i] == '*' && i + 1 < n && chars[i + 1] == '/') {
                i += 1;
            }
            i = (i + 2).min(n);
            push(&mut job, &collect(&chars, start, i), Palette::COMMENT, font_id.clone());
            continue;
        }

        // Line comments: // for C-family/Rust, # for Python
        if !is_python && c == '/' && i + 1 < n && chars[i + 1] == '/' {
            let start = i;
            while i < n && chars[i] != '\n' {
                i += 1;
            }
            push(&mut job, &collect(&chars, start, i), Palette::COMMENT, font_id.clone());
            continue;
        }
        if is_python && c == '#' {
            let start = i;
            while i < n && chars[i] != '\n' {
                i += 1;
            }
            push(&mut job, &collect(&chars, start, i), Palette::COMMENT, font_id.clone());
            continue;
        }

        // Preprocessor directives (C/C++ only): color the whole line, e.g. #include <stdio.h>
        if is_c_family && c == '#' {
            let start = i;
            while i < n && chars[i] != '\n' {
                i += 1;
            }
            push(&mut job, &collect(&chars, start, i), Palette::MACRO, font_id.clone());
            continue;
        }

        // String / char literals. VB doesn't use ' for strings (it's a
        // comment there, handled above), so skip that quote char for VB.
        if c == '"' || (c == '\'' && !is_vb) {
            let quote = c;
            let start = i;
            i += 1;
            while i < n && chars[i] != quote {
                if chars[i] == '\\' && i + 1 < n {
                    i += 1;
                }
                i += 1;
            }
            i = (i + 1).min(n);
            push(&mut job, &collect(&chars, start, i), Palette::STRING, font_id.clone());
            continue;
        }

        // Numbers
        if c.is_ascii_digit() {
            let start = i;
            while i < n && (chars[i].is_ascii_alphanumeric() || chars[i] == '.' || chars[i] == '_') {
                i += 1;
            }
            push(&mut job, &collect(&chars, start, i), Palette::NUMBER, font_id.clone());
            continue;
        }

        // Identifiers / keywords / type-like names (Capitalized)
        if c.is_alphabetic() || c == '_' {
            let start = i;
            while i < n && (chars[i].is_alphanumeric() || chars[i] == '_') {
                i += 1;
            }
            let word = collect(&chars, start, i);
            let is_keyword = if case_insensitive_keywords {
                keywords.contains(word.to_uppercase().as_str())
            } else {
                keywords.contains(word.as_str())
            };
            let color = if is_keyword {
                Palette::KEYWORD
            } else if word.chars().next().map(|ch| ch.is_uppercase()).unwrap_or(false) {
                Palette::TYPE
            } else {
                Palette::DEFAULT
            };
            push(&mut job, &word, color, font_id.clone());
            continue;
        }

        // Everything else (whitespace, punctuation, operators): one char at a time.
        let start = i;
        i += 1;
        push(&mut job, &collect(&chars, start, i), Palette::DEFAULT, font_id.clone());
    }

    ui.fonts(|f| f.layout_job(job))
}

fn collect(chars: &[char], start: usize, end: usize) -> String {
    chars[start..end].iter().collect()
}

fn push(job: &mut egui::text::LayoutJob, text: &str, color: Color32, font_id: FontId) {
    job.append(
        text,
        0.0,
        egui::TextFormat {
            font_id,
            color,
            ..Default::default()
        },
    );
}
