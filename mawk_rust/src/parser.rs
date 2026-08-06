// ----------------------------------------------------------------------
// Parser: recursive-descent, mirrors mawk.c's parser closely.
// ----------------------------------------------------------------------
use crate::ast::*;
use crate::lexer::{token_is_value_end, Lexer, Token, TokType};
use std::collections::HashMap;

pub struct Parser {
    lex: Lexer,
    cur: Token,
    prev_significant: bool,
    suppress_gt: bool,
    funcs: HashMap<String, FuncDef>,
    rules: Vec<Rule>,
}

impl Parser {
    pub fn new(src: &str) -> Parser {
        let mut lex = Lexer::new(src);
        let first = lex.next(false);
        let prev_significant = token_is_value_end(first.ty);
        Parser {
            lex,
            cur: first,
            prev_significant,
            suppress_gt: false,
            funcs: HashMap::new(),
            rules: Vec::new(),
        }
    }

    fn advance(&mut self) {
        let t = self.lex.next(self.prev_significant);
        self.prev_significant = token_is_value_end(t.ty);
        self.cur = t;
    }

    fn expect(&mut self, t: TokType, what: &str) {
        if self.cur.ty != t {
            eprintln!("mawk: parse error, expected {}", what);
            std::process::exit(2);
        }
        self.advance();
    }

    fn skip_newlines(&mut self) {
        while self.cur.ty == TokType::Newline {
            self.advance();
        }
    }
    fn skip_terms(&mut self) {
        while self.cur.ty == TokType::Newline || self.cur.ty == TokType::Semi {
            self.advance();
        }
    }

    // Snapshot for backtracking: clone the whole lexer (cheap: a Vec<u8> + pos).
    fn save(&self) -> (Lexer, Token, bool) {
        (self.lex.clone(), self.cur.clone(), self.prev_significant)
    }
    fn restore(&mut self, s: (Lexer, Token, bool)) {
        self.lex = s.0;
        self.cur = s.1;
        self.prev_significant = s.2;
    }

    // ---- expression list helpers ----

    fn parse_expr_list(&mut self, endtok: TokType) -> Vec<Node> {
        let mut v = Vec::new();
        self.skip_newlines();
        if self.cur.ty != endtok {
            loop {
                self.skip_newlines();
                v.push(self.parse_ternary());
                self.skip_newlines();
                if self.cur.ty == TokType::Comma {
                    self.advance();
                    continue;
                }
                break;
            }
        }
        v
    }

    fn starts_getline_target(&self) -> bool {
        self.cur.ty == TokType::Dollar || self.cur.ty == TokType::Ident
    }

    fn parse_lvalue_only(&mut self) -> Option<Node> {
        if self.cur.ty == TokType::Dollar {
            self.advance();
            let idx = self.parse_unary_lvalue();
            return Some(Node::Field(Box::new(idx)));
        }
        if self.cur.ty == TokType::Ident {
            let name = self.cur.text.clone();
            self.advance();
            if self.cur.ty == TokType::LBracket {
                self.advance();
                let idxs = self.parse_expr_list(TokType::RBracket);
                self.expect(TokType::RBracket, "]");
                return Some(Node::ArrRef(name, idxs));
            }
            return Some(Node::Var(name));
        }
        None
    }

    fn parse_unary_lvalue(&mut self) -> Node {
        self.parse_primary()
    }

    // ---- primary / expr chain (precedence climbing, mirrors C) ----

