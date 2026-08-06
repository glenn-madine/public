// ============================================================================
// mawk_rs -- A feature-rich AWK interpreter, ported from mawk.c to Rust.
//
// This is a faithful, idiomatic-Rust re-implementation of the original C
// program: same language coverage (BEGIN/END, patterns, full expression
// grammar, user functions, a hand-rolled backtracking regex engine, printf,
// getline in all its forms, I/O redirection, etc).
// ============================================================================

mod regex_engine;
mod value;
mod lexer;
mod ast;
mod parser;
mod interp;

use std::env;
use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    match interp::run(&args) {
        Ok(code) => ExitCode::from(code as u8),
        Err(msg) => {
            eprintln!("mawk: {}", msg);
            ExitCode::from(2)
        }
    }
}
