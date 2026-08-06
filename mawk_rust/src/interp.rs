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
use std::io::{BufRead, Write};
use std::process::{Child, Command, Stdio};
use std::rc::Rc;

pub struct VarSlot {
    pub val: Value,
    pub is_arr: bool,
    pub arr: HashMap<String, Value>,
}
pub type VarRef = Rc<RefCell<VarSlot>>;

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
    main_input: MainInput,
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
            retval: value::mknum(0.0),
            rng: Rng::new(1),
            main_input: MainInput::new(),
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

    fn die(&self, msg: &str) -> ! {
        eprintln!("mawk: {}", msg);
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
        let slot = new_slot(value::mkstr(""));
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
        if iu > 1_000_000 {
            self.die("field index too large");
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
        let fs = if fs.is_empty() { " ".to_string() } else { fs };
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
                    None => value::mkstr(""),
                    Some(vref) => {
                        let b = vref.borrow();
                        match &b.val.str {
                            Some(s) => {
                                if b.val.is_strnum { value::mkstrnum(s) } else { value::mkstr(s) }
                            }
                            None => value::mknum(b.val.num),
                        }
                    }
                }
            }
            Node::ArrRef(name, idxs) => {
                let key = self.build_key(idxs);
                let vref = self.var_get(name);
                let mut b = vref.borrow_mut();
                b.is_arr = true;
                let entry = b.arr.entry(key).or_insert_with(|| value::mkstr(""));
                match &entry.str {
                    Some(s) => value::mkstrnum(s),
                    None => value::mknum(entry.num),
                }
            }
            Node::Field(idxn) => {
                let iv = self.eval(idxn);
                let idx = value::to_num(&iv) as i64;
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
            Node::PreIncDec(op, target) => {
                let cur = self.eval_lvalue_current(target);
                let mut d = value::to_num(&cur);
                d += if *op == TokType::Incr { 1.0 } else { -1.0 };
                self.assign_to(target, value::mknum(d));
                value::mknum(d)
            }
            Node::PostIncDec(op, target) => {
                let cur = self.eval_lvalue_current(target);
                let d = value::to_num(&cur);
                let nd = d + if *op == TokType::Incr { 1.0 } else { -1.0 };
                self.assign_to(target, value::mknum(nd));
                value::mknum(d)
            }
            Node::Call(name, args) => self.eval_call(name, args),
        }
    }

    fn eval_lvalue_current(&mut self, target: &Node) -> Value {
        if let Node::Group(inner) = target {
            self.eval_lvalue_current(inner)
        } else {
            self.eval(target)
        }
    }

    fn assign_to(&mut self, target: &Node, v: Value) {
        match target {
            Node::Var(name) => {
                if name == "NF" {
                    let newnf_f = value::to_num(&v);
                    let mut newnf = newnf_f as i64;
                    if newnf < 0 {
                        newnf = 0;
                    }
                    let newnf = newnf as usize;
                    if newnf < self.nf {
                        self.nf = newnf;
                    } else if newnf > self.nf {
                        if self.fields.len() <= newnf {
                            self.fields.resize(newnf + 1, String::new());
                        }
                        self.nf = newnf;
                    }
                    self.setvar_num("NF", self.nf as f64);
                    self.rebuild_record();
                    return;
                }
                let vref = self.var_get(name);
                vref.borrow_mut().val = v;
            }
            Node::Field(idxn) => {
                let iv = self.eval(idxn);
                let idx = value::to_num(&iv) as i64;
                let fmt = self.convfmt();
                let s = value::to_str(&v, &fmt);
                self.set_field(idx, &s);
            }
            Node::ArrRef(name, idxs) => {
                let key = self.build_key(idxs);
                let vref = self.var_get(name);
                let mut b = vref.borrow_mut();
                b.is_arr = true;
                b.arr.insert(key, v);
            }
            Node::Group(inner) => self.assign_to(inner, v),
            _ => self.die("invalid assignment target"),
        }
    }

    fn eval_assign(&mut self, op: TokType, target: &Node, rhs: &Node) -> Value {
        if op == TokType::Assign {
            let v = self.eval(rhs);
            let stored = v.clone();
            self.assign_to(target, v);
            stored
        } else {
            let cur = self.eval_lvalue_current(target);
            let c = value::to_num(&cur);
            let rv = self.eval(rhs);
            let r = value::to_num(&rv);
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
            self.assign_to(target, value::mknum(res));
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
        let fd = self.funcs.get(name).cloned().unwrap();
        let nparams = fd.params.len();
        let mut pvars: Vec<VarRef> = Vec::with_capacity(nparams);
        for i in 0..nparams {
            if i < arg_nodes.len() {
                if let Node::Var(vname) = &arg_nodes[i] {
                    let vref = self.var_get(vname);
                    pvars.push(vref);
                } else {
                    let v = self.eval(&arg_nodes[i]);
                    pvars.push(new_slot(v));
                }
            } else {
                pvars.push(new_slot(value::mkstr("")));
            }
        }
        let mut frame_map = HashMap::new();
        for (pname, vref) in fd.params.iter().zip(pvars.into_iter()) {
            frame_map.insert(pname.clone(), vref);
        }
        self.frames.push(frame_map);
        self.retval = value::mknum(0.0);
        self.exec_stmt(&fd.body);
        let ret = self.retval.clone();
        if self.flow == Flow::Return {
            self.flow = Flow::None;
        }
        self.frames.pop();
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
                    None => value::mknum(0.0),
                    Some(r) => {
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
                    None => value::mknum(0.0),
                    Some(r) => {
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
            match Command::new("sh").arg("-c").arg(key).stdout(Stdio::piped()).spawn() {
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
            match Command::new("sh").arg("-c").arg(key).stdin(Stdio::piped()).spawn() {
                Ok(mut child) => {
                    let stdin = child.stdin.take().unwrap();
                    self.outputs.insert(key.to_string(), OutputEnt { writer: Box::new(stdin), child: Some(child) });
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
                    self.outputs.insert(key.to_string(), OutputEnt { writer: Box::new(file), child: None });
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
            let OutputEnt { writer, child } = ent;
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
        let _ = std::io::stdout().flush();
        for (_, ent) in self.outputs.iter_mut() {
            let _ = ent.writer.flush();
        }
    }

    fn read_from_input_key(&mut self, key: &str) -> Option<String> {
        let rs = self.getvar_str("RS");
        let mut leftover = {
            let ent = self.inputs.get_mut(key)?;
            std::mem::take(&mut ent.leftover)
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
                        self.setvar_str(name, val);
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
                    Err(_) => {
                        eprintln!("mawk: cannot open {}", arg);
                        self.main_input.argv_pos += 1;
                        continue;
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
                self.retval = if let Some(e) = e { self.eval(e) } else { value::mknum(0.0) };
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
                let _ = std::io::stdout().write_all(text.as_bytes());
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
                if !self.io_open_output(&key, iomode) {
                    eprintln!("mawk: cannot open '{}' for output", key);
                    std::process::exit(2);
                }
                if let Some(ent) = self.outputs.get_mut(&key) {
                    let _ = ent.writer.write_all(text.as_bytes());
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
                    value::to_num(&v) as u64
                } else {
                    std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .map(|d| d.as_secs())
                        .unwrap_or(0)
                };
                self.rng = Rng::new(seed);
                value::mknum(0.0)
            }
            "system" => {
                let v = self.eval(&args[0]);
                let fmt = self.convfmt();
                let cmd = value::to_str(&v, &fmt);
                self.flush_all();
                let status = Command::new("sh").arg("-c").arg(&cmd).status();
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
        let len = bytes.len() as i64;
        let m0 = if args.len() > 1 {
            let mv = self.eval(&args[1]);
            value::to_num(&mv)
        } else {
            1.0
        };
        let mut m = m0 as i64;
        let mut n: i64 = if args.len() > 2 {
            let nv = self.eval(&args[2]);
            value::to_num(&nv) as i64
        } else {
            len - m + 1 + 1_000_000
        };
        if m < 1 {
            n += m - 1;
            m = 1;
        }
        if m > len {
            return value::mkstr("");
        }
        if n < 0 {
            n = 0;
        }
        if m - 1 + n > len {
            n = len - (m - 1);
        }
        let start = (m - 1).max(0) as usize;
        let start = start.min(bytes.len());
        let end = (start as i64 + n.max(0)) as usize;
        let end = end.min(bytes.len()).max(start);
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
            let f = self.getvar_str("FS");
            if f.is_empty() { " ".to_string() } else { f }
        };
        let fs = if fs.is_empty() { " ".to_string() } else { fs };
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
        let target = args.get(2);
        let cur = match target {
            Some(t) => {
                let v = self.eval_lvalue_current(t);
                value::to_str(&v, &fmt)
            }
            None => self.record.clone(),
        };
        let cur_bytes = cur.into_bytes();
        let repl_bytes = repl.into_bytes();
        let mut out: Vec<u8> = Vec::new();
        let mut pos = 0usize;
        let mut nsubs = 0i64;
        loop {
            let remaining = std::str::from_utf8(&cur_bytes[pos..]).unwrap_or("");
            match regex_engine::regex_search(&pat, remaining) {
                Some((ms, ml)) => {
                    out.extend_from_slice(&cur_bytes[pos..pos + ms]);
                    let mstart = pos + ms;
                    let mend = pos + ms + ml;
                    let mut i = 0usize;
                    while i < repl_bytes.len() {
                        if repl_bytes[i] == b'&' {
                            out.extend_from_slice(&cur_bytes[mstart..mend]);
                            i += 1;
                        } else if repl_bytes[i] == b'\\' && i + 1 < repl_bytes.len() && repl_bytes[i + 1] == b'&' {
                            out.push(b'&');
                            i += 2;
                        } else {
                            out.push(repl_bytes[i]);
                            i += 1;
                        }
                    }
                    nsubs += 1;
                    if ml == 0 {
                        if mend < cur_bytes.len() {
                            out.push(cur_bytes[mend]);
                        }
                        pos = mend + 1;
                    } else {
                        pos = mend;
                    }
                    if !is_global {
                        if pos < cur_bytes.len() {
                            out.extend_from_slice(&cur_bytes[pos..]);
                        }
                        break;
                    }
                    if pos >= cur_bytes.len() {
                        break;
                    }
                }
                None => {
                    out.extend_from_slice(&cur_bytes[pos..]);
                    break;
                }
            }
        }
        if nsubs > 0 {
            let outs = String::from_utf8_lossy(&out).into_owned();
            match target {
                Some(t) => self.assign_to(t, value::mkstring(outs)),
                None => self.set_record(&outs),
            }
        }
        value::mknum(nsubs as f64)
    }

    // ---- sprintf / printf ----

    fn do_sprintf(&mut self, fmt: &str, args: &[Node]) -> String {
        let fb = fmt.as_bytes();
        let mut out = String::new();
        let mut ai = 0usize;
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
            let conv = fb[i] as char;
            i += 1;
            match conv {
                'd' | 'i' | 'o' | 'x' | 'X' | 'u' => {
                    let v = if ai < args.len() {
                        let r = self.eval(&args[ai]);
                        ai += 1;
                        r
                    } else {
                        value::mknum(0.0)
                    };
                    out.push_str(&fmt_int(&flags, width, precision, conv, &v));
                }
                'f' | 'F' | 'e' | 'E' | 'g' | 'G' => {
                    let v = if ai < args.len() {
                        let r = self.eval(&args[ai]);
                        ai += 1;
                        r
                    } else {
                        value::mknum(0.0)
                    };
                    out.push_str(&fmt_float(&flags, width, precision, conv, &v));
                }
                's' => {
                    let v = if ai < args.len() {
                        let r = self.eval(&args[ai]);
                        ai += 1;
                        r
                    } else {
                        value::mkstr("")
                    };
                    let fmtc = self.convfmt();
                    let s = value::to_str(&v, &fmtc);
                    out.push_str(&fmt_str(&flags, width, precision, &s));
                }
                'c' => {
                    let v = if ai < args.len() {
                        let r = self.eval(&args[ai]);
                        ai += 1;
                        r
                    } else {
                        value::mkstr("")
                    };
                    out.push_str(&fmt_char(&flags, width, &v));
                }
                _ => {
                    out.push_str(std::str::from_utf8(&fb[spec_start..i]).unwrap_or(""));
                }
            }
        }
        out
    }
}

// ---- free helper functions ----

fn awk_split(s: &str, fs: &str) -> Vec<String> {
    let bytes = s.as_bytes();
    let mut result = Vec::new();
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
        let mut pos = 0usize;
        loop {
            let remaining = std::str::from_utf8(&bytes[pos..]).unwrap_or("");
            match regex_engine::regex_search(fs, remaining) {
                Some((ms, ml)) if ml > 0 => {
                    result.push(String::from_utf8_lossy(&bytes[pos..pos + ms]).into_owned());
                    pos += ms + ml;
                }
                _ => {
                    result.push(String::from_utf8_lossy(&bytes[pos..]).into_owned());
                    break;
                }
            }
        }
    }
    result
}

fn read_line_raw(reader: &mut dyn BufRead) -> Option<String> {
    let mut buf: Vec<u8> = Vec::new();
    match reader.read_until(b'\n', &mut buf) {
        Ok(0) => None,
        Ok(_) => {
            if buf.last() == Some(&b'\n') {
                buf.pop();
            }
            if buf.last() == Some(&b'\r') {
                buf.pop();
            }
            Some(String::from_utf8_lossy(&buf).into_owned())
        }
        Err(_) => None,
    }
}

fn read_record_generic(reader: &mut dyn BufRead, leftover: &mut String, rs: &str) -> Option<String> {
    if rs.is_empty() {
        let mut line;
        loop {
            line = read_line_raw(reader)?;
            if !line.is_empty() {
                break;
            }
        }
        let mut buf = String::new();
        buf.push_str(&line);
        loop {
            match read_line_raw(reader) {
                None => break,
                Some(l) => {
                    if l.is_empty() {
                        break;
                    }
                    buf.push('\n');
                    buf.push_str(&l);
                }
            }
        }
        return Some(buf);
    }
    if rs.chars().count() == 1 {
        let c = rs.chars().next().unwrap();
        if c == '\n' {
            return read_line_raw(reader);
        }
        let cbyte = c as u8;
        let mut buf: Vec<u8> = Vec::new();
        let mut any = false;
        loop {
            let mut one = [0u8; 1];
            match reader.read(&mut one) {
                Ok(0) => break,
                Ok(_) => {
                    any = true;
                    if one[0] == cbyte {
                        break;
                    }
                    buf.push(one[0]);
                }
                Err(_) => break,
            }
        }
        if !any {
            return None;
        }
        return Some(String::from_utf8_lossy(&buf).into_owned());
    }
    // multi-char RS: regex-based, using a leftover buffer
    loop {
        if !leftover.is_empty() {
            if let Some((ms, ml)) = regex_engine::regex_search(rs, leftover) {
                if ml > 0 {
                    let rec = leftover[..ms].to_string();
                    let rest = leftover[ms + ml..].to_string();
                    *leftover = rest;
                    return Some(rec);
                }
            }
        }
        let mut chunk = [0u8; 4096];
        let got = match reader.read(&mut chunk) {
            Ok(n) => n,
            Err(_) => 0,
        };
        if got == 0 {
            if !leftover.is_empty() {
                let rec = leftover.clone();
                leftover.clear();
                return Some(rec);
            }
            return None;
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

fn fmt_char(flags: &str, width: Option<i64>, v: &Value) -> String {
    let left = flags.contains('-');
    let ch = match &v.str {
        Some(s) => s.chars().next().unwrap_or('\0'),
        None => {
            let n = v.num as i64;
            char::from_u32(n as u32).unwrap_or('\0')
        }
    };
    let s = if ch == '\0' { String::new() } else { ch.to_string() };
    pad(s, width, left, false, false)
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
            'c' => out.push_str(&fmt_char(&flags, width, &v)),
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
            PType::Ere(pat) => {
                let rec = self.record.clone();
                regex_engine::regex_match_bool(pat, &rec)
            }
            PType::Expr(e) => {
                let v = self.eval(e);
                value::truthy(&v)
            }
            PType::Range(e1, e2) => {
                if !r.range_active.get() {
                    let t1 = match e1 {
                        Node::Ere(p) => {
                            let rec = self.record.clone();
                            regex_engine::regex_match_bool(p, &rec)
                        }
                        _ => {
                            let v = self.eval(e1);
                            value::truthy(&v)
                        }
                    };
                    if !t1 {
                        return false;
                    }
                    r.range_active.set(true);
                }
                let t2 = match e2 {
                    Node::Ere(p) => {
                        let rec = self.record.clone();
                        regex_engine::regex_match_bool(p, &rec)
                    }
                    _ => {
                        let v = self.eval(e2);
                        value::truthy(&v)
                    }
                };
                if t2 {
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

/// Top-level entry point, equivalent to mawk.c's main().
pub fn run(argv: &[String]) -> Result<i32, String> {
    let mut interp = Interp::new();

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
    while i < argv.len() {
        let a = argv[i].clone();
        if a == "--" {
            i += 1;
            break;
        }
        if a.starts_with("-f") {
            let fname = if a.len() > 2 {
                a[2..].to_string()
            } else if i + 1 < argv.len() {
                i += 1;
                argv[i].clone()
            } else {
                break;
            };
            let content = std::fs::read_to_string(&fname).map_err(|_| "cannot open program file".to_string())?;
            progbuf.push_str(&content);
            progbuf.push('\n');
            have_prog = true;
            i += 1;
            continue;
        }
        if a.starts_with("-v") {
            let assign = if a.len() > 2 {
                a[2..].to_string()
            } else if i + 1 < argv.len() {
                i += 1;
                argv[i].clone()
            } else {
                break;
            };
            if let Some(eq) = assign.find('=') {
                let (n, v) = assign.split_at(eq);
                preassign.push((n.to_string(), v[1..].to_string()));
            }
            i += 1;
            continue;
        }
        if a.starts_with("-F") {
            fsopt = Some(if a.len() > 2 {
                a[2..].to_string()
            } else if i + 1 < argv.len() {
                i += 1;
                argv[i].clone()
            } else {
                break;
            });
            i += 1;
            continue;
        }
        if !have_prog && !a.starts_with('-') {
            progbuf.push_str(&a);
            have_prog = true;
            i += 1;
            continue;
        }
        break;
    }
    if !have_prog {
        eprintln!(
            "usage: mawk [-F fs] [-v var=val] 'program' [file|var=val ...]\n       mawk [-F fs] [-v var=val] -f prog.awk [-f prog2.awk ...] [file|var=val ...]"
        );
        return Ok(2);
    }
    if let Some(fs) = fsopt {
        let mut fb = String::new();
        let mut chars = fs.chars().peekable();
        while let Some(c) = chars.next() {
            if c == '\\' {
                if let Some(&nc) = chars.peek() {
                    chars.next();
                    match nc {
                        't' => fb.push('\t'),
                        'n' => fb.push('\n'),
                        _ => fb.push(nc),
                    }
                    continue;
                }
            }
            fb.push(c);
        }
        interp.setvar_str("FS", &fb);
    }
    for (n, v) in preassign {
        interp.setvar_str(&n, &v);
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