    fn parse_primary(&mut self) -> Node {
        if self.cur.ty == TokType::Getline {
            self.advance();
            let target = if self.starts_getline_target() {
                self.parse_lvalue_only()
            } else {
                None
            };
            if self.cur.ty == TokType::Lt {
                self.advance();
                let save = self.suppress_gt;
                self.suppress_gt = false;
                let file = self.parse_unary_lvalue();
                self.suppress_gt = save;
                return Node::Getline(target.map(Box::new), Some(Box::new(file)), 1);
            }
            return Node::Getline(target.map(Box::new), None, 0);
        }
        match self.cur.ty {
            TokType::Num => {
                let n = Node::Num(self.cur.num);
                self.advance();
                return n;
            }
            TokType::Str => {
                let n = Node::Str(self.cur.text.clone());
                self.advance();
                return n;
            }
            TokType::Ere => {
                let n = Node::Ere(self.cur.text.clone());
                self.advance();
                return n;
            }
            TokType::Dollar => {
                self.advance();
                let idx = self.parse_unary_lvalue();
                return Node::Field(Box::new(idx));
            }
            TokType::LParen => {
                self.advance();
                let save = self.suppress_gt;
                self.suppress_gt = false;
                let e = self.parse_expr();
                if self.cur.ty == TokType::Comma {
                    let mut lst = vec![e];
                    while self.cur.ty == TokType::Comma {
                        self.advance();
                        self.skip_newlines();
                        lst.push(self.parse_expr());
                    }
                    self.expect(TokType::RParen, ")");
                    self.suppress_gt = save;
                    self.expect(TokType::In, "in");
                    if self.cur.ty != TokType::Ident {
                        eprintln!("mawk: expected array name after 'in'");
                        std::process::exit(2);
                    }
                    let arrname = self.cur.text.clone();
                    self.advance();
                    return Node::In(lst, arrname);
                }
                self.expect(TokType::RParen, ")");
                self.suppress_gt = save;
                return Node::Group(Box::new(e));
            }
            TokType::Not => {
                self.advance();
                return Node::Not(Box::new(self.parse_primary()));
            }
            TokType::Minus => {
                self.advance();
                return Node::Unary(TokType::Minus, Box::new(self.parse_primary()));
            }
            TokType::Plus => {
                self.advance();
                return Node::Unary(TokType::Plus, Box::new(self.parse_primary()));
            }
            TokType::Incr | TokType::Decr => {
                let op = self.cur.ty;
                self.advance();
                let target = self.parse_primary();
                return Node::PreIncDec(op, Box::new(target));
            }
            TokType::FuncName => {
                let name = self.cur.text.clone();
                self.advance();
                self.expect(TokType::LParen, "(");
                let save = self.suppress_gt;
                self.suppress_gt = false;
                let args = self.parse_expr_list(TokType::RParen);
                self.suppress_gt = save;
                self.expect(TokType::RParen, ")");
                return Node::Call(name, args);
            }
            TokType::Ident => {
                let name = self.cur.text.clone();
                self.advance();
                let n = if self.cur.ty == TokType::LBracket {
                    self.advance();
                    let save = self.suppress_gt;
                    self.suppress_gt = false;
                    let idxs = self.parse_expr_list(TokType::RBracket);
                    self.suppress_gt = save;
                    self.expect(TokType::RBracket, "]");
                    Node::ArrRef(name, idxs)
                } else {
                    Node::Var(name)
                };
                if self.cur.ty == TokType::Incr || self.cur.ty == TokType::Decr {
                    let op = self.cur.ty;
                    self.advance();
                    return Node::PostIncDec(op, Box::new(n));
                }
                return n;
            }
            _ => {
                eprintln!("mawk: parse error near token {:?}", self.cur.ty);
                std::process::exit(2);
            }
        }
    }

    fn parse_pow(&mut self) -> Node {
        let l = self.parse_primary();
        if self.cur.ty == TokType::Caret {
            self.advance();
            let r = self.parse_pow();
            return Node::Pow(Box::new(l), Box::new(r));
        }
        l
    }

    fn parse_mul(&mut self) -> Node {
        let mut l = self.parse_pow();
        loop {
            match self.cur.ty {
                TokType::Star | TokType::Slash | TokType::Percent => {
                    let op = self.cur.ty;
                    self.advance();
                    let r = self.parse_pow();
                    l = Node::BinOp(op, Box::new(l), Box::new(r));
                }
                _ => break,
            }
        }
        l
    }

    fn parse_add(&mut self) -> Node {
        let mut l = self.parse_mul();
        loop {
            match self.cur.ty {
                TokType::Plus | TokType::Minus => {
                    let op = self.cur.ty;
                    self.advance();
                    let r = self.parse_mul();
                    l = Node::BinOp(op, Box::new(l), Box::new(r));
                }
                _ => break,
            }
        }
        l
    }

    fn starts_value(t: TokType) -> bool {
        matches!(
            t,
            TokType::Num | TokType::Str | TokType::Ere | TokType::Ident | TokType::FuncName
                | TokType::Dollar | TokType::LParen | TokType::Not | TokType::Minus | TokType::Plus
                | TokType::Incr | TokType::Decr
        )
    }

