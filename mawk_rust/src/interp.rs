// ----------------------------------------------------------------------
// Evaluator / driver: variables & arrays, fields, I/O streams, record
// reading, printf, builtins, and the main() driver logic.
// ----------------------------------------------------------------------
use crate::ast::{FuncDef, Node, PType, Program, RedirMode, Rule, Stmt};
use crate::lexer::TokType;
use crate::parser::Parser;
use crate::regex_engine;
use crate::value::{self, Value};
use std::cell::RefCell;
use std::cmp::Ordering;
use std::collections::HashMap;
use std::io::{BufRead, IsTerminal, Write};
use std::process::{Child, Command, Stdio};
use std::rc::Rc;

pub struct VarSlot {
    pub val: Value,
    pub is_arr: bool,
    pub arr: HashMap<String, Value>,
}
pub type VarRef = Rc<RefCell<VarSlot>>;

/// Largest field index / NF value a program may create.
pub const MAX_FIELDS: usize = 1_000_000;
/// Largest printf/sprintf field width or precision.
pub const MAX_PRINTF_WIDTH: i64 = 1_000_000;
/// Stack kept in reserve below the interpreter thread's stack size; a user
/// function call that would eat into it fails with "nesting too deep"
/// instead of overflowing the stack.
const STACK_RESERVE: usize = 64 * 1024 * 1024;

/// An OS error message without Rust's " (os error N)" suffix, as C awks print it.
pub fn os_err_msg(e: &std::io::Error) -> String {
    let s = e.to_string();
    match s.find(" (os error") {
        Some(i) => s[..i].to_string(),
        None => s,
    }
}

/// A resolved assignment target. Resolving evaluates subscripts and field
/// indexes exactly once, so `a[i++] += 1` and `$(n++)++` have no double
/// side effects.
enum LRef {
    Var(VarRef),
    NF,
    Elem(VarRef, String),
    Field(i64),
}

/// Builds a shell command: `sh -c` on Unix, `cmd /C` on Windows.
fn shell_command(cmd: &str) -> Command {
    if cfg!(windows) {
        let mut c = Command::new("cmd");
        c.arg("/C").arg(cmd);
        c
    } else {
        let mut c = Command::new("sh");
        c.arg("-c").arg(cmd);
        c
    }
}

/// Processes backslash escapes in -v / command-line assignments and -F.
pub fn process_escapes(s: &str) -> String {
    let b = s.as_bytes();
    let mut out: Vec<u8> = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        if b[i] != b'\\' || i + 1 >= b.len() {
            out.push(b[i]);
            i += 1;
            continue;
        }
        i += 1;
        match b[i] {
            b'n' => { out.push(b'\n'); i += 1; }
            b't' => { out.push(b'\t'); i += 1; }
            b'r' => { out.push(b'\r'); i += 1; }
            b'a' => { out.push(7); i += 1; }
            b'b' => { out.push(8); i += 1; }
            b'f' => { out.push(12); i += 1; }
            b'v' => { out.push(11); i += 1; }
            b'\\' => { out.push(b'\\'); i += 1; }
            b'"' => { out.push(b'"'); i += 1; }
            b'/' => { out.push(b'/'); i += 1; }
            b'0'..=b'7' => {
                let mut v: u32 = 0;
                let mut n = 0;
                while n < 3 && i < b.len() && (b'0'..=b'7').contains(&b[i]) {
                    v = v * 8 + (b[i] - b'0') as u32;
                    i += 1;
                    n += 1;
                }
                out.push((v & 0xFF) as u8);
            }
            c => {
                out.push(b'\\');
                out.push(c);
                i += 1;
            }
        }
    }
    String::from_utf8_lossy(&out).into_owned()
}

fn new_slot(v: Value) -> VarRef {
    Rc::new(RefCell::new(VarSlot { val: v, is_arr: false, arr: HashMap::new() }))
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Flow {
    None,
    Break,
    Continue,
    Next,
    NextFile,
    Exit,
    Return,
}

struct InputEnt {
    reader: Box<dyn BufRead>,
    child: Option<Child>,
    leftover: String,
}
struct OutputEnt {
    writer: Box<dyn Write>,
    child: Option<Child>,
}

struct MainInput {
    fp: Option<Box<dyn BufRead>>,
    used_stdin_fallback: bool,
    argv_pos: i64,
    all_done: bool,
    any_real_file_seen: bool,
    leftover: String,
}
impl MainInput {
    fn new() -> Self {
        MainInput {
            fp: None,
            used_stdin_fallback: false,
            argv_pos: 1,
            all_done: false,
            any_real_file_seen: false,
            leftover: String::new(),
        }
    }
}

struct Rng {
    state: u64,
}
impl Rng {
    fn new(seed: u64) -> Rng {
        let s = seed.wrapping_mul(2685821657736338717).wrapping_add(0x9E3779B97F4A7C15);
        Rng { state: if s == 0 { 0xDEADBEEF } else { s } }
    }
    fn next_u32(&mut self) -> u32 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        (x >> 20) as u32
    }
    fn next_f64(&mut self) -> f64 {
        (self.next_u32() as f64) / ((u32::MAX as f64) + 1.0)
    }
}

pub struct Interp {
    globals: HashMap<String, VarRef>,
    frames: Vec<HashMap<String, VarRef>>,
    fields: Vec<String>,
    nf: usize,
    record: String,
    inputs: HashMap<String, InputEnt>,
    outputs: HashMap<String, OutputEnt>,
    funcs: HashMap<String, Rc<FuncDef>>,
    rules: Rc<Vec<Rule>>,
    flow: Flow,
    exit_code: i32,
    retval: Value,
    rng: Rng,
    seed: f64,
    main_input: MainInput,
    stdout: Box<dyn Write>,
    /// Address near the top of the interpreter thread's stack (see run()).
    stack_base: usize,
}

impl Interp {
    pub fn new() -> Self {
        let mut it = Interp {
            globals: HashMap::new(),
            frames: Vec::new(),
            fields: vec![String::new()],
            nf: 0,
            record: String::new(),
            inputs: HashMap::new(),
            outputs: HashMap::new(),
            funcs: HashMap::new(),
            rules: Rc::new(Vec::new()),
            flow: Flow::None,
            exit_code: 0,
            retval: value::mkuninit(),
            rng: Rng::new(0),
            seed: 0.0,
            stack_base: 0,
            main_input: MainInput::new(),
            // Block-buffer stdout unless it's a terminal (then stay line-buffered).
            stdout: if std::io::stdout().is_terminal() {
                Box::new(std::io::stdout())
            } else {
                Box::new(std::io::BufWriter::with_capacity(64 * 1024, std::io::stdout()))
            },
        };
        it.setvar_str("FS", " ");
        it.setvar_str("OFS", " ");
        it.setvar_str("ORS", "\n");
        it.setvar_str("RS", "\n");
        it.setvar_str("SUBSEP", "\x1c");
        it.setvar_str("CONVFMT", "%.6g");
        it.setvar_str("OFMT", "%.6g");
        it.setvar_num("NR", 0.0);
        it.setvar_num("FNR", 0.0);
        it.setvar_num("RSTART", 0.0);
        it.setvar_num("RLENGTH", -1.0);
        it.setvar_str("FILENAME", "");
        it
    }

    pub fn load_program(&mut self, p: Program) {
        self.rules = Rc::new(p.rules);
        self.funcs = p.funcs.into_iter().map(|(k, v)| (k, Rc::new(v))).collect();
    }

    fn die(&mut self, msg: &str) -> ! {
        self.flush_all();
        eprintln!("mawk: {}", msg);
        self.io_close_all();
        std::process::exit(2);
    }

    /// Handles a failed write. A closed pipe (e.g. `awk ... | head`) ends the
    /// program quietly, like the SIGPIPE a C awk would receive.
    fn write_failed(&mut self, e: std::io::Error, what: &str) -> ! {
        if e.kind() == std::io::ErrorKind::BrokenPipe {
            std::process::exit(141);
        }
        eprintln!("mawk: write failure on {}: {}", what, e);
        std::process::exit(2);
    }

    // ---- variables ----

    fn frame_lookup(&self, name: &str) -> Option<VarRef> {
        self.frames.last().and_then(|f| f.get(name).cloned())
    }
    fn var_find(&self, name: &str) -> Option<VarRef> {
        if let Some(v) = self.frame_lookup(name) {
            return Some(v);
        }
        self.globals.get(name).cloned()
    }
    fn var_get(&mut self, name: &str) -> VarRef {
        if let Some(v) = self.frame_lookup(name) {
            return v;
        }
        if let Some(v) = self.globals.get(name) {
            return v.clone();
        }
        let slot = new_slot(value::mkuninit());
        self.globals.insert(name.to_string(), slot.clone());
        slot
    }

    fn setvar_num(&mut self, name: &str, d: f64) {
        let v = self.var_get(name);
        v.borrow_mut().val = value::mknum(d);
    }
    fn setvar_str(&mut self, name: &str, s: &str) {
        let v = self.var_get(name);
        v.borrow_mut().val = value::mkstr(s);
    }
    fn setvar_strnum(&mut self, name: &str, s: &str) {
        let v = self.var_get(name);
        v.borrow_mut().val = value::mkstrnum(s);
    }
    fn getvar_num(&self, name: &str) -> f64 {
        match self.var_find(name) {
            Some(v) => value::to_num(&v.borrow().val),
            None => 0.0,
        }
    }
    fn var_str_raw(&self, name: &str) -> String {
        match self.var_find(name) {
            Some(v) => {
                let b = v.borrow();
                match &b.val.str {
                    Some(s) => s.clone(),
                    None => value::fmt_num(b.val.num, "%.6g"),
                }
            }
            None => String::new(),
        }
    }
    fn convfmt(&self) -> String {
        let s = self.var_str_raw("CONVFMT");
        if s.is_empty() { "%.6g".to_string() } else { s }
    }
    fn ofmt(&self) -> String {
        let s = self.var_str_raw("OFMT");
        if s.is_empty() { "%.6g".to_string() } else { s }
    }
    fn getvar_str(&self, name: &str) -> String {
        match self.var_find(name) {
            Some(v) => {
                let b = v.borrow();
                value::to_str(&b.val, &self.convfmt())
            }
            None => String::new(),
        }
    }

