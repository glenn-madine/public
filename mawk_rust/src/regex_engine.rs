// ----------------------------------------------------------------------
// Regex engine: literals, '.', '^', '$', '[...]', '[^...]', * + ?,
// grouping '(...)' and alternation 'a|b', with backtracking.
// A direct port of the continuation-frame backtracking matcher in mawk.c.
// ----------------------------------------------------------------------

use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

#[derive(Clone)]
pub enum AtomKind {
    Char(u8),
    Any,
    Class { bits: [u8; 32], neg: bool },
    Group(Rc<RAlt>),
}

#[derive(Clone)]
pub struct RAtom {
    pub kind: AtomKind,
    pub quant: u8, // 0 none, 1 star, 2 plus, 3 ques
}

#[derive(Clone)]
pub struct RConcat {
    pub atoms: Vec<RAtom>,
}

#[derive(Clone)]
pub struct RAlt {
    pub concats: Vec<RConcat>,
}

pub struct Regex {
    pub top: RAlt,
    pub anchor_start: bool,
    pub anchor_end: bool,
}

fn class_set(bits: &mut [u8; 32], c: u8) {
    bits[(c >> 3) as usize] |= 1u8 << (c & 7);
}
fn class_test(bits: &[u8; 32], c: u8) -> bool {
    (bits[(c >> 3) as usize] >> (c & 7)) & 1 != 0
}

// ---- Parser -------------------------------------------------------------

struct RParser<'a> {
    b: &'a [u8],
    pos: usize,
}

impl<'a> RParser<'a> {
    fn peek(&self) -> u8 {
        if self.pos < self.b.len() { self.b[self.pos] } else { 0 }
    }
    fn peek_at(&self, off: usize) -> u8 {
        let p = self.pos + off;
        if p < self.b.len() { self.b[p] } else { 0 }
    }

    fn parse_atom(&mut self) -> RAtom {
        let mut kind;
        if self.peek() == b'(' {
            self.pos += 1;
            let group = self.parse_alt();
            if self.peek() == b')' { self.pos += 1; }
            kind = AtomKind::Group(Rc::new(group));
        } else if self.peek() == b'.' {
            self.pos += 1;
            kind = AtomKind::Any;
        } else if self.peek() == b'[' {
            self.pos += 1;
            let mut neg = false;
            if self.peek() == b'^' { neg = true; self.pos += 1; }
            let mut bits = [0u8; 32];
            let mut first = true;
            while self.peek() != 0 && (self.peek() != b']' || first) {
                first = false;
                let lo: u8;
                if self.peek() == b'\\' && self.peek_at(1) != 0 {
                    lo = self.peek_at(1);
                    self.pos += 2;
                } else {
                    lo = self.peek();
                    self.pos += 1;
                }
                if self.peek() == b'-' && self.peek_at(1) != 0 && self.peek_at(1) != b']' {
                    let hi = self.peek_at(1);
                    self.pos += 2;
                    let mut c = lo as u32;
                    while c <= hi as u32 {
                        class_set(&mut bits, c as u8);
                        c += 1;
                    }
                } else {
                    class_set(&mut bits, lo);
                }
            }
            if self.peek() == b']' { self.pos += 1; }
            kind = AtomKind::Class { bits, neg };
        } else if self.peek() == b'\\' && self.peek_at(1) != 0 {
            let e = self.peek_at(1);
            let lit = match e {
                b'n' => b'\n',
                b't' => b'\t',
                b'r' => b'\r',
                _ => e,
            };
            self.pos += 2;
            kind = AtomKind::Char(lit);
        } else {
            kind = AtomKind::Char(self.peek());
            if self.pos < self.b.len() { self.pos += 1; }
        }
        let mut quant = 0u8;
        match self.peek() {
            b'*' => { quant = 1; self.pos += 1; }
            b'+' => { quant = 2; self.pos += 1; }
            b'?' => { quant = 3; self.pos += 1; }
            _ => {}
        }
        let _ = &mut kind;
        RAtom { kind, quant }
    }

    fn parse_concat(&mut self) -> RConcat {
        let mut atoms = Vec::new();
        while self.peek() != 0 && self.peek() != b'|' && self.peek() != b')' {
            atoms.push(self.parse_atom());
        }
        RConcat { atoms }
    }

    fn parse_alt(&mut self) -> RAlt {
        let mut concats = Vec::new();
        loop {
            concats.push(self.parse_concat());
            if self.peek() == b'|' {
                self.pos += 1;
                continue;
            }
            break;
        }
        RAlt { concats }
    }
}

thread_local! {
    static RECACHE: RefCell<HashMap<String, Rc<Regex>>> = RefCell::new(HashMap::new());
}