    fn parse_concat(&mut self) -> Node {
        let mut l = self.parse_add();
        while Self::starts_value(self.cur.ty) {
            let r = self.parse_add();
            l = Node::Concat(Box::new(l), Box::new(r));
        }
        while self.cur.ty == TokType::Pipe {
            let save = self.save();
            self.advance();
            if self.cur.ty == TokType::Getline {
                self.advance();
                let target = if self.starts_getline_target() {
                    self.parse_lvalue_only()
                } else {
                    None
                };
                l = Node::Getline(target.map(Box::new), Some(Box::new(l)), 2);
            } else {
                self.restore(save);
                break;
            }
        }
        l
    }

    fn parse_rel(&mut self) -> Node {
        let l = self.parse_concat();
        let t = self.cur.ty;
        if t == TokType::Lt
            || t == TokType::Le
            || t == TokType::Ge
            || t == TokType::Eq
            || t == TokType::Ne
            || (t == TokType::Gt && !self.suppress_gt)
        {
            self.advance();
            let r = self.parse_concat();
            return Node::BinOp(t, Box::new(l), Box::new(r));
        } else if t == TokType::Match || t == TokType::NoMatch {
            self.advance();
            let r = self.parse_concat();
            return Node::Match(t, Box::new(l), Box::new(r));
        } else if t == TokType::In {
            self.advance();
            if self.cur.ty != TokType::Ident {
                eprintln!("mawk: expected array name after 'in'");
                std::process::exit(2);
            }
            let arrname = self.cur.text.clone();
            self.advance();
            return Node::In(vec![l], arrname);
        }
        l
    }

    fn parse_and(&mut self) -> Node {
        let mut l = self.parse_rel();
        while self.cur.ty == TokType::And {
            self.advance();
            self.skip_newlines();
            let r = self.parse_rel();
            l = Node::And(Box::new(l), Box::new(r));
        }
        l
    }

    fn parse_or(&mut self) -> Node {
        let mut l = self.parse_and();
        while self.cur.ty == TokType::Or {
            self.advance();
            self.skip_newlines();
            let r = self.parse_and();
            l = Node::Or(Box::new(l), Box::new(r));
        }
        l
    }

    fn parse_ternary(&mut self) -> Node {
        let c = self.parse_or();
        if self.cur.ty == TokType::Question {
            self.advance();
            self.skip_newlines();
            let a = self.parse_ternary();
            self.skip_newlines();
            self.expect(TokType::Colon, ":");
            self.skip_newlines();
            let b = self.parse_ternary();
            return Node::Ternary(Box::new(c), Box::new(a), Box::new(b));
        }
        c
    }

    fn parse_expr(&mut self) -> Node {
        let l = self.parse_ternary();
        let t = self.cur.ty;
        if matches!(
            t,
            TokType::Assign
                | TokType::AddAssign
                | TokType::SubAssign
                | TokType::MulAssign
                | TokType::DivAssign
                | TokType::ModAssign
                | TokType::PowAssign
        ) {
            self.advance();
            let r = self.parse_expr();
            return Node::Assign(t, Box::new(l), Box::new(r));
        }
        l
    }

    // ---- statements ----

    fn parse_simple_or_null(&mut self) -> Option<Stmt> {
        if self.cur.ty == TokType::Semi || self.cur.ty == TokType::RParen {
            return None;
        }
        Some(self.parse_stmt())
    }

    fn parse_block(&mut self) -> Stmt {
        self.expect(TokType::LBrace, "{");
        let mut v = Vec::new();
        self.skip_terms();
        while self.cur.ty != TokType::RBrace && self.cur.ty != TokType::Eof {
            let s = self.parse_stmt_opt();
            if let Some(s) = s {
                v.push(s);
            }
            self.skip_terms();
        }
        self.expect(TokType::RBrace, "}");
        Stmt::Block(v)
    }

    fn parse_print_args(&mut self) -> Vec<Node> {
        let mut v = Vec::new();
        let save = self.suppress_gt;
        self.suppress_gt = true;
        if !matches!(
            self.cur.ty,
            TokType::Semi
                | TokType::Newline
                | TokType::RBrace
                | TokType::Eof
                | TokType::Gt
                | TokType::Append
                | TokType::Pipe
        ) {
            loop {
                v.push(self.parse_ternary());
                if self.cur.ty == TokType::Comma {
                    self.advance();
                    self.skip_newlines();
                    continue;
                }
                break;
            }
        }
        self.suppress_gt = save;
        v
    }