    fn build_key(&mut self, idxs: &[Node]) -> String {
        let subsep = {
            let s = self.getvar_str("SUBSEP");
            if s.is_empty() { "\x1c".to_string() } else { s }
        };
        let mut parts = Vec::with_capacity(idxs.len());
        for idx in idxs {
            let v = self.eval(idx);
            let fmt = self.convfmt();
            parts.push(value::to_str(&v, &fmt));
        }
        parts.join(&subsep)
    }

    fn in_test(&mut self, idxs: &[Node], arrname: &str) -> bool {
        let key = self.build_key(idxs);
        match self.var_find(arrname) {
            None => false,
            Some(v) => v.borrow().arr.contains_key(&key),
        }
    }

    // ---- fields / record ----

    fn get_field(&self, i: i64) -> String {
        if i == 0 {
            return self.record.clone();
        }
        if i < 0 || (i as usize) > self.nf {
            return String::new();
        }
        self.fields.get(i as usize).cloned().unwrap_or_default()
    }

    fn set_field(&mut self, i: i64, s: &str) {
        if i == 0 {
            self.set_record(s);
            return;
        }
        if i < 0 {
            return;
        }
        let iu = i as usize;
        if iu > MAX_FIELDS {
            self.die(&format!("field index ${} too large (limit {})", i, MAX_FIELDS));
        }
        if self.fields.len() <= iu {
            self.fields.resize(iu + 1, String::new());
        }
        if iu > self.nf {
            self.nf = iu;
            self.setvar_num("NF", self.nf as f64);
        }
        self.fields[iu] = s.to_string();
        self.rebuild_record();
    }

    fn rebuild_record(&mut self) {
        let ofs = self.getvar_str("OFS");
        let mut parts = Vec::with_capacity(self.nf);
        for idx in 1..=self.nf {
            parts.push(self.fields.get(idx).cloned().unwrap_or_default());
        }
        self.record = parts.join(&ofs);
    }

    fn set_record(&mut self, s: &str) {
        self.record = s.to_string();
        self.split_record();
    }

    fn split_record(&mut self) {
        let fs = self.getvar_str("FS");
        let pieces = awk_split(&self.record, &fs);
        self.fields = Vec::with_capacity(pieces.len() + 1);
        self.fields.push(String::new());
        self.nf = pieces.len();
        self.fields.extend(pieces);
        self.setvar_num("NF", self.nf as f64);
    }
}

impl Interp {
    // ---- expression evaluation ----

    fn eval(&mut self, n: &Node) -> Value {
        match n {
            Node::Num(d) => value::mknum(*d),
            Node::Str(s) => value::mkstr(s),
            Node::Ere(pat) => {
                let rec = self.record.clone();
                value::mknum(if regex_engine::regex_match_bool(pat, &rec) { 1.0 } else { 0.0 })
            }
            Node::Group(inner) => self.eval(inner),
            Node::Getline(target, src, mode) => self.eval_getline(target.as_deref(), src.as_deref(), *mode),
            Node::In(idxs, arrname) => value::mknum(if self.in_test(idxs, arrname) { 1.0 } else { 0.0 }),
            Node::Var(name) => {
                if name == "NF" {
                    return value::mknum(self.nf as f64);
                }
                match self.var_find(name) {
                    None => value::mkuninit(),
                    Some(vref) => {
                        let b = vref.borrow();
                        if b.is_arr {
                            drop(b);
                            self.die(&format!("illegal reference to array {}", name));
                        }
                        b.val.clone()
                    }
                }
            }
            Node::ArrRef(..) => match self.resolve(n) {
                Some(r) => self.lref_get(&r),
                None => unreachable!(),
            },
            Node::Field(idxn) => {
                let iv = self.eval(idxn);
                let idx = value::to_num(&iv) as i64;
                if idx < 0 {
                    self.die(&format!("negative field index ${}", idx));
                }
                value::mkstrnum(&self.get_field(idx))
            }
            Node::Ternary(c, a, b) => {
                let cv = self.eval(c);
                if value::truthy(&cv) { self.eval(a) } else { self.eval(b) }
            }
            Node::Assign(op, target, rhs) => self.eval_assign(*op, target, rhs),
            Node::Pow(a, b) => {
                let av = self.eval(a);
                let bv = self.eval(b);
                value::mknum(value::to_num(&av).powf(value::to_num(&bv)))
            }
            Node::BinOp(op, a, b) => self.eval_binop(*op, a, b),
            Node::Match(op, a, b) => self.eval_match(*op, a, b),
            Node::Concat(a, b) => {
                let av = self.eval(a);
                let bv = self.eval(b);
                let fmt = self.convfmt();
                let sa = value::to_str(&av, &fmt);
                let sb = value::to_str(&bv, &fmt);
                value::mkstring(sa + &sb)
            }
            Node::And(a, b) => {
                let av = self.eval(a);
                if !value::truthy(&av) {
                    return value::mknum(0.0);
                }
                let bv = self.eval(b);
                value::mknum(if value::truthy(&bv) { 1.0 } else { 0.0 })
            }
            Node::Or(a, b) => {
                let av = self.eval(a);
                if value::truthy(&av) {
                    return value::mknum(1.0);
                }
                let bv = self.eval(b);
                value::mknum(if value::truthy(&bv) { 1.0 } else { 0.0 })
            }
            Node::Not(a) => {
                let av = self.eval(a);
                value::mknum(if value::truthy(&av) { 0.0 } else { 1.0 })
            }
            Node::Unary(op, a) => {
                let av = self.eval(a);
                let d = value::to_num(&av);
                value::mknum(if *op == TokType::Minus { -d } else { d })
            }
            Node::PreIncDec(op, target) | Node::PostIncDec(op, target) => {
                let r = match self.resolve(target) {
                    Some(r) => r,
                    None => self.die("invalid ++/-- target"),
                };
                let cur = self.lref_get(&r);
                let d = value::to_num(&cur);
                let nd = d + if *op == TokType::Incr { 1.0 } else { -1.0 };
                self.lref_set(r, value::mknum(nd));
                value::mknum(if matches!(n, Node::PreIncDec(..)) { nd } else { d })
            }
            Node::Call(name, args) => self.eval_call(name, args),
        }
    }

    /// Resolves an assignment target, evaluating subscripts/field indexes once.
    /// Returns None if `target` is not an lvalue.
    fn resolve(&mut self, target: &Node) -> Option<LRef> {
        match target {
            Node::Var(name) => {
                if name == "NF" && self.frame_lookup(name).is_none() {
                    return Some(LRef::NF);
                }
                let vref = self.var_get(name);
                if vref.borrow().is_arr {
                    self.die(&format!("can't assign to {}; it's an array name.", name));
                }
                Some(LRef::Var(vref))
            }
            Node::ArrRef(name, idxs) => {
                let key = self.build_key(idxs);
                let vref = self.var_get(name);
                {
                    let mut b = vref.borrow_mut();
                    if !b.is_arr && !b.val.uninit {
                        drop(b);
                        self.die(&format!("can't use scalar {} as array", name));
                    }
                    b.is_arr = true;
                }
                Some(LRef::Elem(vref, key))
            }
            Node::Field(idxn) => {
                let iv = self.eval(idxn);
                let idx = value::to_num(&iv) as i64;
                if idx < 0 {
                    self.die(&format!("negative field index ${}", idx));
                }
                Some(LRef::Field(idx))
            }
            Node::Group(inner) => self.resolve(inner),
            _ => None,
        }
    }

    fn lref_get(&mut self, r: &LRef) -> Value {
        match r {
            LRef::Var(v) => v.borrow().val.clone(),
            LRef::NF => value::mknum(self.nf as f64),
            LRef::Elem(v, key) => {
                // referencing an element creates it (awk semantics)
                let mut b = v.borrow_mut();
                b.arr.entry(key.clone()).or_insert_with(value::mkuninit).clone()
            }
            LRef::Field(i) => value::mkstrnum(&self.get_field(*i)),
        }
    }

    fn lref_set(&mut self, r: LRef, v: Value) {
        match r {
            LRef::Var(vref) => vref.borrow_mut().val = v,
            LRef::NF => {
                let mut newnf = value::to_num(&v) as i64;
                if newnf < 0 {
                    newnf = 0;
                }
                if newnf as u64 > MAX_FIELDS as u64 {
                    self.die(&format!("NF set to {} is too large (limit {})", newnf, MAX_FIELDS));
                }
                let newnf = newnf as usize;
                if newnf > self.nf && self.fields.len() <= newnf {
                    self.fields.resize(newnf + 1, String::new());
                }
                if newnf > self.nf {
                    for f in &mut self.fields[self.nf + 1..=newnf] {
                        f.clear();
                    }
                }
                self.nf = newnf;
                self.setvar_num("NF", self.nf as f64);
                self.rebuild_record();
            }
            LRef::Elem(vref, key) => {
                vref.borrow_mut().arr.insert(key, v);
            }
            LRef::Field(idx) => {
                let fmt = self.convfmt();
                let s = value::to_str(&v, &fmt);
                self.set_field(idx, &s);
            }
        }
    }

    fn assign_to(&mut self, target: &Node, v: Value) {
        match self.resolve(target) {
            Some(r) => self.lref_set(r, v),
            None => self.die("invalid assignment target"),
        }
    }

