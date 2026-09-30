// ----------------------------------------------------------------------
// Lexer
// ----------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TokType {
    Eof, Num, Str, Ere, Ident, FuncName, Builtin,
    Begin, End, If, Else, While, Do, For, Print, Printf,
    Next, NextFile, Exit, Break, Continue, Delete, In,
    Function, Getline, Return,
    LBrace, RBrace, LParen, RParen, LBracket, RBracket,
    Semi, Newline, Comma, Dollar,
    Assign, AddAssign, SubAssign, MulAssign, DivAssign, ModAssign, PowAssign,
    Or, And, Not, Lt, Le, Gt, Ge, Eq, Ne, Match, NoMatch,
    Plus, Minus, Star, Slash, Percent, Caret, Incr, Decr,
    Question, Colon, Pipe, Append,
}

#[derive(Clone, Debug)]
pub struct Token {
    pub ty: TokType,
    pub num: f64,
    pub text: String,
    pub line: usize,
}

/// Builtin function names with their (min, max) argument counts.
/// `usize::MAX` means variadic.
pub const BUILTINS: &[(&str, usize, usize)] = &[
    ("length", 0, 1),
    ("substr", 2, 3),
    ("index", 2, 2),
    ("split", 2, 3),
    ("sub", 2, 3),
    ("gsub", 2, 3),
    ("match", 2, 2),
    ("sprintf", 1, usize::MAX),
    ("sin", 1, 1),
    ("cos", 1, 1),
    ("atan2", 2, 2),
    ("exp", 1, 1),
    ("log", 1, 1),
    ("sqrt", 1, 1),
    ("int", 1, 1),
    ("rand", 0, 0),
    ("srand", 0, 1),
    ("tolower", 1, 1),
    ("toupper", 1, 1),
    ("system", 1, 1),
    ("close", 1, 1),
    ("fflush", 0, 1),
];

pub fn builtin_arity(name: &str) -> Option<(usize, usize)> {
    BUILTINS.iter().find(|(n, _, _)| *n == name).map(|&(_, lo, hi)| (lo, hi))
}

#[derive(Clone)]
pub struct Lexer {
    src: Vec<u8>,
    pos: usize,
    pub line: usize,
}

impl Lexer {
    pub fn new(src: &str) -> Lexer {
        Lexer { src: src.as_bytes().to_vec(), pos: 0, line: 1 }
    }

    fn cur(&self) -> u8 {
        if self.pos < self.src.len() { self.src[self.pos] } else { 0 }
    }
    fn at(&self, off: usize) -> u8 {
        let p = self.pos + off;
        if p < self.src.len() { self.src[p] } else { 0 }
    }

    fn tok(&self, ty: TokType) -> Token {
        Token { ty, num: 0.0, text: String::new(), line: self.line }
    }

    fn skip_ws_comments(&mut self) {
        loop {
            while self.cur() == b' '
                || self.cur() == b'\t'
                || self.cur() == b'\r'
                || (self.cur() == b'\\' && self.at(1) == b'\n')
                || (self.cur() == b'\\' && self.at(1) == b'\r' && self.at(2) == b'\n')
            {
                if self.cur() == b'\\' {
                    self.pos += if self.at(1) == b'\r' { 3 } else { 2 };
                    self.line += 1;
                } else {
                    self.pos += 1;
                }
            }
            if self.cur() == b'#' {
                while self.cur() != 0 && self.cur() != b'\n' {
                    self.pos += 1;
                }
                continue;
            }
            break;
        }
    }