    fn parse_stmt_opt(&mut self) -> Option<Stmt> {
        if self.cur.ty == TokType::Semi {
            self.advance();
            return None;
        }
        Some(self.parse_stmt())
    }

    fn parse_stmt(&mut self) -> Stmt {
        self.skip_newlines();
        match self.cur.ty {
            TokType::LBrace => self.parse_block(),
            TokType::If => {
                self.advance();
                self.expect(TokType::LParen, "(");
                let cond = self.parse_expr();
                self.expect(TokType::RParen, ")");
                self.skip_newlines();
                let then_s = self.parse_stmt();
                let mut else_s = None;
                let save = self.save();
                self.skip_terms();
                if self.cur.ty == TokType::Else {
                    self.advance();
                    self.skip_newlines();
                    else_s = Some(Box::new(self.parse_stmt()));
                } else {
                    self.restore(save);
                }
                Stmt::If(cond, Box::new(then_s), else_s)
            }
            TokType::While => {
                self.advance();
                self.expect(TokType::LParen, "(");
                let cond = self.parse_expr();
                self.expect(TokType::RParen, ")");
                self.skip_newlines();
                let body = self.parse_stmt();
                Stmt::While(cond, Box::new(body))
            }
            TokType::Do => {
                self.advance();
                self.skip_newlines();
                let body = self.parse_stmt();
                self.skip_terms();
                self.expect(TokType::While, "while");
                self.expect(TokType::LParen, "(");
                let cond = self.parse_expr();
                self.expect(TokType::RParen, ")");
                Stmt::DoWhile(Box::new(body), cond)
            }
            TokType::For => {
                self.advance();
                self.expect(TokType::LParen, "(");
                if self.cur.ty == TokType::Ident {
                    let save = self.save();
                    let var = self.cur.text.clone();
                    self.advance();
                    if self.cur.ty == TokType::In {
                        self.advance();
                        if self.cur.ty != TokType::Ident {
                            eprintln!("mawk: expected array name after 'in'");
                            std::process::exit(2);
                        }
                        let arrname = self.cur.text.clone();
                        self.advance();
                        self.expect(TokType::RParen, ")");
                        self.skip_newlines();
                        let body = self.parse_stmt();
                        return Stmt::ForIn(var, arrname, Box::new(body));
                    }
                    self.restore(save);
                }
                let init = self.parse_simple_or_null();
                self.expect(TokType::Semi, ";");
                let cond = if self.cur.ty == TokType::Semi { None } else { Some(self.parse_expr()) };
                self.expect(TokType::Semi, ";");
                let incr = self.parse_simple_or_null();
                self.expect(TokType::RParen, ")");
                self.skip_newlines();
                let body = self.parse_stmt();
                Stmt::For(init.map(Box::new), cond, incr.map(Box::new), Box::new(body))
            }
            TokType::Print => {
                self.advance();
                let args = self.parse_print_args();
                let (mode, target) = self.parse_redir();
                Stmt::Print(args, mode, target)
            }
            TokType::Printf => {
                self.advance();
                let args = self.parse_print_args();
                let (mode, target) = self.parse_redir();
                Stmt::Printf(args, mode, target)
            }
            TokType::Next => {
                self.advance();
                Stmt::Next
            }
            TokType::NextFile => {
                self.advance();
                Stmt::NextFile
            }
            TokType::Break => {
                self.advance();
                Stmt::Break
            }
            TokType::Continue => {
                self.advance();
                Stmt::Continue
            }
            TokType::Return => {
                self.advance();
                let e = if !matches!(self.cur.ty, TokType::Semi | TokType::Newline | TokType::RBrace | TokType::Eof) {
                    Some(self.parse_expr())
                } else {
                    None
                };
                Stmt::Return(e)
            }
            TokType::Exit => {
                self.advance();
                let e = if !matches!(self.cur.ty, TokType::Semi | TokType::Newline | TokType::RBrace | TokType::Eof) {
                    Some(self.parse_expr())
                } else {
                    None
                };
                Stmt::Exit(e)
            }
            TokType::Delete => {
                self.advance();
                if self.cur.ty != TokType::Ident {
                    eprintln!("mawk: expected array name after delete");
                    std::process::exit(2);
                }
                let name = self.cur.text.clone();
                self.advance();
                let mut idxs = Vec::new();
                if self.cur.ty == TokType::LBracket {
                    self.advance();
                    idxs = self.parse_expr_list(TokType::RBracket);
                    self.expect(TokType::RBracket, "]");
                }
                Stmt::Delete(name, idxs)
            }
            TokType::Semi => {
                self.advance();
                Stmt::Block(Vec::new())
            }
            _ => {
                let e = self.parse_expr();
                Stmt::Expr(e)
            }
        }
    }