    fn eval_assign(&mut self, op: TokType, target: &Node, rhs: &Node) -> Value {
        if op == TokType::Assign {
            let v = self.eval(rhs);
            let stored = v.clone();
            self.assign_to(target, v);
            stored
        } else {
            let lr = match self.resolve(target) {
                Some(r) => r,
                None => self.die("invalid assignment target"),
            };
            // mawk order: evaluate the right side, then read the target
            // (so `x += x++` with x=1 gives 3).
            let rv = self.eval(rhs);
            let r = value::to_num(&rv);
            let cur = self.lref_get(&lr);
            let c = value::to_num(&cur);
            let res = match op {
                TokType::AddAssign => c + r,
                TokType::SubAssign => c - r,
                TokType::MulAssign => c * r,
                TokType::DivAssign => {
                    if r == 0.0 {
                        self.die("division by zero");
                    }
                    c / r
                }
                TokType::ModAssign => {
                    if r == 0.0 {
                        self.die("division by zero");
                    }
                    c % r
                }
                TokType::PowAssign => c.powf(r),
                _ => 0.0,
            };
            self.lref_set(lr, value::mknum(res));
            value::mknum(res)
        }
    }

    fn eval_binop(&mut self, op: TokType, a: &Node, b: &Node) -> Value {
        let av = self.eval(a);
        let bv = self.eval(b);
        match op {
            TokType::Plus | TokType::Minus | TokType::Star | TokType::Slash | TokType::Percent => {
                let x = value::to_num(&av);
                let y = value::to_num(&bv);
                let r = match op {
                    TokType::Plus => x + y,
                    TokType::Minus => x - y,
                    TokType::Star => x * y,
                    TokType::Slash => {
                        if y == 0.0 {
                            self.die("division by zero");
                        }
                        x / y
                    }
                    _ => {
                        if y == 0.0 {
                            self.die("division by zero");
                        }
                        x % y
                    }
                };
                value::mknum(r)
            }
            _ => {
                let numeric = value::is_numericish(&av) && value::is_numericish(&bv);
                let cmp = if numeric {
                    let x = value::to_num(&av);
                    let y = value::to_num(&bv);
                    if x < y { -1 } else if x > y { 1 } else { 0 }
                } else {
                    let fmt = self.convfmt();
                    let sa = value::to_str(&av, &fmt);
                    let sb = value::to_str(&bv, &fmt);
                    match sa.cmp(&sb) {
                        Ordering::Less => -1,
                        Ordering::Equal => 0,
                        Ordering::Greater => 1,
                    }
                };
                let r = match op {
                    TokType::Lt => cmp < 0,
                    TokType::Le => cmp <= 0,
                    TokType::Gt => cmp > 0,
                    TokType::Ge => cmp >= 0,
                    TokType::Eq => cmp == 0,
                    TokType::Ne => cmp != 0,
                    _ => false,
                };
                value::mknum(if r { 1.0 } else { 0.0 })
            }
        }
    }

    fn eval_match(&mut self, op: TokType, a: &Node, b: &Node) -> Value {
        let av = self.eval(a);
        let fmt = self.convfmt();
        let s = value::to_str(&av, &fmt);
        let pat = match b {
            Node::Ere(p) => p.clone(),
            other => {
                let bv = self.eval(other);
                value::to_str(&bv, &fmt)
            }
        };
        let m = regex_engine::regex_match_bool(&pat, &s);
        let m = if op == TokType::NoMatch { !m } else { m };
        value::mknum(if m { 1.0 } else { 0.0 })
    }

    fn eval_call(&mut self, name: &str, args: &[Node]) -> Value {
        if self.funcs.contains_key(name) {
            self.call_user_func(name, args)
        } else {
            self.call_builtin(name, args)
        }
    }

    fn call_user_func(&mut self, name: &str, arg_nodes: &[Node]) -> Value {
        // Deep (usually runaway) recursion: stop cleanly before the stack runs out.
        let marker = 0u8;
        let used = self.stack_base.saturating_sub(&marker as *const u8 as usize);
        if self.stack_base != 0 && used > crate::INTERP_STACK_SIZE - STACK_RESERVE {
            self.die(&format!(
                "function call nesting too deep in {}() ({} calls active)",
                name,
                self.frames.len()
            ));
        }
        let fd = self.funcs.get(name).cloned().unwrap();
        let nparams = fd.params.len();
        let mut pvars: Vec<VarRef> = Vec::with_capacity(nparams);
        // (param slot, caller slot) for untyped variables: if the callee turns
        // the parameter into an array, the caller's variable becomes that array.
        let mut backlinks: Vec<(VarRef, VarRef)> = Vec::new();
        for i in 0..nparams {
            if i < arg_nodes.len() {
                if let Node::Var(vname) = &arg_nodes[i] {
                    let caller = if vname == "NF" && self.frame_lookup(vname).is_none() {
                        new_slot(value::mknum(self.nf as f64))
                    } else {
                        self.var_get(vname)
                    };
                    let (is_arr, uninit) = {
                        let b = caller.borrow();
                        (b.is_arr, b.val.uninit)
                    };
                    if is_arr {
                        // arrays are passed by reference
                        pvars.push(caller);
                    } else if uninit {
                        let slot = new_slot(value::mkuninit());
                        backlinks.push((slot.clone(), caller));
                        pvars.push(slot);
                    } else {
                        // scalars are passed by value
                        let v = caller.borrow().val.clone();
                        pvars.push(new_slot(v));
                    }
                } else {
                    let v = self.eval(&arg_nodes[i]);
                    pvars.push(new_slot(v));
                }
            } else {
                pvars.push(new_slot(value::mkuninit()));
            }
        }
        let mut frame_map = HashMap::new();
        for (pname, vref) in fd.params.iter().zip(pvars.into_iter()) {
            frame_map.insert(pname.clone(), vref);
        }
        self.frames.push(frame_map);
        self.exec_stmt(&fd.body);
        let ret = if self.flow == Flow::Return {
            self.flow = Flow::None;
            std::mem::replace(&mut self.retval, value::mkuninit())
        } else {
            value::mkuninit()
        };
        self.frames.pop();
        for (slot, caller) in backlinks {
            let mut s = slot.borrow_mut();
            if s.is_arr {
                let mut c = caller.borrow_mut();
                if !c.is_arr && c.val.uninit {
                    c.is_arr = true;
                    c.arr = std::mem::take(&mut s.arr);
                }
            }
        }
        ret
    }
}

impl Interp {
    // ---- getline / I/O streams ----

    fn eval_getline(&mut self, target: Option<&Node>, src: Option<&Node>, mode: u8) -> Value {
        match mode {
            0 => match self.getline_read_main() {
                None => value::mknum(0.0),
                Some(r) => {
                    let nr = self.getvar_num("NR") + 1.0;
                    self.setvar_num("NR", nr);
                    let fnr = self.getvar_num("FNR") + 1.0;
                    self.setvar_num("FNR", fnr);
                    match target {
                        Some(t) => self.assign_to(t, value::mkstrnum_owned(r)),
                        None => self.set_record(&r),
                    }
                    value::mknum(1.0)
                }
            },
            1 => {
                let fv = self.eval(src.unwrap());
                let fmt = self.convfmt();
                let fname = value::to_str(&fv, &fmt);
                if !self.io_open_input(&fname, false) {
                    return value::mknum(-1.0);
                }
                match self.read_from_input_key(&fname) {
                    Err(_) => value::mknum(-1.0),
                    Ok(None) => value::mknum(0.0),
                    Ok(Some(r)) => {
                        match target {
                            Some(t) => self.assign_to(t, value::mkstrnum_owned(r)),
                            None => self.set_record(&r),
                        }
                        value::mknum(1.0)
                    }
                }
            }
            _ => {
                let cv = self.eval(src.unwrap());
                let fmt = self.convfmt();
                let cmd = value::to_str(&cv, &fmt);
                if !self.io_open_input(&cmd, true) {
                    return value::mknum(-1.0);
                }
                match self.read_from_input_key(&cmd) {
                    Err(_) => value::mknum(-1.0),
                    Ok(None) => value::mknum(0.0),
                    Ok(Some(r)) => {
                        let nr = self.getvar_num("NR") + 1.0;
                        self.setvar_num("NR", nr);
                        match target {
                            Some(t) => self.assign_to(t, value::mkstrnum_owned(r)),
                            None => self.set_record(&r),
                        }
                        value::mknum(1.0)
                    }
                }
            }
        }
    }

    fn io_open_input(&mut self, key: &str, is_cmd: bool) -> bool {
        if self.inputs.contains_key(key) {
            return true;
        }
        if is_cmd {
            self.flush_all();
            match shell_command(key).stdout(Stdio::piped()).spawn() {
                Ok(mut child) => {
                    let stdout = child.stdout.take().unwrap();
                    self.inputs.insert(
                        key.to_string(),
                        InputEnt { reader: Box::new(std::io::BufReader::new(stdout)), child: Some(child), leftover: String::new() },
                    );
                    true
                }
                Err(_) => false,
            }
        } else {
            match std::fs::File::open(key) {
                Ok(f) => {
                    self.inputs.insert(
                        key.to_string(),
                        InputEnt { reader: Box::new(std::io::BufReader::new(f)), child: None, leftover: String::new() },
                    );
                    true
                }
                Err(_) => false,
            }
        }
    }

    fn io_open_output(&mut self, key: &str, mode: u8) -> bool {
        if self.outputs.contains_key(key) {
            return true;
        }
        if mode == 2 {
            self.flush_all();
            match shell_command(key).stdin(Stdio::piped()).spawn() {
                Ok(mut child) => {
                    let stdin = child.stdin.take().unwrap();
                    self.outputs.insert(
                        key.to_string(),
                        OutputEnt { writer: Box::new(std::io::BufWriter::new(stdin)), child: Some(child) },
                    );
                    true
                }
                Err(_) => false,
            }
        } else {
            let f = std::fs::OpenOptions::new()
                .write(true)
                .create(true)
                .truncate(mode == 0)
                .append(mode == 1)
                .open(key);
            match f {
                Ok(file) => {
                    self.outputs.insert(
                        key.to_string(),
                        OutputEnt { writer: Box::new(std::io::BufWriter::new(file)), child: None },
                    );
                    true
                }
                Err(_) => false,
            }
        }
    }

