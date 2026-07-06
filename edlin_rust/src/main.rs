// edlin.rs - A simple MS-DOS EDLIN clone (Rust version)
// Compile with: rustc -O -o edlin edlin.rs

use std::env;
use std::fs::File;
use std::io::{self, BufRead, BufWriter, Write};
use std::process::exit;
use std::time::{SystemTime, UNIX_EPOCH};

const MAX_LINE_LEN: usize = 4096;

struct Editor {
    lines: Vec<String>,
    current_file: String,
    current_line: i64, // 1-based; 0 means none
    #[allow(dead_code)]
    modified: bool,
}

struct ParsedCmd {
    start: i64,
    end: i64,
    third: i64,
    cmd: char,
    rest: String,
}

fn imin(a: i64, b: i64) -> i64 {
    if a < b {
        a
    } else {
        b
    }
}
fn imax(a: i64, b: i64) -> i64 {
    if a > b {
        a
    } else {
        b
    }
}

fn strip_trailing_cr(s: &mut String) {
    if s.ends_with('\n') {
        s.pop();
    }
    if s.ends_with('\r') {
        s.pop();
    }
}

/// Get a line from stdin, returns None on EOF. Strips trailing newline / CR.
fn get_line_stdin(reader: &mut impl BufRead) -> Option<String> {
    let mut buf = String::new();
    let n = reader.read_line(&mut buf).unwrap_or(0);
    if n == 0 {
        return None;
    }
    if buf.ends_with('\n') {
        buf.pop();
    }
    if buf.ends_with('\r') {
        buf.pop();
    }
    Some(buf)
}

impl Editor {
    fn new() -> Self {
        Editor {
            lines: Vec::new(),
            current_file: String::new(),
            current_line: 0,
            modified: false,
        }
    }

    fn line_count(&self) -> i64 {
        self.lines.len() as i64
    }

    /// Insert a single line (0-based index pos)
    fn lines_insert(&mut self, pos: usize, text: &str) {
        self.lines.insert(pos, text.to_string());
    }

    /// Insert a block of lines at 0-based index pos
    fn lines_insert_block(&mut self, pos: usize, block: &[String]) {
        for (i, item) in block.iter().enumerate() {
            self.lines.insert(pos + i, item.clone());
        }
    }

    /// Erase lines from [start, end) 0-based, exclusive of end
    fn lines_erase(&mut self, start: usize, end: usize) {
        self.lines.drain(start..end);
    }

    fn lines_clear(&mut self) {
        self.lines.clear();
    }

    fn print_line(&self, n: i64) {
        if n >= 1 && n <= self.line_count() {
            println!("{}:\t{}", n, self.lines[(n - 1) as usize]);
        }
    }

    fn list_lines(&mut self, mut start: i64, mut end: i64) {
        if self.line_count() == 0 {
            println!("Empty buffer");
            return;
        }
        if start < 1 {
            start = 1;
        }
        if end > self.line_count() {
            end = self.line_count();
        }
        for i in start..=end {
            let marker = if i == self.current_line { "*" } else { " " };
            println!("{}{}:\t{}", marker, i, self.lines[(i - 1) as usize]);
        }
        if end >= 1 {
            self.current_line = end;
        }
    }

    fn load_file(&mut self, filename: &str) {
        self.lines_clear();
        match File::open(filename) {
            Err(_) => {
                println!("New file");
                self.current_file = filename.to_string();
                self.current_line = 0;
            }
            Ok(f) => {
                let reader = io::BufReader::new(f);
                for line_res in reader.lines() {
                    let mut line = match line_res {
                        Ok(l) => l,
                        Err(_) => break,
                    };
                    // Reading lines via lines() already strips '\n'; also strip stray '\r'
                    strip_trailing_cr(&mut line);
                    let pos = self.lines.len();
                    self.lines_insert(pos, &line);
                }
                self.current_file = filename.to_string();
                self.current_line = if self.line_count() == 0 { 0 } else { 1 };
                println!("{} lines", self.line_count());
            }
        }
    }

    fn save_file(&mut self, filename: &str) {
        let target: &str = if filename.is_empty() {
            &self.current_file
        } else {
            filename
        };
        if target.is_empty() {
            println!("No file name");
            return;
        }
        match File::create(target) {
            Err(_) => {
                println!("Cannot write file");
            }
            Ok(f) => {
                let mut out = BufWriter::new(f);
                for line in &self.lines {
                    let _ = write!(out, "{}\r\n", line);
                }
                let _ = out.flush();
                println!("{} lines written", self.line_count());
                self.modified = false;
            }
        }
    }

