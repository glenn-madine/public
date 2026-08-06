// ----------------------------------------------------------------------
// AST
// ----------------------------------------------------------------------
use crate::lexer::TokType;
use std::cell::Cell;

#[derive(Debug)]
pub enum Node {
    Num(f64),
    Str(String),
    Ere(String),
    Var(String),
    ArrRef(String, Vec<Node>),
    Field(Box<Node>),
    Assign(TokType, Box<Node>, Box<Node>),
    BinOp(TokType, Box<Node>, Box<Node>),
    Pow(Box<Node>, Box<Node>),
    Concat(Box<Node>, Box<Node>),
    And(Box<Node>, Box<Node>),
    Or(Box<Node>, Box<Node>),
    Not(Box<Node>),
    Unary(TokType, Box<Node>),
    PreIncDec(TokType, Box<Node>),
    PostIncDec(TokType, Box<Node>),
    Call(String, Vec<Node>),
    Match(TokType, Box<Node>, Box<Node>),
    Group(Box<Node>),
    Ternary(Box<Node>, Box<Node>, Box<Node>),
    In(Vec<Node>, String),
    /// getline target, source(file/cmd), mode: 0 plain, 1 <file, 2 cmd|
    Getline(Option<Box<Node>>, Option<Box<Node>>, u8),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RedirMode {
    None,
    Trunc,
    Append,
    Pipe,
}

#[derive(Debug)]
pub enum Stmt {
    Expr(Node),
    Print(Vec<Node>, RedirMode, Option<Node>),
    Printf(Vec<Node>, RedirMode, Option<Node>),
    If(Node, Box<Stmt>, Option<Box<Stmt>>),
    While(Node, Box<Stmt>),
    DoWhile(Box<Stmt>, Node),
    For(Option<Box<Stmt>>, Option<Node>, Option<Box<Stmt>>, Box<Stmt>),
    ForIn(String, String, Box<Stmt>),
    Block(Vec<Stmt>),
    Next,
    NextFile,
    Exit(Option<Node>),
    Break,
    Continue,
    Delete(String, Vec<Node>),
    Return(Option<Node>),
}

#[derive(Debug)]
pub enum PType {
    Begin,
    End,
    Always,
    Expr(Node),
    Ere(String),
    Range(Node, Node),
}

pub struct Rule {
    pub ptype: PType,
    pub action: Option<Stmt>,
    pub range_active: Cell<bool>,
}

pub struct FuncDef {
    pub name: String,
    pub params: Vec<String>,
    pub body: Stmt,
}

pub struct Program {
    pub rules: Vec<Rule>,
    pub funcs: std::collections::HashMap<String, FuncDef>,
}