    fn io_close(&mut self, key: &str) -> i32 {
        let mut rc = -1;
        if let Some(mut ent) = self.inputs.remove(key) {
            if let Some(mut child) = ent.child.take() {
                drop(ent.reader);
                rc = child.wait().map(|s| s.code().unwrap_or(-1)).unwrap_or(-1);
            } else {
                rc = 0;
            }
        }
        if let Some(ent) = self.outputs.remove(key) {
            let OutputEnt { mut writer, child } = ent;
            let _ = writer.flush();
            drop(writer);
            if let Some(mut child) = child {
                rc = child.wait().map(|s| s.code().unwrap_or(-1)).unwrap_or(-1);
            } else {
                rc = 0;
            }
        }
        rc
    }

    fn io_close_all(&mut self) {
        let keys: Vec<String> = self.inputs.keys().cloned().collect();
        for k in keys {
            self.io_close(&k);
        }
        let keys: Vec<String> = self.outputs.keys().cloned().collect();
        for k in keys {
            self.io_close(&k);
        }
    }

    fn flush_all(&mut self) {
        if let Err(e) = self.stdout.flush() {
            if e.kind() == std::io::ErrorKind::BrokenPipe {
                std::process::exit(141);
            }
        }
        for (_, ent) in self.outputs.iter_mut() {
            let _ = ent.writer.flush();
        }
    }

    fn read_from_input_key(&mut self, key: &str) -> std::io::Result<Option<String>> {
        let rs = self.getvar_str("RS");
        let mut leftover = match self.inputs.get_mut(key) {
            Some(ent) => std::mem::take(&mut ent.leftover),
            None => return Ok(None),
        };
        let rec = {
            let ent = self.inputs.get_mut(key).unwrap();
            read_record_generic(ent.reader.as_mut(), &mut leftover, &rs)
        };
        if let Some(ent) = self.inputs.get_mut(key) {
            ent.leftover = leftover;
        }
        rec
    }

    fn open_next_main_file(&mut self) -> bool {
        self.main_input.fp = None;
        loop {
            let argc = self.getvar_num("ARGC") as i64;
            if self.main_input.argv_pos >= argc {
                return false;
            }
            let idx = self.main_input.argv_pos;
            let argv_ref = self.var_get("ARGV");
            let fmt = self.convfmt();
            let arg = {
                let b = argv_ref.borrow();
                b.arr.get(&idx.to_string()).map(|v| value::to_str(v, &fmt)).unwrap_or_default()
            };
            if arg.is_empty() {
                self.main_input.argv_pos += 1;
                continue;
            }
            if let Some(eqpos) = arg.find('=') {
                if eqpos != 0 {
                    let name = &arg[..eqpos];
                    let val = &arg[eqpos + 1..];
                    let ok = name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
                        && !name.as_bytes()[0].is_ascii_digit();
                    if ok {
                        let (name, val) = (name.to_string(), process_escapes(val));
                        self.setvar_strnum(&name, &val);
                        self.main_input.argv_pos += 1;
                        continue;
                    }
                }
            }
            let reader: Option<Box<dyn BufRead>> = if arg == "-" {
                Some(Box::new(std::io::BufReader::new(std::io::stdin())))
            } else {
                match std::fs::File::open(&arg) {
                    Ok(f) => Some(Box::new(std::io::BufReader::new(f))),
                    Err(e) => {
                        self.die(&format!("cannot open \"{}\" ({})", arg, os_err_msg(&e)));
                    }
                }
            };
            self.setvar_str("FILENAME", &arg);
            self.setvar_num("FNR", 0.0);
            self.main_input.fp = reader;
            self.main_input.argv_pos += 1;
            return true;
        }
    }

    fn getline_read_main(&mut self) -> Option<String> {
        loop {
            if self.main_input.fp.is_none() {
                if self.main_input.all_done {
                    return None;
                }
                if !self.open_next_main_file() {
                    if !self.main_input.any_real_file_seen && !self.main_input.used_stdin_fallback {
                        self.main_input.used_stdin_fallback = true;
                        self.main_input.fp = Some(Box::new(std::io::BufReader::new(std::io::stdin())));
                        self.setvar_str("FILENAME", "");
                        self.setvar_num("FNR", 0.0);
                    } else {
                        self.main_input.all_done = true;
                        return None;
                    }
                } else {
                    self.main_input.any_real_file_seen = true;
                }
            }
            let rs = self.getvar_str("RS");
            let rec = {
                let mut leftover = std::mem::take(&mut self.main_input.leftover);
                let fp = self.main_input.fp.as_mut().unwrap();
                let r = read_record_generic(fp.as_mut(), &mut leftover, &rs);
                self.main_input.leftover = leftover;
                r
            };
            let rec = match rec {
                Ok(r) => r,
                Err(e) => {
                    let name = self.getvar_str("FILENAME");
                    let what = if name.is_empty() { "standard input".to_string() } else { format!("\"{}\"", name) };
                    self.die(&format!("read error on {} ({})", what, os_err_msg(&e)));
                }
            };
            if let Some(rec) = rec {
                return Some(rec);
            }
            self.main_input.fp = None;
            if self.main_input.used_stdin_fallback {
                self.main_input.all_done = true;
                return None;
            }
        }
    }
}

impl Interp {
    // ---- statements ----

    fn exec_block(&mut self, list: &[Stmt]) {
        for s in list {
            if self.flow != Flow::None {
                break;
            }
            self.exec_stmt(s);
        }
    }

    fn exec_stmt(&mut self, s: &Stmt) {
        if self.flow != Flow::None {
            return;
        }
        match s {
            Stmt::Block(list) => self.exec_block(list),
            Stmt::Expr(e) => {
                self.eval(e);
            }
            Stmt::Print(args, mode, target) => self.do_print(args, *mode, target.as_ref()),
            Stmt::Printf(args, mode, target) => self.do_printf(args, *mode, target.as_ref()),
            Stmt::If(c, t, e) => {
                let cv = self.eval(c);
                if value::truthy(&cv) {
                    self.exec_stmt(t);
                } else if let Some(e) = e {
                    self.exec_stmt(e);
                }
            }
            Stmt::While(c, body) => loop {
                let cv = self.eval(c);
                if !value::truthy(&cv) {
                    break;
                }
                self.exec_stmt(body);
                match self.flow {
                    Flow::Break => {
                        self.flow = Flow::None;
                        break;
                    }
                    Flow::Continue => self.flow = Flow::None,
                    Flow::None => {}
                    _ => return,
                }
            },
            Stmt::DoWhile(body, c) => loop {
                self.exec_stmt(body);
                match self.flow {
                    Flow::Break => {
                        self.flow = Flow::None;
                        break;
                    }
                    Flow::Continue => self.flow = Flow::None,
                    Flow::None => {}
                    _ => return,
                }
                let cv = self.eval(c);
                if !value::truthy(&cv) {
                    break;
                }
            },
            Stmt::For(init, cond, incr, body) => {
                if let Some(i) = init {
                    self.exec_stmt(i);
                }
                loop {
                    if let Some(c) = cond {
                        let cv = self.eval(c);
                        if !value::truthy(&cv) {
                            break;
                        }
                    }
                    self.exec_stmt(body);
                    match self.flow {
                        Flow::Break => {
                            self.flow = Flow::None;
                            break;
                        }
                        Flow::Continue => self.flow = Flow::None,
                        Flow::None => {}
                        _ => return,
                    }
                    if let Some(i) = incr {
                        self.exec_stmt(i);
                    }
                }
            }
            Stmt::ForIn(var, arrname, body) => {
                let vref = self.var_get(arrname);
                let keys: Vec<String> = vref.borrow().arr.keys().cloned().collect();
                for k in keys {
                    self.setvar_str(var, &k);
                    self.exec_stmt(body);
                    match self.flow {
                        Flow::Break => {
                            self.flow = Flow::None;
                            break;
                        }
                        Flow::Continue => self.flow = Flow::None,
                        Flow::None => {}
                        _ => break,
                    }
                }
            }
            Stmt::Next => self.flow = Flow::Next,
            Stmt::NextFile => self.flow = Flow::NextFile,
            Stmt::Break => self.flow = Flow::Break,
            Stmt::Continue => self.flow = Flow::Continue,
            Stmt::Return(e) => {
                self.retval = if let Some(e) = e { self.eval(e) } else { value::mkuninit() };
                self.flow = Flow::Return;
            }
            Stmt::Exit(e) => {
                if let Some(e) = e {
                    let v = self.eval(e);
                    self.exit_code = value::to_num(&v) as i32;
                }
                self.flow = Flow::Exit;
            }
            Stmt::Delete(name, idxs) => {
                let vref = self.var_get(name);
                if idxs.is_empty() {
                    vref.borrow_mut().arr.clear();
                } else {
                    let key = self.build_key(idxs);
                    vref.borrow_mut().arr.remove(&key);
                }
            }
        }
    }

    fn write_output(&mut self, mode: RedirMode, target: Option<&Node>, text: &str) {
        match mode {
            RedirMode::None => {
                if let Err(e) = self.stdout.write_all(text.as_bytes()) {
                    self.write_failed(e, "stdout");
                }
            }
            _ => {
                let key = {
                    let v = self.eval(target.unwrap());
                    let fmt = self.convfmt();
                    value::to_str(&v, &fmt)
                };
                let iomode = match mode {
                    RedirMode::Trunc => 0,
                    RedirMode::Append => 1,
                    RedirMode::Pipe => 2,
                    RedirMode::None => unreachable!(),
                };
                if iomode != 2 && (key == "/dev/stdout" || key == "-") {
                    if let Err(e) = self.stdout.write_all(text.as_bytes()) {
                        self.write_failed(e, "stdout");
                    }
                    return;
                }
                if iomode != 2 && key == "/dev/stderr" {
                    let _ = self.stdout.flush();
                    let _ = std::io::stderr().write_all(text.as_bytes());
                    return;
                }
                if !self.io_open_output(&key, iomode) {
                    eprintln!("mawk: cannot open '{}' for output", key);
                    std::process::exit(2);
                }
                let res = match self.outputs.get_mut(&key) {
                    Some(ent) => ent.writer.write_all(text.as_bytes()),
                    None => Ok(()),
                };
                if let Err(e) = res {
                    self.write_failed(e, &key);
                }
            }
        }
    }