    /// Insert mode: read lines from stdin until a single "." on its own line
    fn insert_lines(&mut self, after_line: i64, reader: &mut impl BufRead) {
        println!("Press F6 or Ctrl+Z then Enter to end insertion (or '.' on its own line)");
        let mut insert_pos: i64 = after_line; // 0-based index to insert at
        loop {
            print!("{}:*", insert_pos + 1);
            let _ = io::stdout().flush();
            let input = match get_line_stdin(reader) {
                Some(s) => s,
                None => break,
            };
            if input == "." {
                break;
            }
            // strip ^Z if user typed it literally
            if let Some(zpos) = input.find('\x1A') {
                let truncated = &input[..zpos];
                self.lines_insert(insert_pos as usize, truncated);
                insert_pos += 1;
                self.modified = true;
                break;
            }
            self.lines_insert(insert_pos as usize, &input);
            insert_pos += 1;
            self.modified = true;
        }
        self.current_line = insert_pos; // EDLIN sets current line to one after last inserted
        if self.current_line > self.line_count() {
            self.current_line = self.line_count();
        }
    }

    /// Delete a range of lines (1-based, inclusive)
    fn delete_lines(&mut self, mut start: i64, mut end: i64) {
        if self.line_count() == 0 {
            println!("Empty buffer");
            return;
        }
        if start < 1 {
            start = 1;
        }
        if end > self.line_count() {
            end = self.line_count();
        }
        if start > end {
            return;
        }
        self.lines_erase((start - 1) as usize, end as usize);
        self.modified = true;
        self.current_line = start;
        if self.current_line > self.line_count() {
            self.current_line = self.line_count();
        }
    }

    /// Search for text starting from current line
    fn search_lines(&mut self, target: &str, case_sensitive: bool) {
        if target.is_empty() {
            println!("?");
            return;
        }
        let needle = if case_sensitive {
            target.to_string()
        } else {
            target.to_lowercase()
        };

        let start = if self.current_line < 1 {
            1
        } else {
            self.current_line
        };
        for i in start..=self.line_count() {
            let hay = if case_sensitive {
                self.lines[(i - 1) as usize].clone()
            } else {
                self.lines[(i - 1) as usize].to_lowercase()
            };
            if hay.contains(&needle) {
                self.current_line = i;
                self.print_line(i);
                return;
            }
        }
        println!("Not found");
    }

    /// Replace text on current line (simple R command: replace old with new)
    fn replace_lines(&mut self, old_text: &str, new_text: &str, global: bool) {
        if old_text.is_empty() {
            println!("?");
            return;
        }
        let mut any = false;
        let start = if self.current_line < 1 {
            1
        } else {
            self.current_line
        };
        let mut i = start;
        while i <= self.line_count() {
            let idx = (i - 1) as usize;
            if let Some(pos) = self.lines[idx].find(old_text) {
                let l = &self.lines[idx];
                let mut new_buf = String::with_capacity(l.len() + new_text.len());
                new_buf.push_str(&l[..pos]);
                new_buf.push_str(new_text);
                new_buf.push_str(&l[pos + old_text.len()..]);
                self.lines[idx] = new_buf;
                self.modified = true;
                self.current_line = i;
                self.print_line(i);
                any = true;
                if !global {
                    return;
                }
            }
            i += 1;
        }
        if !any {
            println!("Not found");
        }
    }

    /// Page through lines, 23 at a time, updating current line to last shown
    fn page_lines(&mut self, mut start: i64, mut end: i64) {
        if self.line_count() == 0 {
            println!("Empty buffer");
            return;
        }
        if start < 1 {
            start = 1;
        }
        if end < 1 || end > self.line_count() {
            end = self.line_count();
        }
        if start > end {
            return;
        }

        let mut i = start;
        while i <= end {
            let page_end = imin(end, i + 22);
            for n in i..=page_end {
                let marker = if n == self.current_line { "*" } else { " " };
                println!("{}{}:\t{}", marker, n, self.lines[(n - 1) as usize]);
            }
            self.current_line = page_end;
            i = page_end + 1;
        }
    }