    /// Reads one escape sequence (the backslash has already been consumed)
    /// and appends the resulting byte(s). Unknown escapes keep the backslash,
    /// as mawk does, so "\." still reaches a dynamic regex as a literal dot.
    fn read_escape(&mut self, out: &mut Vec<u8>) {
        let e = self.cur();
        match e {
            b'n' => { out.push(b'\n'); self.pos += 1; }
            b't' => { out.push(b'\t'); self.pos += 1; }
            b'r' => { out.push(b'\r'); self.pos += 1; }
            b'a' => { out.push(7); self.pos += 1; }
            b'b' => { out.push(8); self.pos += 1; }
            b'f' => { out.push(12); self.pos += 1; }
            b'v' => { out.push(11); self.pos += 1; }
            b'\\' => { out.push(b'\\'); self.pos += 1; }
            b'"' => { out.push(b'"'); self.pos += 1; }
            b'0'..=b'7' => {
                let mut v: u32 = 0;
                let mut n = 0;
                while n < 3 && (b'0'..=b'7').contains(&self.cur()) {
                    v = v * 8 + (self.cur() - b'0') as u32;
                    self.pos += 1;
                    n += 1;
                }
                out.push((v & 0xFF) as u8);
            }
            b'x' if self.at(1).is_ascii_hexdigit() => {
                self.pos += 1;
                let mut v: u32 = 0;
                let mut n = 0;
                while n < 2 && self.cur().is_ascii_hexdigit() {
                    v = v * 16 + (self.cur() as char).to_digit(16).unwrap();
                    self.pos += 1;
                    n += 1;
                }
                out.push(v as u8);
            }
            b'\n' => {
                // backslash-newline inside a string: line continuation
                self.pos += 1;
                self.line += 1;
            }
            0 => out.push(b'\\'),
            _ => {
                out.push(b'\\');
                out.push(e);
                self.pos += 1;
            }
        }
    }