    fn do_print(&mut self, args: &[Node], mode: RedirMode, target: Option<&Node>) {
        let ofs = self.getvar_str("OFS");
        let ors = self.getvar_str("ORS");
        let mut out = String::new();
        if args.is_empty() {
            out.push_str(&self.record);
        } else {
            for (i, a) in args.iter().enumerate() {
                if i > 0 {
                    out.push_str(&ofs);
                }
                let v = self.eval(a);
                let fmt = self.ofmt();
                out.push_str(&value::to_str(&v, &fmt));
            }
        }
        out.push_str(&ors);
        self.write_output(mode, target, &out);
    }

    fn do_printf(&mut self, args: &[Node], mode: RedirMode, target: Option<&Node>) {
        if args.is_empty() {
            return;
        }
        let fmt_v = self.eval(&args[0]);
        let fmt = { let f = self.convfmt(); value::to_str(&fmt_v, &f) };
        let out = self.do_sprintf(&fmt, &args[1..]);
        self.write_output(mode, target, &out);
    }
}

impl Interp {
    // ---- builtin functions ----

    fn call_builtin(&mut self, name: &str, args: &[Node]) -> Value {
        match name {
            "length" => {
                if args.is_empty() {
                    return value::mknum(self.record.as_bytes().len() as f64);
                }
                if let Node::Var(vname) = &args[0] {
                    if let Some(vref) = self.var_find(vname) {
                        if vref.borrow().is_arr {
                            let n = vref.borrow().arr.len();
                            return value::mknum(n as f64);
                        }
                    }
                }
                let v = self.eval(&args[0]);
                let fmt = self.convfmt();
                value::mknum(value::to_str(&v, &fmt).as_bytes().len() as f64)
            }
            "substr" => self.builtin_substr(args),
            "index" => {
                let sv = self.eval(&args[0]);
                let tv = self.eval(&args[1]);
                let fmt = self.convfmt();
                let ss = value::to_str(&sv, &fmt);
                let tt = value::to_str(&tv, &fmt);
                match ss.find(&tt) {
                    Some(pos) => value::mknum((pos + 1) as f64),
                    None => value::mknum(0.0),
                }
            }
            "match" => self.builtin_match(args),
            "split" => self.builtin_split(args),
            "sub" => self.do_sub(false, args),
            "gsub" => self.do_sub(true, args),
            "toupper" | "tolower" => {
                let v = self.eval(&args[0]);
                let fmt = self.convfmt();
                let s = value::to_str(&v, &fmt);
                let out: String = if name == "toupper" {
                    s.chars().map(|c| c.to_ascii_uppercase()).collect()
                } else {
                    s.chars().map(|c| c.to_ascii_lowercase()).collect()
                };
                value::mkstring(out)
            }
            "sprintf" => {
                let fv = self.eval(&args[0]);
                let fmt = { let f = self.convfmt(); value::to_str(&fv, &f) };
                value::mkstring(self.do_sprintf(&fmt, &args[1..]))
            }
            "int" => {
                let v = self.eval(&args[0]);
                value::mknum(value::to_num(&v).trunc())
            }
            "sin" => {
                let v = self.eval(&args[0]);
                value::mknum(value::to_num(&v).sin())
            }
            "cos" => {
                let v = self.eval(&args[0]);
                value::mknum(value::to_num(&v).cos())
            }
            "atan2" => {
                let a = self.eval(&args[0]);
                let b = self.eval(&args[1]);
                value::mknum(value::to_num(&a).atan2(value::to_num(&b)))
            }
            "sqrt" => {
                let v = self.eval(&args[0]);
                value::mknum(value::to_num(&v).sqrt())
            }
            "exp" => {
                let v = self.eval(&args[0]);
                value::mknum(value::to_num(&v).exp())
            }
            "log" => {
                let v = self.eval(&args[0]);
                value::mknum(value::to_num(&v).ln())
            }
            "rand" => value::mknum(self.rng.next_f64()),
            "srand" => {
                let seed = if !args.is_empty() {
                    let v = self.eval(&args[0]);
                    value::to_num(&v)
                } else {
                    std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .map(|d| d.as_secs() as f64)
                        .unwrap_or(0.0)
                };
                self.rng = Rng::new(seed as i64 as u64);
                let prev = std::mem::replace(&mut self.seed, seed);
                value::mknum(prev)
            }
            "system" => {
                let v = self.eval(&args[0]);
                let fmt = self.convfmt();
                let cmd = value::to_str(&v, &fmt);
                self.flush_all();
                let status = shell_command(&cmd).status();
                let rc = status.map(|s| s.code().unwrap_or(-1)).unwrap_or(-1);
                value::mknum(rc as f64)
            }
            "close" => {
                let v = self.eval(&args[0]);
                let fmt = self.convfmt();
                let key = value::to_str(&v, &fmt);
                value::mknum(self.io_close(&key) as f64)
            }
            "fflush" => {
                if args.is_empty() {
                    self.flush_all();
                    return value::mknum(0.0);
                }
                let v = self.eval(&args[0]);
                let fmt = self.convfmt();
                let key = value::to_str(&v, &fmt);
                if key == "/dev/stdout" || key == "-" {
                    let _ = self.stdout.flush();
                    return value::mknum(0.0);
                }
                match self.outputs.get_mut(&key) {
                    Some(ent) => {
                        let _ = ent.writer.flush();
                        value::mknum(0.0)
                    }
                    None => value::mknum(-1.0),
                }
            }
            _ => {
                eprintln!("mawk: unknown function '{}'", name);
                std::process::exit(2);
            }
        }
    }

    fn builtin_substr(&mut self, args: &[Node]) -> Value {
        let sv = self.eval(&args[0]);
        let fmt = self.convfmt();
        let s = value::to_str(&sv, &fmt);
        let bytes = s.as_bytes();
        let len = bytes.len() as f64;
        let m = {
            let mv = self.eval(&args[1]);
            value::to_num(&mv).trunc()
        };
        let n = if args.len() > 2 {
            let nv = self.eval(&args[2]);
            value::to_num(&nv).trunc()
        } else {
            f64::INFINITY
        };
        // POSIX: characters m .. m+n-1, clipped to 1 .. len. Done in f64 so
        // huge/infinite/NaN arguments can't overflow (NaN compares false -> "").
        let first = m.max(1.0);
        let past_end = (m + n).min(len + 1.0);
        if !(first < past_end) {
            return value::mkstr("");
        }
        let start = (first - 1.0) as usize;
        let end = (past_end - 1.0) as usize;
        value::mkstring(String::from_utf8_lossy(&bytes[start..end]).into_owned())
    }

    fn builtin_match(&mut self, args: &[Node]) -> Value {
        let sv = self.eval(&args[0]);
        let fmt = self.convfmt();
        let s = value::to_str(&sv, &fmt);
        let pat = match &args[1] {
            Node::Ere(p) => p.clone(),
            other => {
                let v = self.eval(other);
                value::to_str(&v, &fmt)
            }
        };
        match regex_engine::regex_search(&pat, &s) {
            Some((ms, ml)) => {
                self.setvar_num("RSTART", (ms + 1) as f64);
                self.setvar_num("RLENGTH", ml as f64);
                value::mknum((ms + 1) as f64)
            }
            None => {
                self.setvar_num("RSTART", 0.0);
                self.setvar_num("RLENGTH", -1.0);
                value::mknum(0.0)
            }
        }
    }

    fn builtin_split(&mut self, args: &[Node]) -> Value {
        let sv = self.eval(&args[0]);
        let fmt = self.convfmt();
        let s = value::to_str(&sv, &fmt);
        let arrname = match &args[1] {
            Node::Var(n) => n.clone(),
            _ => self.die("split: second argument must be an array name"),
        };
        let fs = if args.len() > 2 {
            match &args[2] {
                Node::Ere(p) => p.clone(),
                other => {
                    let v = self.eval(other);
                    value::to_str(&v, &fmt)
                }
            }
        } else {
            self.getvar_str("FS")
        };
        let pieces = awk_split(&s, &fs);
        let vref = self.var_get(&arrname);
        {
            let mut b = vref.borrow_mut();
            b.arr.clear();
            b.is_arr = true;
            for (i, piece) in pieces.iter().enumerate() {
                b.arr.insert((i + 1).to_string(), value::mkstrnum(piece));
            }
        }
        value::mknum(pieces.len() as f64)
    }

    fn do_sub(&mut self, is_global: bool, args: &[Node]) -> Value {
        let fmt = self.convfmt();
        let pat = match &args[0] {
            Node::Ere(p) => p.clone(),
            other => {
                let v = self.eval(other);
                value::to_str(&v, &fmt)
            }
        };
        let rep_v = self.eval(&args[1]);
        let repl = value::to_str(&rep_v, &fmt);
        // Resolve the target once (so `sub(/x/, "y", a[i++])` bumps i once).
        // A non-lvalue target (e.g. a literal) is matched but not assigned.
        let (lref, cur) = match args.get(2) {
            Some(t) => match self.resolve(t) {
                Some(r) => {
                    let v = self.lref_get(&r);
                    (Some(r), value::to_str(&v, &fmt))
                }
                None => {
                    let v = self.eval(t);
                    (None, value::to_str(&v, &fmt))
                }
            },
            None => (Some(LRef::Field(0)), self.record.clone()),
        };
        let text = cur.as_bytes();
        let repl_bytes = repl.as_bytes();
        let mut out: Vec<u8> = Vec::with_capacity(text.len());
        let mut pos = 0usize;
        let mut last_match_end: Option<usize> = None;
        let mut nsubs = 0i64;
        // POSIX gsub: an empty match directly after the previous match is not
        // replaced ("abc" gsub(/b*/,"-") -> "-a-c-"), and an empty match at the
        // very end is (gsub(/x*/,"-","abc") -> "-a-b-c-").
        while pos <= text.len() {
            let (ms, ml) = match regex_engine::regex_search_at(&pat, text, pos) {
                Some(m) => m,
                None => break,
            };
            if ml == 0 && last_match_end == Some(ms) {
                if ms < text.len() {
                    out.push(text[ms]);
                }
                pos = ms + 1;
                continue;
            }
            out.extend_from_slice(&text[pos..ms]);
            let mut i = 0usize;
            while i < repl_bytes.len() {
                if repl_bytes[i] == b'\\' && i + 1 < repl_bytes.len() && repl_bytes[i + 1] == b'&' {
                    out.push(b'&');
                    i += 2;
                } else if repl_bytes[i] == b'\\' && i + 1 < repl_bytes.len() && repl_bytes[i + 1] == b'\\' {
                    out.push(b'\\');
                    i += 2;
                } else if repl_bytes[i] == b'&' {
                    out.extend_from_slice(&text[ms..ms + ml]);
                    i += 1;
                } else {
                    out.push(repl_bytes[i]);
                    i += 1;
                }
            }
            nsubs += 1;
            last_match_end = Some(ms + ml);
            if ml == 0 {
                if ms < text.len() {
                    out.push(text[ms]);
                }
                pos = ms + 1;
            } else {
                pos = ms + ml;
            }
            if !is_global {
                break;
            }
        }
        if pos < text.len() {
            out.extend_from_slice(&text[pos..]);
        }
        if nsubs > 0 {
            if let Some(r) = lref {
                let outs = String::from_utf8_lossy(&out).into_owned();
                self.lref_set(r, value::mkstring(outs));
            }
        }
        value::mknum(nsubs as f64)
    }