    /// Copy lines [start..end] to before 'destination', optionally 'count' times
    fn copy_lines(&mut self, mut start: i64, mut end: i64, mut destination: i64, mut count: i64) {
        if self.line_count() == 0 {
            println!("Empty buffer");
            return;
        }
        if start < 1 {
            start = 1;
        }
        if end > self.line_count() {
            end = self.line_count();
        }
        if start > end {
            println!("?");
            return;
        }
        if destination < 1 {
            destination = 1;
        }
        if destination > self.line_count() + 1 {
            destination = self.line_count() + 1;
        }
        if count < 1 {
            count = 1;
        }

        let block: Vec<String> = self.lines[(start - 1) as usize..end as usize].to_vec();

        // destination is 1-based line number that the copy is placed before
        let mut dest_idx: i64 = destination - 1; // 0-based index

        for _ in 0..count {
            self.lines_insert_block(dest_idx as usize, &block);
            dest_idx += block.len() as i64;
        }

        self.modified = true;
        self.current_line = dest_idx; // current line set to last line of inserted copies
        if self.current_line > self.line_count() {
            self.current_line = self.line_count();
        }
    }

    /// Move lines [start..end] to before 'destination'
    fn move_lines(&mut self, mut start: i64, mut end: i64, mut destination: i64) {
        if self.line_count() == 0 {
            println!("Empty buffer");
            return;
        }
        if start < 1 {
            start = 1;
        }
        if end > self.line_count() {
            end = self.line_count();
        }
        if start > end {
            println!("?");
            return;
        }
        if destination < 1 {
            destination = 1;
        }
        if destination > self.line_count() + 1 {
            destination = self.line_count() + 1;
        }

        // If destination falls within the block being moved, treat as no-op
        if destination > start && destination <= end + 1 {
            println!("?");
            return;
        }

        let block: Vec<String> = self.lines[(start - 1) as usize..end as usize].to_vec();
        let block_count = block.len() as i64;
        self.lines_erase((start - 1) as usize, end as usize);

        let mut dest_idx: i64 = destination - 1; // 0-based index into buffer AFTER erase
        if destination > end {
            dest_idx -= block_count;
        }
        if dest_idx < 0 {
            dest_idx = 0;
        }
        if dest_idx > self.line_count() {
            dest_idx = self.line_count();
        }

        self.lines_insert_block(dest_idx as usize, &block);

        self.modified = true;
        self.current_line = dest_idx + block_count;
        if self.current_line > self.line_count() {
            self.current_line = self.line_count();
        }
        if self.current_line < 1 {
            self.current_line = if self.line_count() == 0 { 0 } else { 1 };
        }
    }

    /// Transfer (merge) contents of filename into the buffer before 'line_num'
    fn transfer_file(&mut self, mut line_num: i64, filename: &str) {
        if filename.is_empty() {
            println!("?");
            return;
        }
        let f = match File::open(filename) {
            Ok(f) => f,
            Err(_) => {
                println!("Cannot find file");
                return;
            }
        };

        let reader = io::BufReader::new(f);
        let mut incoming: Vec<String> = Vec::new();
        for line_res in reader.lines() {
            let mut line = match line_res {
                Ok(l) => l,
                Err(_) => break,
            };
            strip_trailing_cr(&mut line);
            incoming.push(line);
        }

        if line_num < 1 {
            line_num = 1;
        }
        if line_num > self.line_count() + 1 {
            line_num = self.line_count() + 1;
        }
        let dest_idx = (line_num - 1) as usize;

        let incoming_count = incoming.len() as i64;
        self.lines_insert_block(dest_idx, &incoming);

        self.modified = true;
        self.current_line = dest_idx as i64 + incoming_count;
        if self.current_line > self.line_count() {
            self.current_line = self.line_count();
        }
        if self.current_line < 1 {
            self.current_line = if self.line_count() == 0 { 0 } else { 1 };
        }

        println!("{} lines transferred", incoming_count);
    }
}

fn is_digit_char(c: u8) -> bool {
    c.is_ascii_digit()
}