pub fn regex_compile(pat: &str) -> Rc<Regex> {
    if let Some(re) = RECACHE.with(|c| c.borrow().get(pat).cloned()) {
        return re;
    }
    let mut anchor_start = false;
    let mut anchor_end = false;
    let mut bytes = pat.as_bytes().to_vec();
    if bytes.first() == Some(&b'^') {
        anchor_start = true;
        bytes.remove(0);
    }
    if !bytes.is_empty() && *bytes.last().unwrap() == b'$' {
        let tl = bytes.len();
        let escaped = tl >= 2 && bytes[tl - 2] == b'\\';
        if !escaped {
            bytes.pop();
            anchor_end = true;
        }
    }
    let mut p = RParser { b: &bytes, pos: 0 };
    let top = p.parse_alt();
    let re = Rc::new(Regex { top, anchor_start, anchor_end });
    RECACHE.with(|c| c.borrow_mut().insert(pat.to_string(), re.clone()));
    re
}

// ---- Matcher (continuation-frame backtracking) --------------------------

enum Cont<'a> {
    Done,
    Seq { atoms: &'a [RAtom], idx: usize, parent: &'a Cont<'a> },
    Repeat { atom: &'a RAtom, repcount: i32, parent: &'a Cont<'a> },
}

fn atom_char_matches(a: &RAtom, c: u8) -> bool {
    match &a.kind {
        AtomKind::Any => true,
        AtomKind::Char(lit) => *lit == c,
        AtomKind::Class { bits, neg } => {
            let r = class_test(bits, c);
            if *neg { !r } else { r }
        }
        AtomKind::Group(_) => false,
    }
}

fn matchatoms(f: &Cont, text: &[u8], pos: usize, start: usize, anchor_end: bool) -> Option<usize> {
    match f {
        Cont::Done => {
            if anchor_end && pos != text.len() {
                return None;
            }
            Some(pos - start)
        }
        Cont::Repeat { atom, repcount, parent } => {
            try_repeat(atom, *repcount, parent, text, pos, start, anchor_end)
        }
        Cont::Seq { atoms, idx, parent } => {
            if *idx == atoms.len() {
                return matchatoms(parent, text, pos, start, anchor_end);
            }
            let a = &atoms[*idx];
            let nextf = Cont::Seq { atoms, idx: idx + 1, parent };
            match_one_atom(a, &nextf, text, pos, start, anchor_end)
        }
    }
}

fn try_repeat(
    a: &RAtom,
    repcount: i32,
    cont: &Cont,
    text: &[u8],
    pos: usize,
    start: usize,
    anchor_end: bool,
) -> Option<usize> {
    let maxrep: i32 = if a.quant == 3 { 1 } else { -1 };
    let minrep_total: i32 = if a.quant == 2 { 1 } else { 0 };
    if maxrep == -1 || repcount < maxrep {
        if let AtomKind::Group(g) = &a.kind {
            for concat in &g.concats {
                let repf = Cont::Repeat { atom: a, repcount: repcount + 1, parent: cont };
                let bf = Cont::Seq { atoms: &concat.atoms, idx: 0, parent: &repf };
                if let Some(r) = matchatoms(&bf, text, pos, start, anchor_end) {
                    return Some(r);
                }
            }
        } else if pos < text.len() && atom_char_matches(a, text[pos]) {
            let repf = Cont::Repeat { atom: a, repcount: repcount + 1, parent: cont };
            if let Some(r) = matchatoms(&repf, text, pos + 1, start, anchor_end) {
                return Some(r);
            }
        }
    }
    if repcount >= minrep_total {
        return matchatoms(cont, text, pos, start, anchor_end);
    }
    None
}

fn match_one_atom(
    a: &RAtom,
    cont: &Cont,
    text: &[u8],
    pos: usize,
    start: usize,
    anchor_end: bool,
) -> Option<usize> {
    if a.quant == 0 {
        if let AtomKind::Group(g) = &a.kind {
            for concat in &g.concats {
                let bf = Cont::Seq { atoms: &concat.atoms, idx: 0, parent: cont };
                if let Some(r) = matchatoms(&bf, text, pos, start, anchor_end) {
                    return Some(r);
                }
            }
            return None;
        }
        if pos < text.len() && atom_char_matches(a, text[pos]) {
            return matchatoms(cont, text, pos + 1, start, anchor_end);
        }
        return None;
    }
    try_repeat(a, 0, cont, text, pos, start, anchor_end)
}

fn regex_match_at(re: &Regex, text: &[u8], pos: usize) -> Option<usize> {
    for concat in &re.top.concats {
        let bf = Cont::Seq { atoms: &concat.atoms, idx: 0, parent: &Cont::Done };
        if let Some(len) = matchatoms(&bf, text, pos, pos, re.anchor_end) {
            return Some(len);
        }
    }
    None
}

/// Returns Some((mstart, mlen)) byte offsets on success.
pub fn regex_search(pat: &str, text: &str) -> Option<(usize, usize)> {
    let re = regex_compile(pat);
    let bytes = text.as_bytes();
    let mut pos = 0usize;
    loop {
        if let Some(len) = regex_match_at(&re, bytes, pos) {
            return Some((pos, len));
        }
        if re.anchor_start {
            break;
        }
        if pos >= bytes.len() {
            break;
        }
        pos += 1;
    }
    None
}

pub fn regex_match_bool(pat: &str, text: &str) -> bool {
    regex_search(pat, text).is_some()
}