    // ---- sprintf / printf ----

    fn next_arg(&mut self, args: &[Node], ai: &mut usize) -> Option<Value> {
        if *ai < args.len() {
            let v = self.eval(&args[*ai]);
            *ai += 1;
            Some(v)
        } else {
            None
        }
    }

    fn do_sprintf(&mut self, fmt: &str, args: &[Node]) -> String {
        // Work in bytes so non-ASCII text in the format survives untouched.
        let fb = fmt.as_bytes();
        let mut out: Vec<u8> = Vec::with_capacity(fb.len() + 16);
        let mut ai = 0usize;
        let mut i = 0usize;
        while i < fb.len() {
            if fb[i] != b'%' {
                out.push(fb[i]);
                i += 1;
                continue;
            }
            if i + 1 < fb.len() && fb[i + 1] == b'%' {
                out.push(b'%');
                i += 2;
                continue;
            }
            let spec_start = i;
            i += 1;
            let mut flags = String::new();
            while i < fb.len() && matches!(fb[i], b'-' | b'+' | b' ' | b'0' | b'#') {
                flags.push(fb[i] as char);
                i += 1;
            }
            let mut width: Option<i64> = None;
            if i < fb.len() && fb[i] == b'*' {
                i += 1;
                let w = self.next_arg(args, &mut ai).map(|v| value::to_num(&v) as i64).unwrap_or(0);
                if w < 0 {
                    flags.push('-');
                }
                width = Some(w.saturating_abs());
            } else {
                let wstart = i;
                while i < fb.len() && fb[i].is_ascii_digit() {
                    i += 1;
                }
                if i > wstart {
                    // too many digits to parse -> treat as "too large"
                    width = Some(std::str::from_utf8(&fb[wstart..i]).ok().and_then(|s| s.parse().ok()).unwrap_or(i64::MAX));
                }
            }
            let mut precision: Option<i64> = None;
            if i < fb.len() && fb[i] == b'.' {
                i += 1;
                if i < fb.len() && fb[i] == b'*' {
                    i += 1;
                    let p = self.next_arg(args, &mut ai).map(|v| value::to_num(&v) as i64).unwrap_or(0);
                    precision = if p < 0 { None } else { Some(p) };
                } else {
                    let pstart = i;
                    while i < fb.len() && fb[i].is_ascii_digit() {
                        i += 1;
                    }
                    let pstr = std::str::from_utf8(&fb[pstart..i]).unwrap_or("");
                    precision = Some(if pstr.is_empty() { 0 } else { pstr.parse().unwrap_or(i64::MAX) });
                }
            }
            if width.unwrap_or(0) > MAX_PRINTF_WIDTH || precision.unwrap_or(0) > MAX_PRINTF_WIDTH {
                self.die(&format!(
                    "printf: field width or precision too large (limit {}) in \"{}\"",
                    MAX_PRINTF_WIDTH,
                    fmt
                ));
            }
            // C length modifiers are accepted and ignored: %ld, %lld, %hd
            while i < fb.len() && matches!(fb[i], b'l' | b'h' | b'L' | b'q' | b'j' | b'z') {
                i += 1;
            }
            if i >= fb.len() {
                out.extend_from_slice(&fb[spec_start..]);
                break;
            }
            let conv = fb[i] as char;
            i += 1;
            match conv {
                'd' | 'i' | 'o' | 'x' | 'X' | 'u' => {
                    let v = self.next_arg(args, &mut ai).unwrap_or_else(|| value::mknum(0.0));
                    out.extend_from_slice(fmt_int(&flags, width, precision, conv, &v).as_bytes());
                }
                'f' | 'F' | 'e' | 'E' | 'g' | 'G' => {
                    let v = self.next_arg(args, &mut ai).unwrap_or_else(|| value::mknum(0.0));
                    out.extend_from_slice(fmt_float(&flags, width, precision, conv, &v).as_bytes());
                }
                's' => {
                    let v = self.next_arg(args, &mut ai).unwrap_or_else(|| value::mkstr(""));
                    let fmtc = self.convfmt();
                    let s = value::to_str(&v, &fmtc);
                    out.extend_from_slice(fmt_str(&flags, width, precision, &s).as_bytes());
                }
                'c' => {
                    let v = self.next_arg(args, &mut ai).unwrap_or_else(|| value::mkstr(""));
                    out.extend_from_slice(&fmt_char(&flags, width, &v));
                }
                _ => {
                    out.extend_from_slice(&fb[spec_start..i]);
                }
            }
        }
        String::from_utf8_lossy(&out).into_owned()
    }
}

// ---- free helper functions ----

fn awk_split(s: &str, fs: &str) -> Vec<String> {
    let bytes = s.as_bytes();
    let mut result = Vec::new();
    if bytes.is_empty() {
        return result; // an empty record has no fields, whatever FS is
    }
    if fs.is_empty() {
        // FS = "" (mawk/gawk extension): every character is a field
        return s.chars().map(|c| c.to_string()).collect();
    }
    if fs == " " {
        let mut i = 0usize;
        while i < bytes.len() {
            while i < bytes.len() && bytes[i].is_ascii_whitespace() {
                i += 1;
            }
            if i >= bytes.len() {
                break;
            }
            let start = i;
            while i < bytes.len() && !bytes[i].is_ascii_whitespace() {
                i += 1;
            }
            result.push(String::from_utf8_lossy(&bytes[start..i]).into_owned());
        }
    } else if fs.chars().count() == 1 && fs != "\\" {
        let sep = fs.as_bytes()[0];
        let mut start = 0usize;
        loop {
            let mut i = start;
            while i < bytes.len() && bytes[i] != sep {
                i += 1;
            }
            result.push(String::from_utf8_lossy(&bytes[start..i]).into_owned());
            if i >= bytes.len() {
                break;
            }
            start = i + 1;
        }
    } else {
        // Search the whole record from an offset so `^` in FS only matches
        // at the start; empty matches never split.
        let mut pos = 0usize;
        let mut from = 0usize;
        while from <= bytes.len() {
            match regex_engine::regex_search_at(fs, bytes, from) {
                Some((ms, ml)) if ml > 0 => {
                    result.push(String::from_utf8_lossy(&bytes[pos..ms]).into_owned());
                    pos = ms + ml;
                    from = pos;
                }
                Some((ms, _)) => from = ms + 1,
                None => break,
            }
        }
        result.push(String::from_utf8_lossy(&bytes[pos..]).into_owned());
    }
    result
}

/// Reads one '\n'-terminated line. Ok(None) at end of input; read errors
/// (e.g. the "file" is a directory) are returned, not treated as EOF.
fn read_line_raw(reader: &mut dyn BufRead) -> std::io::Result<Option<String>> {
    let mut buf: Vec<u8> = Vec::new();
    if reader.read_until(b'\n', &mut buf)? == 0 {
        return Ok(None);
    }
    if buf.last() == Some(&b'\n') {
        buf.pop();
    }
    if buf.last() == Some(&b'\r') {
        buf.pop();
    }
    Ok(Some(String::from_utf8_lossy(&buf).into_owned()))
}