/// Parse a command line of form: [lineRange]Letter[arguments]
/// Range can be: N, N,M, ,M, N, (current to N)
fn parse_command(input: &str) -> Option<ParsedCmd> {
    let bytes = input.as_bytes();
    let len = bytes.len();
    let mut i: usize = 0;

    let mut n1: i64 = -1;
    let mut n2: i64 = -1;
    let mut n3: i64 = -1;
    let mut has_comma = false;
    let mut has_comma2 = false;

    if i < len && is_digit_char(bytes[i]) {
        let j_start = i;
        let mut j = i;
        while j < len && is_digit_char(bytes[j]) {
            j += 1;
        }
        n1 = input[j_start..j].parse::<i64>().unwrap_or(0);
        i = j;
    }
    if i < len && bytes[i] == b',' {
        has_comma = true;
        i += 1;
        if i < len && is_digit_char(bytes[i]) {
            let j_start = i;
            let mut j = i;
            while j < len && is_digit_char(bytes[j]) {
                j += 1;
            }
            n2 = input[j_start..j].parse::<i64>().unwrap_or(0);
            i = j;
        }
    }
    if i < len && bytes[i] == b',' {
        has_comma2 = true;
        i += 1;
        if i < len && is_digit_char(bytes[i]) {
            let j_start = i;
            let mut j = i;
            while j < len && is_digit_char(bytes[j]) {
                j += 1;
            }
            n3 = input[j_start..j].parse::<i64>().unwrap_or(0);
            i = j;
        }
    }

    // skip spaces
    while i < len && bytes[i] == b' ' {
        i += 1;
    }

    if i >= len {
        // just a number -> go to line
        if n1 != -1 && !has_comma {
            return Some(ParsedCmd {
                start: n1,
                end: n1, // unused for '#', but keep consistent
                third: -1,
                cmd: '#', // pseudo command: go to line
                rest: String::new(),
            });
        }
        return None;
    }

    let cmd = (bytes[i] as char).to_ascii_uppercase();
    i += 1;
    while i < len && bytes[i] == b' ' {
        i += 1;
    }

    let mut rest = input[i..].to_string();
    if rest.len() >= MAX_LINE_LEN {
        rest.truncate(MAX_LINE_LEN - 1);
    }

    let (start, end, third) = if has_comma2 {
        (n1, n2, n3)
    } else if has_comma {
        (n1, n2, -1)
    } else {
        (n1, n1, -1)
    };

    Some(ParsedCmd {
        start,
        end,
        third,
        cmd,
        rest,
    })
}

fn print_help() {
    print!(
        "Commands:\n\
         \x20 n               Go to / display line n\n\
         \x20 L               List all lines\n\
         \x20 L   n,mL        List lines n through m\n\
         \x20 P   n,mP        Page through lines n through m\n\
         \x20 I   n I         Insert before line n (omit n to insert at current)\n\
         \x20 D   n,mD        Delete lines n through m\n\
         \x20 C   n,m,dC      Copy lines n-m to before line d\n\
         \x20 C   n,m,dC cnt  Copy lines n-m to before line d, 'cnt' times\n\
         \x20 M   n,m,dM      Move lines n-m to before line d\n\
         \x20 T   n T file    Transfer (merge) contents of 'file' before line n\n\
         \x20 S   text        Search for text from current line\n\
         \x20 R   old new     Replace 'old' with 'new' on current/next matching line\n\
         \x20 W   filename    Write (save) buffer to file\n\
         \x20 E               End edit (save and quit)\n\
         \x20 Q               Quit without saving\n\
         \x20 ?               Help\n"
    );
}

