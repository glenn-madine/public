// ----------------------------------------------------------------------
// Parser: recursive descent.
//
// Expression precedence, lowest to highest (POSIX awk):
//   assignment  ?:  ||  &&  in  ~ !~  relational  concatenation
//   + -  * / %  unary ! + -  ^  ++ --  $  grouping
// ----------------------------------------------------------------------
use crate::ast::*;
use crate::lexer::{builtin_arity, token_is_value_end, Lexer, Token, TokType};
use std::collections::HashMap;

pub struct Parser {
    lex: Lexer,
    cur: Token,
    prev_significant: bool,
    suppress_gt: bool,
    funcs: HashMap<String, FuncDef>,
    rules: Vec<Rule>,
    loop_depth: usize,
    in_function: bool,
    in_begin_end: bool,
    /// user-function calls seen: (name, nargs, line) -- validated after parsing
    calls: Vec<(String, usize, usize)>,
}

type Snapshot = (Lexer, Token, bool);

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
            loop_depth: 0,
            in_function: false,
            in_begin_end: false,
            calls: Vec::new(),
        }
    }

    fn error_at(&self, line: usize, msg: &str) -> ! {
        eprintln!("mawk: line {}: {}", line, msg);
        std::process::exit(2);
    }

    fn error(&self, msg: &str) -> ! {
        self.error_at(self.cur.line, msg)
    }

    fn syntax_error(&self) -> ! {
        let near = match self.cur.ty {
            TokType::Eof => "end of file".to_string(),
            TokType::Newline => "end of line".to_string(),
            TokType::Str => format!("\"{}\"", self.cur.text),
            TokType::Ere => format!("/{}/", self.cur.text),
            TokType::Num => format!("{}", self.cur.num),
            TokType::Ident | TokType::FuncName | TokType::Builtin => self.cur.text.clone(),
            t => tok_text(t).to_string(),
        };
        self.error(&format!("syntax error at or near {}", near))
    }

    fn advance(&mut self) {
        let t = self.lex.next(self.prev_significant);
        self.prev_significant = token_is_value_end(t.ty);
        self.cur = t;
    }

    fn expect(&mut self, t: TokType) {
        if self.cur.ty != t {
            self.syntax_error();
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

    // Snapshot for backtracking: clone the whole lexer (a Vec<u8> + pos).
    fn save(&self) -> Snapshot {
        (self.lex.clone(), self.cur.clone(), self.prev_significant)
    }
    fn restore(&mut self, s: Snapshot) {
        self.lex = s.0;
        self.cur = s.1;
        self.prev_significant = s.2;
    }

    fn is_lvalue(n: &Node) -> bool {
        match n {
            Node::Var(_) | Node::ArrRef(_, _) | Node::Field(_) => true,
            Node::Group(inner) => Self::is_lvalue(inner),
            _ => false,
        }
    }

    // ---- expression list helpers ----

    fn parse_expr_list(&mut self, endtok: TokType) -> Vec<Node> {
        let mut v = Vec::new();
        self.skip_newlines();
        if self.cur.ty != endtok {
            loop {
                self.skip_newlines();
                v.push(self.parse_expr());
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

    fn array_name_after_in(&mut self) -> String {
        if self.cur.ty != TokType::Ident {
            self.error("expected array name after 'in'");
        }
        let n = self.cur.text.clone();
        self.advance();
        n
    }

    fn parse_getline_target(&mut self) -> Option<Node> {
        match self.cur.ty {
            TokType::Dollar => {
                self.advance();
                Some(Node::Field(Box::new(self.parse_dollar_operand())))
            }
            TokType::Ident => {
                let name = self.cur.text.clone();
                self.advance();
                if self.cur.ty == TokType::LBracket {
                    self.advance();
                    let idxs = self.parse_expr_list(TokType::RBracket);
                    self.expect(TokType::RBracket);
                    Some(Node::ArrRef(name, idxs))
                } else {
                    Some(Node::Var(name))
                }
            }
            _ => None,
        }
    }

    // ---- primaries ----

    /// A primary expression with no prefix/postfix operators.
    fn parse_base(&mut self) -> Node {
        match self.cur.ty {
            TokType::Getline => {
                self.advance();
                let target = self.parse_getline_target();
                if self.cur.ty == TokType::Lt {
                    self.advance();
                    let save = self.suppress_gt;
                    self.suppress_gt = false;
                    let file = self.parse_base();
                    self.suppress_gt = save;
                    return Node::Getline(target.map(Box::new), Some(Box::new(file)), 1);
                }
                Node::Getline(target.map(Box::new), None, 0)
            }
            TokType::Num => {
                let n = Node::Num(self.cur.num);
                self.advance();
                n
            }
            TokType::Str => {
                let n = Node::Str(self.cur.text.clone());
                self.advance();
                n
            }
            TokType::Ere => {
                let n = Node::Ere(self.cur.text.clone());
                self.advance();
                n
            }
            TokType::Dollar => {
                self.advance();
                Node::Field(Box::new(self.parse_dollar_operand()))
            }
            TokType::LParen => {
                self.advance();
                let save = self.suppress_gt;
                self.suppress_gt = false;
                self.skip_newlines();
                let e = self.parse_expr();
                self.skip_newlines();
                if self.cur.ty == TokType::Comma {
                    let mut lst = vec![e];
                    while self.cur.ty == TokType::Comma {
                        self.advance();
                        self.skip_newlines();
                        lst.push(self.parse_expr());
                        self.skip_newlines();
                    }
                    self.expect(TokType::RParen);
                    self.suppress_gt = save;
                    if self.cur.ty != TokType::In {
                        // `(a, b)` is only legal as `(a, b) in arr` or as a
                        // print/printf argument list (handled in parse_print_args).
                        self.syntax_error();
                    }
                    self.advance();
                    let arrname = self.array_name_after_in();
                    return Node::In(lst, arrname);
                }
                self.expect(TokType::RParen);
                self.suppress_gt = save;
                Node::Group(Box::new(e))
            }
            TokType::FuncName => {
                let name = self.cur.text.clone();
                let line = self.cur.line;
                self.advance();
                self.expect(TokType::LParen);
                let save = self.suppress_gt;
                self.suppress_gt = false;
                let args = self.parse_expr_list(TokType::RParen);
                self.suppress_gt = save;
                self.expect(TokType::RParen);
                self.calls.push((name.clone(), args.len(), line));
                Node::Call(name, args)
            }
            TokType::Builtin => self.parse_builtin_call(),
            TokType::Ident => {
                let name = self.cur.text.clone();
                self.advance();
                if self.cur.ty == TokType::LBracket {
                    self.advance();
                    let save = self.suppress_gt;
                    self.suppress_gt = false;
                    let idxs = self.parse_expr_list(TokType::RBracket);
                    self.suppress_gt = save;
                    self.expect(TokType::RBracket);
                    Node::ArrRef(name, idxs)
                } else {
                    Node::Var(name)
                }
            }
            _ => self.syntax_error(),
        }
    }

    fn parse_builtin_call(&mut self) -> Node {
        let name = self.cur.text.clone();
        let line = self.cur.line;
        self.advance();
        let args = if self.cur.ty == TokType::LParen {
            self.advance();
            let save = self.suppress_gt;
            self.suppress_gt = false;
            let a = self.parse_expr_list(TokType::RParen);
            self.suppress_gt = save;
            self.expect(TokType::RParen);
            a
        } else if name == "length" {
            Vec::new()
        } else {
            self.syntax_error();
        };
        let (lo, hi) = builtin_arity(&name).unwrap();
        if args.len() < lo || args.len() > hi {
            self.error_at(line, &format!("wrong number of arguments in call to {}", name));
        }
        if name == "split" && !matches!(args[1], Node::Var(_)) {
            self.error_at(line, "split: second argument must be an array name");
        }
        Node::Call(name, args)
    }

    /// Operand of `$`: binds tighter than ++/-- so `$i++` is `($i)++`,
    /// but allows `$++i`, `$-1`, `$$0`, `$NF`, `$(expr)`.
    fn parse_dollar_operand(&mut self) -> Node {
        match self.cur.ty {
            TokType::Incr | TokType::Decr => {
                let op = self.cur.ty;
                self.advance();
                let t = self.parse_base();
                if !Self::is_lvalue(&t) {
                    self.syntax_error();
                }
                Node::PreIncDec(op, Box::new(t))
            }
            TokType::Minus | TokType::Plus => {
                let op = self.cur.ty;
                self.advance();
                Node::Unary(op, Box::new(self.parse_dollar_operand()))
            }
            TokType::Not => {
                self.advance();
                Node::Not(Box::new(self.parse_dollar_operand()))
            }
            _ => self.parse_base(),
        }
    }

    fn parse_postfix(&mut self) -> Node {
        if matches!(self.cur.ty, TokType::Incr | TokType::Decr) {
            let op = self.cur.ty;
            self.advance();
            let t = self.parse_base();
            if !Self::is_lvalue(&t) {
                self.syntax_error();
            }
            return Node::PreIncDec(op, Box::new(t));
        }
        let n = self.parse_base();
        if Self::is_lvalue(&n) && matches!(self.cur.ty, TokType::Incr | TokType::Decr) {
            let op = self.cur.ty;
            self.advance();
            return Node::PostIncDec(op, Box::new(n));
        }
        n
    }

    fn parse_pow(&mut self) -> Node {
        let l = self.parse_postfix();
        if self.cur.ty == TokType::Caret {
            self.advance();
            // right-associative, and the exponent may carry a sign: 2^-1
            let r = self.parse_unary();
            return Node::Pow(Box::new(l), Box::new(r));
        }
        l
    }

    /// Unary ! + - bind looser than ^, so -2^2 == -4.
    fn parse_unary(&mut self) -> Node {
        match self.cur.ty {
            TokType::Not => {
                self.advance();
                Node::Not(Box::new(self.parse_unary()))
            }
            TokType::Minus | TokType::Plus => {
                let op = self.cur.ty;
                self.advance();
                Node::Unary(op, Box::new(self.parse_unary()))
            }
            _ => self.parse_pow(),
        }
    }

    fn parse_mul(&mut self) -> Node {
        let mut l = self.parse_unary();
        while matches!(self.cur.ty, TokType::Star | TokType::Slash | TokType::Percent) {
            let op = self.cur.ty;
            self.advance();
            let r = self.parse_unary();
            l = Node::BinOp(op, Box::new(l), Box::new(r));
        }
        l
    }

    fn parse_add(&mut self) -> Node {
        let mut l = self.parse_mul();
        while matches!(self.cur.ty, TokType::Plus | TokType::Minus) {
            let op = self.cur.ty;
            self.advance();
            let r = self.parse_mul();
            l = Node::BinOp(op, Box::new(l), Box::new(r));
        }
        l
    }

    fn starts_value(t: TokType) -> bool {
        matches!(
            t,
            TokType::Num | TokType::Str | TokType::Ere | TokType::Ident | TokType::FuncName | TokType::Builtin
                | TokType::Dollar | TokType::LParen | TokType::Not | TokType::Minus | TokType::Plus
                | TokType::Incr | TokType::Decr
        )
    }

    fn parse_concat(&mut self) -> Node {
        let mut l = self.parse_add();
        loop {
            if Self::starts_value(self.cur.ty) {
                // `a (b, c) in arr` style backtracking isn't needed: a group
                // with commas not followed by `in` is a syntax error anyway.
                let r = self.parse_add();
                l = Node::Concat(Box::new(l), Box::new(r));
                continue;
            }
            if self.cur.ty == TokType::Pipe {
                let save = self.save();
                self.advance();
                if self.cur.ty == TokType::Getline {
                    self.advance();
                    let target = self.parse_getline_target();
                    l = Node::Getline(target.map(Box::new), Some(Box::new(l)), 2);
                    continue;
                }
                self.restore(save);
            }
            break;
        }
        l
    }

    fn parse_rel(&mut self) -> Node {
        let l = self.parse_concat();
        let t = self.cur.ty;
        if matches!(t, TokType::Lt | TokType::Le | TokType::Ge | TokType::Eq | TokType::Ne)
            || (t == TokType::Gt && !self.suppress_gt)
        {
            self.advance();
            let r = self.parse_concat();
            return Node::BinOp(t, Box::new(l), Box::new(r));
        }
        l
    }

    fn parse_match(&mut self) -> Node {
        let mut l = self.parse_rel();
        while matches!(self.cur.ty, TokType::Match | TokType::NoMatch) {
            let t = self.cur.ty;
            self.advance();
            let r = self.parse_rel();
            l = Node::Match(t, Box::new(l), Box::new(r));
        }
        l
    }

    fn parse_in(&mut self) -> Node {
        let mut l = self.parse_match();
        while self.cur.ty == TokType::In {
            self.advance();
            let arrname = self.array_name_after_in();
            l = Node::In(vec![l], arrname);
        }
        l
    }

    fn parse_and(&mut self) -> Node {
        let mut l = self.parse_in();
        while self.cur.ty == TokType::And {
            self.advance();
            self.skip_newlines();
            let r = self.parse_in();
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
            self.expect(TokType::Colon);
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
            if !Self::is_lvalue(&l) {
                self.syntax_error();
            }
            self.advance();
            self.skip_newlines();
            let r = self.parse_expr();
            return Node::Assign(t, Box::new(l), Box::new(r));
        }
        l
    }

    // ---- statements ----

    fn at_print_end(&self) -> bool {
        matches!(
            self.cur.ty,
            TokType::Semi | TokType::Newline | TokType::RBrace | TokType::Eof | TokType::Gt | TokType::Append | TokType::Pipe
        )
    }

    fn parse_print_args(&mut self) -> Vec<Node> {
        // `print (a, b) > "f"` / `printf("%d\n", x)`: a parenthesised list
        // that makes up the whole argument list.
        if self.cur.ty == TokType::LParen {
            let snap = self.save();
            let save_gt = self.suppress_gt;
            self.advance();
            self.suppress_gt = false;
            let list = self.parse_expr_list(TokType::RParen);
            self.suppress_gt = save_gt;
            if self.cur.ty == TokType::RParen {
                self.advance();
                if self.at_print_end() && !list.is_empty() {
                    return list;
                }
            }
            self.restore(snap);
        }
        let mut v = Vec::new();
        let save = self.suppress_gt;
        self.suppress_gt = true;
        if !self.at_print_end() {
            loop {
                v.push(self.parse_expr());
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

    fn parse_block(&mut self) -> Stmt {
        self.expect(TokType::LBrace);
        let mut v = Vec::new();
        self.skip_terms();
        while self.cur.ty != TokType::RBrace && self.cur.ty != TokType::Eof {
            v.push(self.parse_stmt());
            self.skip_terms();
        }
        self.expect(TokType::RBrace);
        Stmt::Block(v)
    }

    /// A simple statement must be followed by a terminator.
    fn end_simple(&mut self) {
        if !matches!(
            self.cur.ty,
            TokType::Semi | TokType::Newline | TokType::RBrace | TokType::Eof | TokType::Else
        ) {
            self.syntax_error();
        }
        if self.cur.ty == TokType::Semi {
            self.advance();
        }
    }

    fn parse_loop_body(&mut self) -> Stmt {
        self.skip_newlines();
        self.loop_depth += 1;
        let body = if self.cur.ty == TokType::Semi {
            self.advance();
            Stmt::Block(Vec::new())
        } else {
            self.parse_stmt()
        };
        self.loop_depth -= 1;
        body
    }

    fn parse_opt_simple(&mut self, end: TokType) -> Option<Box<Stmt>> {
        if self.cur.ty == end {
            None
        } else {
            Some(Box::new(Stmt::Expr(self.parse_expr())))
        }
    }

    fn parse_stmt(&mut self) -> Stmt {
        self.skip_newlines();
        match self.cur.ty {
            TokType::LBrace => self.parse_block(),
            TokType::If => {
                self.advance();
                self.expect(TokType::LParen);
                let cond = self.parse_expr();
                self.expect(TokType::RParen);
                self.skip_newlines();
                let then_s = if self.cur.ty == TokType::Semi {
                    self.advance();
                    Stmt::Block(Vec::new())
                } else {
                    self.parse_stmt()
                };
                let mut else_s = None;
                let save = self.save();
                self.skip_terms();
                if self.cur.ty == TokType::Else {
                    self.advance();
                    self.skip_newlines();
                    else_s = Some(Box::new(if self.cur.ty == TokType::Semi {
                        self.advance();
                        Stmt::Block(Vec::new())
                    } else {
                        self.parse_stmt()
                    }));
                } else {
                    self.restore(save);
                }
                Stmt::If(cond, Box::new(then_s), else_s)
            }
            TokType::While => {
                self.advance();
                self.expect(TokType::LParen);
                let cond = self.parse_expr();
                self.expect(TokType::RParen);
                let body = self.parse_loop_body();
                Stmt::While(cond, Box::new(body))
            }
            TokType::Do => {
                self.advance();
                let body = self.parse_loop_body();
                self.skip_terms();
                self.expect(TokType::While);
                self.expect(TokType::LParen);
                let cond = self.parse_expr();
                self.expect(TokType::RParen);
                self.end_simple();
                Stmt::DoWhile(Box::new(body), cond)
            }
            TokType::For => {
                self.advance();
                self.expect(TokType::LParen);
                if self.cur.ty == TokType::Ident {
                    let save = self.save();
                    let var = self.cur.text.clone();
                    self.advance();
                    if self.cur.ty == TokType::In {
                        self.advance();
                        let arrname = self.array_name_after_in();
                        if self.cur.ty == TokType::RParen {
                            self.advance();
                            let body = self.parse_loop_body();
                            return Stmt::ForIn(var, arrname, Box::new(body));
                        }
                    }
                    self.restore(save);
                }
                let init = self.parse_opt_simple(TokType::Semi);
                self.expect(TokType::Semi);
                self.skip_newlines();
                let cond = if self.cur.ty == TokType::Semi { None } else { Some(self.parse_expr()) };
                self.expect(TokType::Semi);
                self.skip_newlines();
                let incr = self.parse_opt_simple(TokType::RParen);
                self.expect(TokType::RParen);
                let body = self.parse_loop_body();
                Stmt::For(init, cond, incr, Box::new(body))
            }
            TokType::Print | TokType::Printf => {
                let is_printf = self.cur.ty == TokType::Printf;
                let line = self.cur.line;
                self.advance();
                let args = self.parse_print_args();
                if is_printf && args.is_empty() {
                    self.error_at(line, "printf: no format");
                }
                let (mode, target) = self.parse_redir();
                self.end_simple();
                if is_printf {
                    Stmt::Printf(args, mode, target)
                } else {
                    Stmt::Print(args, mode, target)
                }
            }
            TokType::Next | TokType::NextFile => {
                let is_next = self.cur.ty == TokType::Next;
                if self.in_begin_end {
                    self.error(if is_next { "improper use of next" } else { "improper use of nextfile" });
                }
                self.advance();
                self.end_simple();
                if is_next { Stmt::Next } else { Stmt::NextFile }
            }
            TokType::Break | TokType::Continue => {
                let is_break = self.cur.ty == TokType::Break;
                if self.loop_depth == 0 {
                    self.error(if is_break {
                        "break statement outside of loop"
                    } else {
                        "continue statement outside of loop"
                    });
                }
                self.advance();
                self.end_simple();
                if is_break { Stmt::Break } else { Stmt::Continue }
            }
            TokType::Return => {
                if !self.in_function {
                    self.error("return outside function body");
                }
                self.advance();
                let e = if !matches!(self.cur.ty, TokType::Semi | TokType::Newline | TokType::RBrace | TokType::Eof) {
                    Some(self.parse_expr())
                } else {
                    None
                };
                self.end_simple();
                Stmt::Return(e)
            }
            TokType::Exit => {
                self.advance();
                let e = if !matches!(self.cur.ty, TokType::Semi | TokType::Newline | TokType::RBrace | TokType::Eof) {
                    Some(self.parse_expr())
                } else {
                    None
                };
                self.end_simple();
                Stmt::Exit(e)
            }
            TokType::Delete => {
                self.advance();
                if self.cur.ty != TokType::Ident {
                    self.error("expected array name after delete");
                }
                let name = self.cur.text.clone();
                self.advance();
                let mut idxs = Vec::new();
                if self.cur.ty == TokType::LBracket {
                    self.advance();
                    idxs = self.parse_expr_list(TokType::RBracket);
                    self.expect(TokType::RBracket);
                }
                self.end_simple();
                Stmt::Delete(name, idxs)
            }
            TokType::Semi => {
                self.advance();
                Stmt::Block(Vec::new())
            }
            _ => {
                let e = self.parse_expr();
                self.end_simple();
                Stmt::Expr(e)
            }
        }
    }

    fn parse_redir(&mut self) -> (RedirMode, Option<Node>) {
        let mode = match self.cur.ty {
            TokType::Gt => RedirMode::Trunc,
            TokType::Append => RedirMode::Append,
            TokType::Pipe => RedirMode::Pipe,
            _ => return (RedirMode::None, None),
        };
        self.advance();
        (mode, Some(self.parse_concat()))
    }

    fn parse_function_def(&mut self) {
        let line = self.cur.line;
        self.advance();
        let fname = match self.cur.ty {
            TokType::FuncName | TokType::Ident => {
                let n = self.cur.text.clone();
                self.advance();
                n
            }
            TokType::Builtin => self.error(&format!("cannot redefine builtin function {}", self.cur.text)),
            _ => self.error("expected function name"),
        };
        if self.funcs.contains_key(&fname) {
            self.error_at(line, &format!("function {} redefined", fname));
        }
        self.expect(TokType::LParen);
        let mut params: Vec<String> = Vec::new();
        self.skip_newlines();
        while self.cur.ty == TokType::Ident {
            let p = self.cur.text.clone();
            if p == fname {
                self.error(&format!("function {}: parameter shadows function name", fname));
            }
            if params.contains(&p) {
                self.error(&format!("function {}: duplicate parameter {}", fname, p));
            }
            params.push(p);
            self.advance();
            self.skip_newlines();
            if self.cur.ty == TokType::Comma {
                self.advance();
                self.skip_newlines();
                continue;
            }
            break;
        }
        self.expect(TokType::RParen);
        self.skip_newlines();
        self.in_function = true;
        let body = self.parse_block();
        self.in_function = false;
        self.funcs.insert(fname.clone(), FuncDef { name: fname, params, body });
    }

    fn new_rule(ptype: PType, action: Option<Stmt>) -> Rule {
        Rule { ptype, action, range_active: std::cell::Cell::new(false) }
    }

    pub fn parse_program(mut self) -> Program {
        self.skip_terms();
        while self.cur.ty != TokType::Eof {
            if self.cur.ty == TokType::Function {
                self.parse_function_def();
                self.skip_terms();
                continue;
            }
            let rule = match self.cur.ty {
                TokType::Begin | TokType::End => {
                    let is_begin = self.cur.ty == TokType::Begin;
                    self.advance();
                    self.skip_newlines();
                    self.in_begin_end = true;
                    let action = self.parse_block();
                    self.in_begin_end = false;
                    Self::new_rule(if is_begin { PType::Begin } else { PType::End }, Some(action))
                }
                TokType::LBrace => {
                    let action = self.parse_block();
                    Self::new_rule(PType::Always, Some(action))
                }
                _ => {
                    // The action's '{' must be on the same line as the pattern;
                    // `pattern\n{...}` is two separate rules.
                    let e = self.parse_expr();
                    let ptype = if self.cur.ty == TokType::Comma {
                        self.advance();
                        self.skip_newlines();
                        let e2 = self.parse_expr();
                        PType::Range(e, e2)
                    } else {
                        PType::Expr(e)
                    };
                    let action = if self.cur.ty == TokType::LBrace { Some(self.parse_block()) } else { None };
                    if action.is_none()
                        && !matches!(self.cur.ty, TokType::Newline | TokType::Semi | TokType::Eof)
                    {
                        self.syntax_error();
                    }
                    Self::new_rule(ptype, action)
                }
            };
            self.rules.push(rule);
            self.skip_terms();
        }
        for (name, nargs, line) in &self.calls {
            match self.funcs.get(name) {
                None => self.error_at(*line, &format!("function {} never defined", name)),
                Some(f) if *nargs > f.params.len() => {
                    self.error_at(*line, &format!("too many arguments in call to {}", name))
                }
                _ => {}
            }
        }
        Program { rules: self.rules, funcs: self.funcs }
    }
}

fn tok_text(t: TokType) -> &'static str {
    use TokType::*;
    match t {
        Begin => "BEGIN", End => "END", If => "if", Else => "else", While => "while", Do => "do",
        For => "for", Print => "print", Printf => "printf", Next => "next", NextFile => "nextfile",
        Exit => "exit", Break => "break", Continue => "continue", Delete => "delete", In => "in",
        Function => "function", Getline => "getline", Return => "return",
        LBrace => "{", RBrace => "}", LParen => "(", RParen => ")", LBracket => "[", RBracket => "]",
        Semi => ";", Comma => ",", Dollar => "$", Assign => "=", AddAssign => "+=", SubAssign => "-=",
        MulAssign => "*=", DivAssign => "/=", ModAssign => "%=", PowAssign => "^=", Or => "||",
        And => "&&", Not => "!", Lt => "<", Le => "<=", Gt => ">", Ge => ">=", Eq => "==", Ne => "!=",
        Match => "~", NoMatch => "!~", Plus => "+", Minus => "-", Star => "*", Slash => "/",
        Percent => "%", Caret => "^", Incr => "++", Decr => "--", Question => "?", Colon => ":",
        Pipe => "|", Append => ">>",
        _ => "?",
    }
}