fn read_byte(reader: &mut dyn BufRead) -> std::io::Result<Option<u8>> {
    let mut one = [0u8; 1];
    loop {
        match reader.read(&mut one) {
            Ok(0) => return Ok(None),
            Ok(_) => return Ok(Some(one[0])),
            Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
}

fn read_record_generic(
    reader: &mut dyn BufRead,
    leftover: &mut String,
    rs: &str,
) -> std::io::Result<Option<String>> {
    if rs.is_empty() {
        // paragraph mode: records are separated by blank lines
        let mut line;
        loop {
            line = match read_line_raw(reader)? {
                Some(l) => l,
                None => return Ok(None),
            };
            if !line.is_empty() {
                break;
            }
        }
        let mut buf = line;
        while let Some(l) = read_line_raw(reader)? {
            if l.is_empty() {
                break;
            }
            buf.push('\n');
            buf.push_str(&l);
        }
        return Ok(Some(buf));
    }
    if rs.chars().count() == 1 {
        let c = rs.chars().next().unwrap();
        if c == '\n' {
            return read_line_raw(reader);
        }
        let cbyte = c as u8;
        let mut buf: Vec<u8> = Vec::new();
        let mut any = false;
        while let Some(b) = read_byte(reader)? {
            any = true;
            if b == cbyte {
                break;
            }
            buf.push(b);
        }
        if !any {
            return Ok(None);
        }
        return Ok(Some(String::from_utf8_lossy(&buf).into_owned()));
    }
    // multi-char RS: regex-based, using a leftover buffer
    loop {
        if !leftover.is_empty() {
            if let Some((ms, ml)) = regex_engine::regex_search(rs, leftover) {
                if ml > 0 {
                    let rec = leftover[..ms].to_string();
                    let rest = leftover[ms + ml..].to_string();
                    *leftover = rest;
                    return Ok(Some(rec));
                }
            }
        }
        let mut chunk = [0u8; 4096];
        let got = loop {
            match reader.read(&mut chunk) {
                Ok(n) => break n,
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(e) => return Err(e),
            }
        };
        if got == 0 {
            if !leftover.is_empty() {
                return Ok(Some(std::mem::take(leftover)));
            }
            return Ok(None);
        }
        leftover.push_str(&String::from_utf8_lossy(&chunk[..got]));
    }
}

// ---- printf-style formatting ----

fn pad(s: String, width: Option<i64>, left_align: bool, zero_pad: bool, numeric: bool) -> String {
    let w = width.unwrap_or(0).max(0) as usize;
    let slen = s.chars().count();
    if slen >= w {
        return s;
    }
    let padlen = w - slen;
    if left_align {
        format!("{}{}", s, " ".repeat(padlen))
    } else if zero_pad && numeric {
        if let Some(rest) = s.strip_prefix('-') {
            format!("-{}{}", "0".repeat(padlen), rest)
        } else if let Some(rest) = s.strip_prefix('+') {
            format!("+{}{}", "0".repeat(padlen), rest)
        } else if let Some(rest) = s.strip_prefix(' ') {
            format!(" {}{}", "0".repeat(padlen), rest)
        } else {
            format!("{}{}", "0".repeat(padlen), s)
        }
    } else {
        format!("{}{}", " ".repeat(padlen), s)
    }
}

fn fmt_int(flags: &str, width: Option<i64>, precision: Option<i64>, conv: char, v: &Value) -> String {
    let left = flags.contains('-');
    let zero = flags.contains('0') && !left && precision.is_none();
    let plus = flags.contains('+');
    let space = flags.contains(' ');
    let alt = flags.contains('#');
    let d = value::to_num(v);
    let n: i64 = if d.is_finite() { d as i64 } else { 0 };
    let body = match conv {
        'd' | 'i' => {
            let mag = n.unsigned_abs();
            let mut s = mag.to_string();
            if let Some(p) = precision {
                let p = p.max(0) as usize;
                while s.len() < p {
                    s.insert(0, '0');
                }
                if p == 0 && mag == 0 {
                    s.clear();
                }
            }
            if n < 0 {
                format!("-{}", s)
            } else if plus {
                format!("+{}", s)
            } else if space {
                format!(" {}", s)
            } else {
                s
            }
        }
        'u' => {
            let u = n as u64;
            let mut s = u.to_string();
            apply_int_precision(&mut s, precision);
            s
        }
        'o' => {
            let u = n as u64;
            let mut s = format!("{:o}", u);
            apply_int_precision(&mut s, precision);
            if alt && !s.starts_with('0') {
                s.insert(0, '0');
            }
            s
        }
        'x' => {
            let u = n as u64;
            let mut s = format!("{:x}", u);
            apply_int_precision(&mut s, precision);
            if alt && u != 0 {
                s = format!("0x{}", s);
            }
            s
        }
        'X' => {
            let u = n as u64;
            let mut s = format!("{:X}", u);
            apply_int_precision(&mut s, precision);
            if alt && u != 0 {
                s = format!("0X{}", s);
            }
            s
        }
        _ => String::new(),
    };
    pad(body, width, left, zero, true)
}

fn apply_int_precision(s: &mut String, precision: Option<i64>) {
    if let Some(p) = precision {
        let p = p.max(0) as usize;
        while s.len() < p {
            s.insert(0, '0');
        }
    }
}

fn format_exp(x: f64, prec: usize, upper: bool) -> String {
    if x == 0.0 {
        let mantissa = format!("{:.*}", prec, 0.0);
        return format!("{}{}+00", mantissa, if upper { "E" } else { "e" });
    }
    let mut exp = x.abs().log10().floor() as i32;
    let mut mantissa = x.abs() / 10f64.powi(exp);
    let mut mstr = format!("{:.*}", prec, mantissa);
    if mstr.starts_with("10") {
        exp += 1;
        mantissa = x.abs() / 10f64.powi(exp);
        mstr = format!("{:.*}", prec, mantissa);
    }
    format!("{}{}{}{:02}", mstr, if upper { "E" } else { "e" }, if exp < 0 { "-" } else { "+" }, exp.abs())
}

fn trim_trailing_zeros(s: &str) -> String {
    if !s.contains('.') {
        return s.to_string();
    }
    let t = s.trim_end_matches('0');
    let t = t.trim_end_matches('.');
    t.to_string()
}

fn format_g(x: f64, prec: usize, upper: bool, alt: bool) -> String {
    let p = if prec == 0 { 1 } else { prec };
    if x == 0.0 {
        return if alt { format!("0.{}", "0".repeat(p.saturating_sub(1))) } else { "0".to_string() };
    }
    let exp = x.abs().log10().floor() as i32;
    if exp < -4 || exp >= p as i32 {
        let s = format_exp(x, p.saturating_sub(1), upper);
        if alt {
            s
        } else if let Some(epos) = s.find(|c| c == 'e' || c == 'E') {
            let (mant, exp_part) = s.split_at(epos);
            format!("{}{}", trim_trailing_zeros(mant), exp_part)
        } else {
            s
        }
    } else {
        let decimals = (p as i32 - 1 - exp).max(0) as usize;
        let s = format!("{:.*}", decimals, x.abs());
        if alt { s } else { trim_trailing_zeros(&s) }
    }
}

fn fmt_float(flags: &str, width: Option<i64>, precision: Option<i64>, conv: char, v: &Value) -> String {
    let left = flags.contains('-');
    let zero = flags.contains('0') && !left;
    let plus = flags.contains('+');
    let space = flags.contains(' ');
    let alt = flags.contains('#');
    let d = value::to_num(v);
    let prec = precision.unwrap_or(6).max(0) as usize;
    let body = if d.is_nan() {
        "nan".to_string()
    } else if d.is_infinite() {
        "inf".to_string()
    } else {
        match conv {
            'f' | 'F' => format!("{:.*}", prec, d.abs()),
            'e' => format_exp(d.abs(), prec, false),
            'E' => format_exp(d.abs(), prec, true),
            'g' => format_g(d.abs(), prec, false, alt),
            'G' => format_g(d.abs(), prec, true, alt),
            _ => String::new(),
        }
    };
    let sign = if d.is_sign_negative() { "-" } else if plus { "+" } else if space { " " } else { "" };
    let signed = format!("{}{}", sign, body);
    pad(signed, width, left, zero && !d.is_nan() && !d.is_infinite(), true)
}

fn fmt_str(flags: &str, width: Option<i64>, precision: Option<i64>, s: &str) -> String {
    let left = flags.contains('-');
    let mut out = s.to_string();
    if let Some(p) = precision {
        let p = p.max(0) as usize;
        if out.as_bytes().len() > p {
            out = String::from_utf8_lossy(&out.as_bytes()[..p]).into_owned();
        }
    }
    pad(out, width, left, false, false)
}

/// %c: a number -- including a numeric-looking field such as "65" -- prints
/// the byte with that value (mod 256, like C awk); a string prints its first
/// character (whole UTF-8 sequence).
fn fmt_char(flags: &str, width: Option<i64>, v: &Value) -> Vec<u8> {
    let left = flags.contains('-');
    let body: Vec<u8> = if value::is_numericish(v) && !v.uninit {
        let n = (value::to_num(v) as i64 & 0xFF) as u8;
        if n == 0 { Vec::new() } else { vec![n] }
    } else {
        match &v.str {
            Some(s) => s.chars().next().map(|c| c.to_string().into_bytes()).unwrap_or_default(),
            None => Vec::new(),
        }
    };
    let w = width.unwrap_or(0).max(0) as usize;
    let len = if body.is_empty() { 0 } else { 1 };
    if len >= w {
        return body;
    }
    let pad = vec![b' '; w - len];
    if left { [body, pad].concat() } else { [pad, body].concat() }
}

/// Formats a single f64 through a printf-style format string (used for
/// CONVFMT/OFMT, e.g. "%.6g"); any extra specs beyond the first default to 0.
pub fn sprintf_one_num(fmt: &str, d: f64) -> String {
    let fb = fmt.as_bytes();
    let mut out = String::new();
    let mut used = false;
    let mut i = 0usize;
    while i < fb.len() {
        if fb[i] != b'%' {
            out.push(fb[i] as char);
            i += 1;
            continue;
        }
        if i + 1 < fb.len() && fb[i + 1] == b'%' {
            out.push('%');
            i += 2;
            continue;
        }
        let spec_start = i;
        i += 1;
        let mut flags = String::new();
        while i < fb.len() && matches!(fb[i], b'-' | b'+' | b' ' | b'0' | b'#') {
            flags.push(fb[i] as char);
            i += 1;
        }
        let mut width: Option<i64> = None;
        let wstart = i;
        while i < fb.len() && fb[i].is_ascii_digit() {
            i += 1;
        }
        if i > wstart {
            width = std::str::from_utf8(&fb[wstart..i]).ok().and_then(|s| s.parse().ok());
        }
        let mut precision: Option<i64> = None;
        if i < fb.len() && fb[i] == b'.' {
            i += 1;
            let pstart = i;
            while i < fb.len() && fb[i].is_ascii_digit() {
                i += 1;
            }
            let pstr = std::str::from_utf8(&fb[pstart..i]).unwrap_or("");
            precision = Some(if pstr.is_empty() { 0 } else { pstr.parse().unwrap_or(0) });
        }
        if i >= fb.len() {
            out.push_str(std::str::from_utf8(&fb[spec_start..]).unwrap_or(""));
            break;
        }
        let width = width.map(|w| w.min(MAX_PRINTF_WIDTH));
        let precision = precision.map(|p| p.min(MAX_PRINTF_WIDTH));
        let conv = fb[i] as char;
        i += 1;
        let val = if !used {
            used = true;
            d
        } else {
            0.0
        };
        let v = value::mknum(val);
        match conv {
            'd' | 'i' | 'o' | 'x' | 'X' | 'u' => out.push_str(&fmt_int(&flags, width, precision, conv, &v)),
            'f' | 'F' | 'e' | 'E' | 'g' | 'G' => out.push_str(&fmt_float(&flags, width, precision, conv, &v)),
            's' => out.push_str(&fmt_str(&flags, width, precision, &value::to_str(&v, "%.6g"))),
            'c' => out.push_str(&String::from_utf8_lossy(&fmt_char(&flags, width, &v))),
            _ => out.push_str(std::str::from_utf8(&fb[spec_start..i]).unwrap_or("")),
        }
    }
    out
}

impl Interp {
    // ---- driver ----

    fn rule_matches(&mut self, r: &Rule) -> bool {
        match &r.ptype {
            PType::Always => true,
            PType::Expr(e) => {
                let v = self.eval(e);
                value::truthy(&v)
            }
            PType::Range(e1, e2) => {
                if !r.range_active.get() {
                    let v = self.eval(e1);
                    if !value::truthy(&v) {
                        return false;
                    }
                    r.range_active.set(true);
                }
                let v = self.eval(e2);
                if value::truthy(&v) {
                    r.range_active.set(false);
                }
                true
            }
            PType::Begin | PType::End => false,
        }
    }

    fn run_rules_for_record(&mut self, rules: &Rc<Vec<Rule>>) {
        for r in rules.iter() {
            if self.flow != Flow::None {
                break;
            }
            if matches!(r.ptype, PType::Begin) || matches!(r.ptype, PType::End) {
                continue;
            }
            if self.rule_matches(r) {
                match &r.action {
                    Some(a) => self.exec_stmt(a),
                    None => self.do_print(&[], RedirMode::None, None),
                }
            }
            if self.flow == Flow::Next {
                self.flow = Flow::None;
                return;
            }
            if self.flow == Flow::NextFile {
                return;
            }
        }
    }

    fn run_end(&mut self, rules: &Rc<Vec<Rule>>) -> i32 {
        self.flow = Flow::None;
        for r in rules.iter() {
            if matches!(r.ptype, PType::End) {
                if let Some(a) = &r.action {
                    self.exec_stmt(a);
                }
                if self.flow == Flow::Exit {
                    break;
                }
            }
        }
        self.flush_all();
        self.io_close_all();
        self.exit_code
    }

    pub fn run_program(&mut self) -> i32 {
        let rules = self.rules.clone();
        for r in rules.iter() {
            if matches!(r.ptype, PType::Begin) {
                if let Some(a) = &r.action {
                    self.exec_stmt(a);
                }
                if self.flow == Flow::Exit {
                    return self.run_end(&rules);
                }
            }
        }
        self.flow = Flow::None;

        let needs_input = rules.iter().any(|r| !matches!(r.ptype, PType::Begin));
        if needs_input {
            loop {
                let rec = match self.getline_read_main() {
                    Some(r) => r,
                    None => break,
                };
                let nr = self.getvar_num("NR") + 1.0;
                self.setvar_num("NR", nr);
                let fnr = self.getvar_num("FNR") + 1.0;
                self.setvar_num("FNR", fnr);
                self.set_record(&rec);
                self.run_rules_for_record(&rules);
                if self.flow == Flow::Exit {
                    break;
                }
                if self.flow == Flow::NextFile {
                    self.flow = Flow::None;
                    self.main_input.fp = None;
                    if self.main_input.used_stdin_fallback {
                        self.main_input.all_done = true;
                    }
                }
            }
        }
        self.flow = Flow::None;
        self.run_end(&rules)
    }
}

fn version_text() -> String {
    format!(
        "mawk_rs {} (released {})\n\
         An AWK interpreter in Rust, ported from mawk.\n\
         \n\
         compiled limits:\n\
         max fields          {}\n\
         printf width        {}\n\
         interpreter stack   {} MB\n",
        crate::VERSION,
        crate::RELEASE_DATE,
        MAX_FIELDS,
        MAX_PRINTF_WIDTH,
        crate::INTERP_STACK_SIZE / (1024 * 1024)
    )
}

fn usage_text() -> String {
    "usage: mawk [-F fs] [-v var=value] [--] 'program text' [file|var=value ...]\n\
     \x20      mawk [-F fs] [-v var=value] -f progfile [-f progfile ...] [--] [file|var=value ...]\n\
     \n\
     options:\n\
     \x20 -F fs            field separator (-Ft means tab)\n\
     \x20 -v var=value     assign a variable before BEGIN\n\
     \x20 -f progfile      read the program from a file (- for standard input)\n\
     \x20 -W version, --version   show version and limits\n\
     \x20 -W usage,   --help      show this help\n"
        .to_string()
}

/// Reads a -f program file. UTF-8 is expected; a file that isn't valid
/// UTF-8 (e.g. a Windows-1252 script) is decoded as Latin-1 rather than
/// rejected, so its string literals keep their characters.
fn read_program_file(fname: &str) -> Result<String, String> {
    let bytes = if fname == "-" || fname == "/dev/stdin" {
        let mut b = Vec::new();
        std::io::Read::read_to_end(&mut std::io::stdin(), &mut b)
            .map_err(|e| format!("cannot read program from standard input ({})", os_err_msg(&e)))?;
        b
    } else {
        std::fs::read(fname).map_err(|e| format!("cannot open program file \"{}\" ({})", fname, os_err_msg(&e)))?
    };
    Ok(match String::from_utf8(bytes) {
        Ok(s) => s,
        Err(e) => e.into_bytes().iter().map(|&b| b as char).collect(),
    })
}

/// Top-level entry point, equivalent to mawk.c's main().
pub fn run(argv: &[String]) -> Result<i32, String> {
    let mut interp = Interp::new();
    // Remember roughly where this thread's stack starts, for the recursion guard.
    let stack_marker = 0u8;
    interp.stack_base = &stack_marker as *const u8 as usize;

    {
        let environ_ref = interp.var_get("ENVIRON");
        let mut b = environ_ref.borrow_mut();
        b.is_arr = true;
        for (k, v) in std::env::vars() {
            b.arr.insert(k, value::mkstrnum_owned(v));
        }
    }

    let mut progbuf = String::new();
    let mut have_prog = false;
    let mut preassign: Vec<(String, String)> = Vec::new();
    let mut fsopt: Option<String> = None;
    let mut i = 1usize;

    // Value of an option given either attached (-Fx) or as the next argument (-F x).
    fn opt_value(argv: &[String], i: &mut usize, flag: &str) -> Result<String, String> {
        let a = &argv[*i];
        if a.len() > flag.len() {
            Ok(a[flag.len()..].to_string())
        } else if *i + 1 < argv.len() {
            *i += 1;
            Ok(argv[*i].clone())
        } else {
            Err(format!("option {} requires an argument", flag))
        }
    }

    while i < argv.len() {
        let a = argv[i].clone();
        match a.as_str() {
            "--" => {
                i += 1;
                break;
            }
            "--version" | "-V" => {
                print!("{}", version_text());
                return Ok(0);
            }
            "--help" | "-h" => {
                print!("{}", usage_text());
                return Ok(0);
            }
            _ => {}
        }
        if a.starts_with("-W") {
            let w = opt_value(argv, &mut i, "-W")?;
            let wl = w.to_ascii_lowercase();
            if wl.starts_with('v') {
                print!("{}", version_text());
                return Ok(0);
            } else if wl.starts_with('h') || wl.starts_with('u') {
                print!("{}", usage_text());
                return Ok(0);
            } else {
                eprintln!("mawk: vacuous option: -W \"{}\"", w);
            }
            i += 1;
            continue;
        }
        if a.starts_with("-f") {
            let fname = opt_value(argv, &mut i, "-f")?;
            progbuf.push_str(&read_program_file(&fname)?);
            progbuf.push('\n');
            have_prog = true;
            i += 1;
            continue;
        }
        if a.starts_with("-v") {
            let assign = opt_value(argv, &mut i, "-v")?;
            match assign.find('=') {
                Some(eq) => {
                    let (n, v) = assign.split_at(eq);
                    preassign.push((n.to_string(), v[1..].to_string()));
                }
                None => return Err(format!("improper assignment: -v {}", assign)),
            }
            i += 1;
            continue;
        }
        if a.starts_with("-F") {
            fsopt = Some(opt_value(argv, &mut i, "-F")?);
            i += 1;
            continue;
        }
        if a.len() > 1 && a.starts_with('-') && !have_prog {
            return Err(format!("not an option: {}", a));
        }
        break;
    }
    // Without -f, the first operand is the program text; everything after
    // it is an input file or var=value operand, even if it starts with '-'.
    if !have_prog && i < argv.len() {
        progbuf.push_str(&argv[i]);
        have_prog = true;
        i += 1;
    }
    if !have_prog {
        eprint!("{}", usage_text());
        return Ok(2);
    }
    if let Some(fs) = fsopt {
        // POSIX: -Ft means tab
        let fb = if fs == "t" { "\t".to_string() } else { process_escapes(&fs) };
        interp.setvar_str("FS", &fb);
    }
    for (n, v) in preassign {
        let ok = !n.is_empty()
            && n.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
            && !n.as_bytes()[0].is_ascii_digit();
        if !ok {
            return Err(format!("improper assignment: -v {}={}", n, v));
        }
        interp.setvar_strnum(&n, &process_escapes(&v));
    }

    let program = Parser::new(&progbuf).parse_program();
    interp.load_program(program);

    let argv_ref = interp.var_get("ARGV");
    {
        let mut b = argv_ref.borrow_mut();
        b.is_arr = true;
        b.arr.insert("0".to_string(), value::mkstr("awk"));
    }
    let mut argc_count = 1i64;
    while i < argv.len() {
        let v = value::mkstrnum(&argv[i]);
        {
            let mut b = argv_ref.borrow_mut();
            b.arr.insert(argc_count.to_string(), v);
        }
        argc_count += 1;
        i += 1;
    }
    interp.setvar_num("ARGC", argc_count as f64);

    Ok(interp.run_program())
}