fn run_command(editor: &mut Editor, raw: &str, reader: &mut impl BufRead) {
    let mut input = raw.to_string();
    if input.len() >= MAX_LINE_LEN {
        input.truncate(MAX_LINE_LEN - 1);
    }

    // trim trailing \r or \n
    while input.ends_with('\r') || input.ends_with('\n') {
        input.pop();
    }

    if input.is_empty() {
        // blank: list next line
        if editor.current_line < editor.line_count() {
            editor.current_line += 1;
        }
        if editor.line_count() > 0 {
            editor.print_line(editor.current_line);
        }
        return;
    }

    let p = match parse_command(&input) {
        Some(p) => p,
        None => {
            println!("?");
            return;
        }
    };

    match p.cmd {
        '#' => {
            // line number entered alone -> go to and display
            if p.start >= 1 && p.start <= editor.line_count() {
                editor.current_line = p.start;
                editor.print_line(editor.current_line);
            } else {
                println!("?");
            }
        }
        'L' => {
            let s = if p.start == -1 {
                imax(1, editor.current_line - 5)
            } else {
                p.start
            };
            let mut e = if p.end == -1 {
                imin(editor.line_count(), s + 22)
            } else {
                p.end
            };
            if p.start == -1 && p.end == -1 {
                e = editor.line_count();
            }
            editor.list_lines(s, e);
        }
        'I' => {
            let target = if p.start == -1 {
                editor.current_line
            } else {
                p.start
            };
            let mut after_idx = imax(0, target - 1); // insert before line 'target' -> 0-based index target-1
            if target < 1 {
                after_idx = 0;
            }
            editor.insert_lines(after_idx, reader);
        }
        'D' => {
            let s = if p.start == -1 {
                editor.current_line
            } else {
                p.start
            };
            let e = if p.end == -1 { s } else { p.end };
            editor.delete_lines(s, e);
        }
        'P' => {
            let s = if p.start == -1 {
                imax(1, editor.current_line - 5)
            } else {
                p.start
            };
            let mut e = if p.end == -1 {
                imin(editor.line_count(), s + 22)
            } else {
                p.end
            };
            if p.start == -1 && p.end == -1 {
                e = editor.line_count();
            }
            editor.page_lines(s, e);
        }
        'C' => {
            // n,m,dC [count]
            if p.start == -1 || p.third == -1 {
                println!("?");
            } else {
                let s = p.start;
                let e = if p.end == -1 { s } else { p.end };
                let dest = p.third;
                let mut count: i64 = 1;
                if !p.rest.is_empty() {
                    count = p.rest.trim().parse::<i64>().unwrap_or(0);
                    if count == 0 {
                        count = 1;
                    }
                }
                editor.copy_lines(s, e, dest, count);
            }
        }
        'M' => {
            // n,m,dM
            if p.start == -1 || p.third == -1 {
                println!("?");
            } else {
                let s = p.start;
                let e = if p.end == -1 { s } else { p.end };
                let dest = p.third;
                editor.move_lines(s, e, dest);
            }
        }
        'T' => {
            // n T filename
            let target = if p.start == -1 {
                editor.current_line + 1
            } else {
                p.start
            };
            if p.rest.is_empty() {
                println!("?");
            } else {
                editor.transfer_file(target, &p.rest);
            }
        }
        'S' => {
            editor.search_lines(&p.rest, false);
        }
        'R' => {
            // Format: R old new  (split on first space)
            match p.rest.find(' ') {
                None => println!("?"),
                Some(sp) => {
                    let old_t = p.rest[..sp].to_string();
                    let new_t = p.rest[sp + 1..].to_string();
                    editor.replace_lines(&old_t, &new_t, false);
                }
            }
        }
        'W' => {
            editor.save_file(&p.rest);
        }
        'E' => {
            editor.save_file("");
            exit(0);
        }
        'Q' => {
            print!("Abort edit (Y/N)? ");
            let _ = io::stdout().flush();
            if let Some(ans) = get_line_stdin(reader) {
                if ans.starts_with('Y') || ans.starts_with('y') {
                    exit(0);
                }
            }
        }
        '?' => {
            print_help();
        }
        _ => {
            println!("?");
        }
    }
}

fn current_timestamp() -> String {
    // Format: %Y%m%d%H%M using UTC (no external crates available)
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default();
    let secs = now.as_secs() as i64;

    // Convert seconds since epoch to UTC date/time components
    let days = secs.div_euclid(86400);
    let rem = secs.rem_euclid(86400);
    let hour = rem / 3600;
    let minute = (rem % 3600) / 60;

    // Civil-from-days algorithm (Howard Hinnant's algorithm)
    let z = days + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = (z - era * 146097) as u64; // [0, 146096]
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    let y = yoe as i64 + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
    let mp = (5 * doy + 2) / 153; // [0, 11]
    let d = doy - (153 * mp + 2) / 5 + 1; // [1, 31]
    let m = if mp < 10 { mp + 3 } else { mp - 9 }; // [1, 12]
    let y = if m <= 2 { y + 1 } else { y };

    format!(
        "{:04}{:02}{:02}{:02}{:02}",
        y, m, d, hour, minute
    )
}

fn main() {
    println!("EDLIN - Simple Line Editor");
    println!("Type ? for help.\n");

    let mut editor = Editor::new();

    let args: Vec<String> = env::args().collect();
    if args.len() > 1 {
        // load file entered at command line
        editor.load_file(&args[1]);
    } else {
        // if no command line parameters entered, default to filename = _out_[time stamp]
        let ts = current_timestamp();
        let fn_name = format!("_out_{}.txt", ts);
        editor.load_file(&fn_name);
    }

    let stdin = io::stdin();
    let mut reader = stdin.lock();

    loop {
        print!("*");
        let _ = io::stdout().flush();
        let input = match get_line_stdin(&mut reader) {
            Some(s) => s,
            None => break,
        };
        run_command(&mut editor, &input, &mut reader);
    }
}