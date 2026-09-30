// ============================================================================
// mawk_rs -- A feature-rich AWK interpreter, ported from mawk.c to Rust.
//
// This is a faithful, idiomatic-Rust re-implementation of the original C
// program: same language coverage (BEGIN/END, patterns, full expression
// grammar, user functions, a hand-rolled backtracking regex engine, printf,
// getline in all its forms, I/O redirection, etc).
// Version: 1.0.0
// Release  date: 9/27/2026
// ============================================================================

/// Reported by `--version` / `-W version`; keep in sync with the header above.
pub const VERSION: &str = "1.0.0";
pub const RELEASE_DATE: &str = "9/27/2026";

mod regex_engine;
mod value;
mod lexer;
mod ast;
mod parser;
mod interp;

use std::env;
use std::process::ExitCode;

/// The tree-walking evaluator and the backtracking regex matcher both
/// recurse; run on a large stack so deep recursion and long input lines
/// don't overflow the default 8 MB main-thread stack.
const INTERP_STACK_SIZE: usize = 512 * 1024 * 1024;

#[cfg(unix)]
fn restore_sigpipe() {
    // Rust ignores SIGPIPE by default; a C awk dies quietly when the reader
    // of its output goes away (`awk ... | head`). Restore that behaviour.
    unsafe extern "C" {
        fn signal(signum: i32, handler: usize) -> usize;
    }
    const SIGPIPE: i32 = 13;
    const SIG_DFL: usize = 0;
    unsafe {
        signal(SIGPIPE, SIG_DFL);
    }
}

#[cfg(not(unix))]
fn restore_sigpipe() {}

fn main() -> ExitCode {
    restore_sigpipe();
    let args: Vec<String> = env::args_os().map(|a| a.to_string_lossy().into_owned()).collect();
    let result = std::thread::Builder::new()
        .name("mawk".into())
        .stack_size(INTERP_STACK_SIZE)
        .spawn(move || interp::run(&args))
        .expect("failed to start interpreter thread")
        .join();
    match result {
        Ok(Ok(code)) => ExitCode::from((code & 0xFF) as u8),
        Ok(Err(msg)) => {
            eprintln!("mawk: {}", msg);
            ExitCode::from(2)
        }
        Err(_) => ExitCode::from(2), // the panic message was already printed
    }
}