    fn parse_redir(&mut self) -> (RedirMode, Option<Node>) {
        if self.cur.ty == TokType::Gt {
            self.advance();
            (RedirMode::Trunc, Some(self.parse_concat()))
        } else if self.cur.ty == TokType::Append {
            self.advance();
            (RedirMode::Append, Some(self.parse_concat()))
        } else if self.cur.ty == TokType::Pipe {
            self.advance();
            (RedirMode::Pipe, Some(self.parse_concat()))
        } else {
            (RedirMode::None, None)
        }
    }

    fn parse_function_def(&mut self) {
        self.advance();
        let fname = if self.cur.ty == TokType::FuncName || self.cur.ty == TokType::Ident {
            let n = self.cur.text.clone();
            self.advance();
            n
        } else {
            eprintln!("mawk: expected function name");
            std::process::exit(2);
        };
        self.expect(TokType::LParen, "(");
        let mut params = Vec::new();
        while self.cur.ty == TokType::Ident {
            params.push(self.cur.text.clone());
            self.advance();
            if self.cur.ty == TokType::Comma {
                self.advance();
                self.skip_newlines();
                continue;
            }
            break;
        }
        self.expect(TokType::RParen, ")");
        self.skip_newlines();
        let body = self.parse_block();
        self.funcs.insert(fname.clone(), FuncDef { name: fname, params, body });
    }

    pub fn parse_program(mut self) -> Program {
        self.skip_terms();
        while self.cur.ty != TokType::Eof {
            if self.cur.ty == TokType::Function {
                self.parse_function_def();
                self.skip_terms();
                continue;
            }
            let rule = if self.cur.ty == TokType::Begin {
                self.advance();
                self.skip_newlines();
                Rule { ptype: PType::Begin, action: Some(self.parse_block()), range_active: std::cell::Cell::new(false) }
            } else if self.cur.ty == TokType::End {
                self.advance();
                self.skip_newlines();
                Rule { ptype: PType::End, action: Some(self.parse_block()), range_active: std::cell::Cell::new(false) }
            } else if self.cur.ty == TokType::LBrace {
                Rule { ptype: PType::Always, action: Some(self.parse_block()), range_active: std::cell::Cell::new(false) }
            } else if self.cur.ty == TokType::Ere {
                let pere1 = self.cur.text.clone();
                self.advance();
                if self.cur.ty == TokType::Comma {
                    self.advance();
                    self.skip_newlines();
                    let e1 = Node::Ere(pere1);
                    let e2 = if self.cur.ty == TokType::Ere {
                        let t = self.cur.text.clone();
                        self.advance();
                        Node::Ere(t)
                    } else {
                        self.parse_expr()
                    };
                    self.skip_newlines();
                    let action = if self.cur.ty == TokType::LBrace { Some(self.parse_block()) } else { None };
                    Rule { ptype: PType::Range(e1, e2), action, range_active: std::cell::Cell::new(false) }
                } else {
                    self.skip_newlines();
                    let action = if self.cur.ty == TokType::LBrace { Some(self.parse_block()) } else { None };
                    Rule { ptype: PType::Ere(pere1), action, range_active: std::cell::Cell::new(false) }
                }
            } else {
                let e = self.parse_expr();
                self.skip_newlines();
                if self.cur.ty == TokType::Comma {
                    self.advance();
                    self.skip_newlines();
                    let e2 = self.parse_expr();
                    self.skip_newlines();
                    let action = if self.cur.ty == TokType::LBrace { Some(self.parse_block()) } else { None };
                    Rule { ptype: PType::Range(e, e2), action, range_active: std::cell::Cell::new(false) }
                } else {
                    self.skip_newlines();
                    let action = if self.cur.ty == TokType::LBrace { Some(self.parse_block()) } else { None };
                    Rule { ptype: PType::Expr(e), action, range_active: std::cell::Cell::new(false) }
                }
            };
            self.rules.push(rule);
            self.skip_terms();
        }
        Program { rules: self.rules, funcs: self.funcs }
    }
}
