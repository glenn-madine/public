// ----------------------------------------------------------------------
// Lexer
// ----------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TokType {
    Eof, Num, Str, Ere, Ident, FuncName,
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
}

impl Token {
    fn simple(ty: TokType) -> Token {
        Token { ty, num: 0.0, text: String::new() }
    }
}

#[derive(Clone)]
pub struct Lexer {
    src: Vec<u8>,
    pos: usize,
}

impl Lexer {
    pub fn new(src: &str) -> Lexer {
        Lexer { src: src.as_bytes().to_vec(), pos: 0 }
    }

    fn cur(&self) -> u8 {
        if self.pos < self.src.len() { self.src[self.pos] } else { 0 }
    }
    fn at(&self, off: usize) -> u8 {
        let p = self.pos + off;
        if p < self.src.len() { self.src[p] } else { 0 }
    }

    fn skip_ws_comments(&mut self) {
        loop {
            while self.cur() == b' '
                || self.cur() == b'\t'
                || self.cur() == b'\r'
                || (self.cur() == b'\\' && self.at(1) == b'\n')
            {
                if self.cur() == b'\\' {
                    self.pos += 2;
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

    /// prev_significant tells us whether '/' should be interpreted as division
    /// (true) or as the start of an ERE literal (false).
    pub fn next(&mut self, prev_significant: bool) -> Token {
        self.skip_ws_comments();
        let c = self.cur();
        if c == 0 {
            return Token::simple(TokType::Eof);
        }
        if c == b'\n' {
            self.pos += 1;
            return Token::simple(TokType::Newline);
        }
        if c == b'"' {
            self.pos += 1;
            let mut s = String::new();
            while self.cur() != 0 && self.cur() != b'"' {
                let mut ch = self.cur();
                self.pos += 1;
                if ch == b'\\' && self.cur() != 0 {
                    let e = self.cur();
                    self.pos += 1;
                    ch = match e {
                        b'n' => b'\n',
                        b't' => b'\t',
                        b'r' => b'\r',
                        b'\\' => b'\\',
                        b'"' => b'"',
                        b'/' => b'/',
                        _ => e,
                    };
                }
                s.push(ch as char);
            }
            if self.cur() == b'"' {
                self.pos += 1;
            }
            return Token { ty: TokType::Str, num: 0.0, text: s };
        }
        if c.is_ascii_digit() || (c == b'.' && self.at(1).is_ascii_digit()) {
            let start = self.pos;
            let rest = std::str::from_utf8(&self.src[start..]).unwrap_or("");
            if let Some((val, remainder)) = crate::value::parse_leading_double(rest) {
                let consumed = rest.len() - remainder.len();
                self.pos = start + consumed;
                return Token { ty: TokType::Num, num: val, text: String::new() };
            }
            // fallback: shouldn't happen
            self.pos += 1;
            return Token { ty: TokType::Num, num: 0.0, text: String::new() };
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
                return Token::simple(tt);
            }
            if self.cur() == b'(' {
                return Token { ty: TokType::FuncName, num: 0.0, text: word };
            }
            return Token { ty: TokType::Ident, num: 0.0, text: word };
        }
        if c == b'/' && !prev_significant {
            self.pos += 1;
            let mut s = String::new();
            while self.cur() != 0 && self.cur() != b'/' {
                let mut ch = self.cur();
                self.pos += 1;
                if ch == b'\\' && self.cur() != 0 {
                    s.push(ch as char);
                    ch = self.cur();
                    self.pos += 1;
                }
                s.push(ch as char);
            }
            if self.cur() == b'/' {
                self.pos += 1;
            }
            return Token { ty: TokType::Ere, num: 0.0, text: s };
        }

        macro_rules! op2 {
            ($a:expr, $b:expr, $tt:expr) => {
                if c == $a && self.at(1) == $b {
                    self.pos += 2;
                    return Token::simple($tt);
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
                eprintln!("mawk: unexpected character '{}'", c as char);
                std::process::exit(2);
            }
        };
        Token::simple(ty)
    }
}

pub fn token_is_value_end(t: TokType) -> bool {
    matches!(
        t,
        TokType::Num | TokType::Str | TokType::Ident | TokType::RParen | TokType::RBracket
            | TokType::Dollar | TokType::Incr | TokType::Decr
    )
}