    /// prev_significant tells us whether '/' should be interpreted as division
    /// (true) or as the start of an ERE literal (false).
    pub fn next(&mut self, prev_significant: bool) -> Token {
        self.skip_ws_comments();
        let c = self.cur();
        if c == 0 {
            return self.tok(TokType::Eof);
        }
        if c == b'\n' {
            let t = self.tok(TokType::Newline);
            self.pos += 1;
            self.line += 1;
            return t;
        }
        if c == b'"' {
            let line = self.line;
            self.pos += 1;
            let mut s: Vec<u8> = Vec::new();
            while self.cur() != 0 && self.cur() != b'"' {
                let ch = self.cur();
                self.pos += 1;
                if ch == b'\\' {
                    self.read_escape(&mut s);
                } else {
                    if ch == b'\n' {
                        eprintln!("mawk: line {}: runaway string constant", line);
                        std::process::exit(2);
                    }
                    s.push(ch);
                }
            }
            if self.cur() == b'"' {
                self.pos += 1;
            } else {
                eprintln!("mawk: line {}: runaway string constant", line);
                std::process::exit(2);
            }
            return Token { ty: TokType::Str, num: 0.0, text: String::from_utf8_lossy(&s).into_owned(), line };
        }
        if c.is_ascii_digit() || (c == b'.' && self.at(1).is_ascii_digit()) {
            let start = self.pos;
            // Only scan the ASCII number prefix; never hand parse_leading_double
            // something it could read as "inf"/"nan".
            let mut end = start;
            while end < self.src.len()
                && (self.src[end].is_ascii_alphanumeric() || matches!(self.src[end], b'.' | b'+' | b'-'))
            {
                end += 1;
            }
            let rest = std::str::from_utf8(&self.src[start..end]).unwrap_or("");
            if let Some((val, remainder)) = crate::value::parse_leading_double(rest) {
                let consumed = rest.len() - remainder.len();
                self.pos = start + consumed;
                return Token { ty: TokType::Num, num: val, text: String::new(), line: self.line };
            }
            self.pos += 1;
            return Token { ty: TokType::Num, num: 0.0, text: String::new(), line: self.line };
        }
        if c.is_ascii_alphabetic() || c == b'_' {
            let start = self.pos;
            while self.cur().is_ascii_alphanumeric() || self.cur() == b'_' {
                self.pos += 1;
            }
            let word = String::from_utf8_lossy(&self.src[start..self.pos]).to_string();
            let kw = match word.as_str() {
                "BEGIN" => Some(TokType::Begin),
                "END" => Some(TokType::End),
                "if" => Some(TokType::If),
                "else" => Some(TokType::Else),
                "while" => Some(TokType::While),
                "do" => Some(TokType::Do),
                "for" => Some(TokType::For),
                "print" => Some(TokType::Print),
                "printf" => Some(TokType::Printf),
                "next" => Some(TokType::Next),
                "nextfile" => Some(TokType::NextFile),
                "exit" => Some(TokType::Exit),
                "break" => Some(TokType::Break),
                "continue" => Some(TokType::Continue),
                "delete" => Some(TokType::Delete),
                "in" => Some(TokType::In),
                "function" | "func" => Some(TokType::Function),
                "getline" => Some(TokType::Getline),
                "return" => Some(TokType::Return),
                _ => None,
            };
            if let Some(tt) = kw {
                return self.tok(tt);
            }
            // Builtins are reserved words: `length` works without parens and
            // `substr ($0, 1)` may have a space before the paren.
            if builtin_arity(&word).is_some() {
                return Token { ty: TokType::Builtin, num: 0.0, text: word, line: self.line };
            }
            // User function calls must have '(' immediately after the name.
            if self.cur() == b'(' {
                return Token { ty: TokType::FuncName, num: 0.0, text: word, line: self.line };
            }
            return Token { ty: TokType::Ident, num: 0.0, text: word, line: self.line };
        }
        if c == b'/' && !prev_significant {
            let line = self.line;
            self.pos += 1;
            let mut s: Vec<u8> = Vec::new();
            let mut in_class = false;
            while self.cur() != 0 && (self.cur() != b'/' || in_class) {
                let ch = self.cur();
                if ch == b'\n' {
                    break;
                }
                self.pos += 1;
                if ch == b'\\' && self.cur() != 0 {
                    // "\/" is just an escaped slash; keep other escapes for the regex engine
                    if self.cur() == b'/' {
                        s.push(b'/');
                    } else {
                        s.push(b'\\');
                        s.push(self.cur());
                    }
                    self.pos += 1;
                    continue;
                }
                if ch == b'[' && !in_class {
                    in_class = true;
                    s.push(ch);
                    // a ']' right after '[' or '[^' is a literal member
                    if self.cur() == b'^' {
                        s.push(b'^');
                        self.pos += 1;
                    }
                    if self.cur() == b']' {
                        s.push(b']');
                        self.pos += 1;
                    }
                    continue;
                }
                if ch == b']' && in_class {
                    in_class = false;
                }
                s.push(ch);
            }
            if self.cur() == b'/' {
                self.pos += 1;
            } else {
                eprintln!("mawk: line {}: runaway regular expression /{} ...", line, String::from_utf8_lossy(&s));
                std::process::exit(2);
            }
            return Token { ty: TokType::Ere, num: 0.0, text: String::from_utf8_lossy(&s).into_owned(), line };
        }

        macro_rules! op2 {
            ($a:expr, $b:expr, $tt:expr) => {
                if c == $a && self.at(1) == $b {
                    self.pos += 2;
                    return self.tok($tt);
                }
            };
        }
        op2!(b'+', b'+', TokType::Incr);
        op2!(b'-', b'-', TokType::Decr);
        op2!(b'+', b'=', TokType::AddAssign);
        op2!(b'-', b'=', TokType::SubAssign);
        op2!(b'*', b'=', TokType::MulAssign);
        op2!(b'/', b'=', TokType::DivAssign);
        op2!(b'%', b'=', TokType::ModAssign);
        op2!(b'^', b'=', TokType::PowAssign);
        op2!(b'=', b'=', TokType::Eq);
        op2!(b'!', b'=', TokType::Ne);
        op2!(b'<', b'=', TokType::Le);
        op2!(b'>', b'=', TokType::Ge);
        op2!(b'&', b'&', TokType::And);
        op2!(b'|', b'|', TokType::Or);
        op2!(b'!', b'~', TokType::NoMatch);
        op2!(b'>', b'>', TokType::Append);

        self.pos += 1;
        let ty = match c {
            b'{' => TokType::LBrace,
            b'}' => TokType::RBrace,
            b'(' => TokType::LParen,
            b')' => TokType::RParen,
            b'[' => TokType::LBracket,
            b']' => TokType::RBracket,
            b';' => TokType::Semi,
            b',' => TokType::Comma,
            b'$' => TokType::Dollar,
            b'=' => TokType::Assign,
            b'<' => TokType::Lt,
            b'>' => TokType::Gt,
            b'!' => TokType::Not,
            b'~' => TokType::Match,
            b'+' => TokType::Plus,
            b'-' => TokType::Minus,
            b'*' => TokType::Star,
            b'/' => TokType::Slash,
            b'%' => TokType::Percent,
            b'^' => TokType::Caret,
            b'?' => TokType::Question,
            b':' => TokType::Colon,
            b'|' => TokType::Pipe,
            _ => {
                eprintln!("mawk: line {}: unexpected character '{}'", self.line, c as char);
                std::process::exit(2);
            }
        };
        self.tok(ty)
    }
}

pub fn token_is_value_end(t: TokType) -> bool {
    matches!(
        t,
        TokType::Num | TokType::Str | TokType::Ident | TokType::Builtin | TokType::RParen | TokType::RBracket
            | TokType::Dollar | TokType::Incr | TokType::Decr
    )
}
