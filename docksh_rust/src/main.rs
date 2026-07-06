use std::io::{self, Write};
use std::process::Command;
use std::env;
use std::fs;
use std::fs::OpenOptions;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};
use colored::Colorize;
// Docker version 29.5.2, build 79eb04c or greater is recommended 
// Author Glenn Madine
const VERSION: &str = "1.0.4"; 
const RELEASE_DATE: &str = "2026-06-07";
const HELP_TEXT: &str = r#"
╔══════════════════════════════════════════════════════════╗
║                   Docker Shell Help                      ║
╠══════════════════════════════════════════════════════════╣
║  Built-in Commands:                                      ║
║                                                          ║
║  HELP                     Show this help screen          ║
║                                                          ║
║  CD <path>                Change current directory       ║
║  COPY <source> <destfile> Copy a file                    ║ 
║  CLEAR/CLS                Clear the terminal screen      ║
║  DEL <file>               Delete a file                  ║
║  DIR/LS <path>            List contents of a directory   ║
║  DO <command>             Execute a system command       ║
║  MKDIR/MD <dir>           Create a new directory         ║
║  NEW <project name>       Create new Docker project      ║
║                           folder with Dockerfile & .env  ║
║  REN <oldname> <newname>  Rename a file or directory     ║
║  RMDIR/RD <dir>           Remove a directory (empty)     ║
║  TOUCH <file>             Create an new file             ║
║                                                          ║
║  Any other input is passed directly to Docker            ║
║  as parameters (e.g. "ps", "images", "run ...")          ║
║                                                          ║
║  EXIT                     Quit the Docker Shell          ║
║                                                          ║
╚══════════════════════════════════════════════════════════╝
"#;

const BANNER: &str = r#"
╔═══════════════════════════════════════════════════════╗
║   Docker Shell | Type HELP for usage, EXIT to quit.   ║
╠═══════════════════════════════════════════════════════╣"#; 

fn main() {
    setup_console(); 
    print!("\x1B]0;Docker Shell {VERSION}\x07");
    io::stdout().flush().ok();
    handle_clear();
    println!("{BANNER}"); 
    println!("║   Version: {VERSION} Release Date: {RELEASE_DATE}             ║");
    println!("╚═══════════════════════════════════════════════════════╝"); 
    loop {
        print!("✨ {} ▶️ ", get_current_dir().unwrap_or_else(|_| "Unknown".into()));
	    io::stdout().flush().expect("Failed to flush stdout");
		let mut input = String::new();
        match io::stdin().read_line(&mut input) {
            Ok(0) => {
                // EOF (e.g. piped input ended)
                println!("\nEOF received. Exiting.");
                break;
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("Error reading input: {e}");
                break;
            }
        }

        let input = input.trim();
        if input.is_empty() {
            continue;
        }

        // Split into command keyword and the rest
        let (keyword, rest) = match input.split_once(char::is_whitespace) {
            Some((k, r)) => (k, r.trim()),
            None => (input, ""),
        };

        match keyword.to_uppercase().as_str() {
            "EXIT" => {
                println!("Goodbye!");
                break;
            }

            "HELP" => {
                println!("{HELP_TEXT}");
            } 
			
            "DOCKER" => {
                run_docker(&input[6..]);
				println!("{} {}", "ⓘ".bright_red(), "Note. Entering the command:'Docker' is not required.\n".bright_yellow());
            } 			
			
            "REN" => {
                handle_ren(rest);
            } 

            "CLEAR" => {
                handle_clear();
            }

            "CLS" => {
                handle_clear();
            }

            "DEL" => {
                handle_delete(rest);
            } 

            "COPY" => {
                handle_copy(rest);
            }    

            "RMDIR" => {
                handle_rmdir(rest);
            }

            "RD" => {
                handle_rmdir(rest);
            }

            "MKDIR" => {
                handle_mkdir(rest);
            } 

            "MD" => {
                handle_mkdir(rest);
            } 


            "VER" => {
                println!("Docker Shell Version: {VERSION}");
            } 

            "TOUCH" => {
                handle_touch(rest);
            }

            "NEW" => {
                handle_new_project(rest); 
	        }

            "DIR" => {
                if rest.is_empty() {
                    match list_directory(".") {
                        Ok(()) => {}
                        Err(e) => eprintln!("Failed to list current directory: {e}"),
                    }                    
                    continue;
                }
                match list_directory(rest) {
                    Ok(()) => {}
                    Err(e) => eprintln!("Failed to list directory: {e}"),
                }
            }
 
            "LS" => {
                if rest.is_empty() {
                    match list_directory(".") {
                        Ok(()) => {}
                        Err(e) => eprintln!("Failed to list current directory: {e}"),
                    }                    
                    continue;
                }
                match list_directory(rest) {
                    Ok(()) => {}
                    Err(e) => eprintln!("Failed to list directory: {e}"),
                }
            }

            "CD" => {
                if rest.is_empty() {
                    eprintln!("Usage: CD <path>");
                    continue;
                }
                match change_directory(rest) {
                    Ok(()) => {}
                    Err(e) => eprintln!("Failed to change directory: {e}"),
                }
            }
            
            "DO" => {
                if rest.is_empty() {
                    eprintln!("Usage: DO <command> [args...]");
                    continue;
                }
                run_shell_command(rest);
            }

            // Anything else → forward to Docker
            _ => {
                run_docker(input);
            }
        }
    }
}

/// Gets current working directory
fn get_current_dir() -> Result<String, String> {
    env::current_dir()
        .map(|path| path.to_string_lossy().into_owned())
        .map_err(|e| e.to_string())
}

/// Run an arbitrary shell command (DO built-in).
fn run_shell_command(cmd_line: &str) {
    let mut parts = cmd_line.split_whitespace();
    let program = match parts.next() {
        Some(p) => p,
        None => {
            eprintln!("No command provided.");
            return;
        }
    };
    let args: Vec<&str> = parts.collect();

    match Command::new(program).args(&args).status() {
        Ok(status) => {
            if !status.success() {
                eprintln!("Command exited with status: {status}");
            }
        }
        Err(e) => eprintln!("Failed to run '{program}': {e}"),
    }
}

/// Pass the full input string as arguments to the `docker` binary.
fn run_docker(args_str: &str) {
    let args: Vec<&str> = args_str.split_whitespace().collect();

    match Command::new("docker").args(&args).status() {
        Ok(status) => {
            if !status.success() {
                eprintln!("docker exited with status: {status}\n");
            }
        }
        Err(e) => eprintln!("Failed to run docker: {e}"),
    }
}

/// Change working directory
fn change_directory(path: &str) -> io::Result<()> {
    env::set_current_dir(path)
}

/// Formats a SystemTime into a human-readable "YYYY-MM-DD HH:MM:SS" string (UTC).
fn format_modified(time: SystemTime) -> String {
    let secs = time
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
 
    // Manual UTC breakdown (no external crate needed)
    let s = secs % 60;
    let m = (secs / 60) % 60;
    let h = (secs / 3600) % 24;
    let days = secs / 86400;
 
    // Days since 1970-01-01 → calendar date
    let (year, month, day) = days_to_ymd(days);
 
    format!("{:04}-{:02}-{:02} {:02}:{:02}:{:02} UTC", year, month, day, h, m, s)
}
 
/// Converts days since Unix epoch to (year, month, day).
fn days_to_ymd(mut days: u64) -> (u64, u64, u64) {
    let mut year = 1970u64;
    loop {
        let days_in_year = if is_leap(year) { 366 } else { 365 };
        if days < days_in_year {
            break;
        }
        days -= days_in_year;
        year += 1;
    }
    let months = [31, if is_leap(year) { 29 } else { 28 }, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    let mut month = 1u64;
    for &dim in &months {
        if days < dim {
            break;
        }
        days -= dim;
        month += 1;
    }
    (year, month, days + 1)
}
 
fn is_leap(y: u64) -> bool {
    (y % 4 == 0 && y % 100 != 0) || y % 400 == 0
}


/// Displays the contents of a directory, listing files and subdirectories.
/// Returns an error if the path doesn't exist or isn't a directory.
fn list_directory(path: &str) -> Result<(), Box<dyn std::error::Error>> {
    let dir_path = Path::new(path);
 
    if !dir_path.exists() {
        return Err(format!("Path '{}' does not exist", path).into());
    }
 
    if !dir_path.is_dir() {
        return Err(format!("Path '{}' is not a directory", path).into());
    }
 
    println!("Contents of '{}':", path);
    println!("{}", "-".repeat(60));
 
    let mut entries: Vec<_> = fs::read_dir(dir_path)?
        .filter_map(|entry| entry.ok())
        .collect();
 
    // Sort entries: directories first, then files, both alphabetically
    entries.sort_by_key(|e| {
        let is_file = e.path().is_file();
        let name = e.file_name().to_string_lossy().to_lowercase();
        (is_file, name)
    });
 
    if entries.is_empty() {
        println!("  (empty directory)");
    } else {
        for entry in &entries {
            let file_name = entry.file_name();
            let name = file_name.to_string_lossy();
            let metadata = entry.metadata()?;
            let modified = format_modified(metadata.modified()?);
 
            if metadata.is_dir() {
                println!("  📁 {}/  [modified: {}]", name, modified);
            } else {
                let size = metadata.len();
                println!("  📄 {}  ({} bytes)  [modified: {}]", name, size, modified);
            }
        }
    }
 
    println!("{}", "-".repeat(60));
    println!("Total: {} item(s)\n", entries.len());
 
    Ok(())
}

/// Run TOUCH command: create an empty file if it doesn't exist, or update its modification timestamp if it does.
fn handle_touch(rest: &str) {
    let filename = rest.trim();

    if filename.is_empty() {
        eprintln!("Usage: TOUCH <file>");
        return;
    }

    let path = Path::new(filename);

    if path.exists() {
        // File exists — update its modification timestamp
        let now = filetime::FileTime::now();
        match filetime::set_file_mtime(path, now) {
            Ok(_) => println!("Touched: {}", filename),
            Err(e) => eprintln!("Error updating timestamp for '{}': {}", filename, e),
        }
    } else {
        // File does not exist — create it (empty)
        match OpenOptions::new().create(true).write(true).open(path) {
            Ok(_) => println!("Created: {}", filename),
            Err(e) => eprintln!("Error creating '{}': {}", filename, e),
        }
    }
}

/// NEW PROJECT command: create a project folder with a Dockerfile and .env file inside it.
fn handle_new_project(name: &str) {
    let name = name.trim();
    if name.is_empty() {
        eprintln!("Usage: NEW <name>");
        return;
    }
    let project_path = Path::new(name);
    if project_path.exists() {
        eprintln!("Error: '{}' already exists.", name);
        return;
    }
    // Create the project folder
    if let Err(e) = fs::create_dir_all(project_path) {
        eprintln!("Error creating project folder '{}': {}", name, e);
        return;
    }
    println!("Created project folder: {}", name);
    // Create Dockerfile
    let dockerfile_path = project_path.join("Dockerfile");
    match OpenOptions::new().create(true).write(true).open(&dockerfile_path) {
        Ok(_) => println!("Created: {}/Dockerfile", name),
        Err(e) => {
            eprintln!("Error creating Dockerfile: {}", e);
            return;
        }
    }
    // Create .env
    let env_path = project_path.join(".env");
    match OpenOptions::new().create(true).write(true).open(&env_path) {
        Ok(_) => println!("Created: {}/.env", name),
        Err(e) => eprintln!("Error creating .env: {}", e),
    }
	// Open the new project folder in the native file manager
	open_folder_in_explorer(project_path.to_string_lossy().as_ref());
    // Change into the new project directory
    match env::set_current_dir(project_path) {
        Ok(_) => println!("Changed directory to: {}", name),
        Err(e) => eprintln!("Error changing directory to '{}': {}", name, e),
    }
}

/// Open a folder in the native file manager.
/// Uses `explorer` on Windows, `open` on macOS, and `xdg-open` on Linux.
fn open_folder_in_explorer(path: &str) {
    #[cfg(target_os = "windows")]
    let (program, args): (&str, &[&str]) = ("explorer", &[path]);
 
    #[cfg(target_os = "macos")]
    let (program, args): (&str, &[&str]) = ("open", &[path]);
 
    #[cfg(target_os = "linux")]
    let (program, args): (&str, &[&str]) = ("xdg-open", &[path]);
 
    #[cfg(not(any(target_os = "windows", target_os = "macos", target_os = "linux")))]
    {
        eprintln!("Cannot open folder: unsupported platform.");
        return;
    }
 
    match Command::new(program).args(args).spawn() {
        Ok(_)  => println!("Opened folder: {}", path),
        Err(e) => eprintln!("Failed to open folder '{}': {}", path, e),
    }
}

// MkDir command: create a new directory (or nested directories if path includes subdirs).
fn handle_mkdir(rest: &str) {
    let dirname = rest.trim();

    if dirname.is_empty() {
        eprintln!("Usage: MKDIR <directory>");
        return;
    }

    let path = Path::new(dirname);

    if path.exists() {
        eprintln!("Already exists: '{}'", dirname);
        return;
    }

    // create_dir_all handles nested paths like "a/b/c"
    match fs::create_dir_all(path) {
        Ok(_) => println!("Created directory: {}", dirname),
        Err(e) => eprintln!("Error creating '{}': {}", dirname, e),
    }
}

// RMDIR command: remove a directory. Supports optional -r / --recursive flag to remove non-empty directories.
fn handle_rmdir(rest: &str) {
    // Support optional -r / --recursive flag
    let args: Vec<&str> = rest.split_whitespace().collect();

    let (recursive, dirname) = match args.as_slice() {
        [] => {
            eprintln!("Usage: RMDIR [-r] <directory>");
            return;
        }
        ["-r", dir] | ["--recursive", dir] => (true, *dir),
        [dir] => (false, *dir),
        _ => {
            eprintln!("Usage: RMDIR [-r] <directory>");
            return;
        }
    };

    let path = Path::new(dirname);

    if !path.exists() {
        eprintln!("Not found: '{}'", dirname);
        return;
    }

    if !path.is_dir() {
        eprintln!("Not a directory: '{}'", dirname);
        return;
    }

    let result = if recursive {
        fs::remove_dir_all(path)
    } else {
        // remove_dir fails if directory is non-empty — same as plain rmdir
        fs::remove_dir(path)
    };

    match result {
        Ok(_) => println!("Removed directory: {}", dirname),
        Err(e) => {
            if e.raw_os_error() == Some(39) || e.to_string().contains("not empty") {
                eprintln!("Error: '{}' is not empty. Use RMDIR -r to remove recursively.", dirname);
            } else {
                eprintln!("Error removing '{}': {}", dirname, e);
            }
        }
    }
}

/// COPY command: copy a file from source to destination. If destination is an existing directory, copy file into it with the same name. Creates parent directories of destination if needed.
fn handle_copy(rest: &str) {
    let args: Vec<&str> = rest.split_whitespace().collect();

    if args.len() != 2 {
        eprintln!("Usage: COPY <source> <destination>");
        return;
    }

    let (src_str, dst_str) = (args[0], args[1]);
    let src = Path::new(src_str);
    let dst = Path::new(dst_str);

    if !src.exists() {
        eprintln!("Source not found: '{}'", src_str);
        return;
    }

    if src.is_dir() {
        eprintln!("Error: '{}' is a directory. COPY only supports files.", src_str);
        return;
    }

    // If destination is an existing directory, copy file into it with the same name
    let resolved_dst = if dst.is_dir() {
        let filename = src.file_name().expect("Source has no filename");
        dst.join(filename)
    } else {
        dst.to_path_buf()
    };

    // Create parent directories of the destination if they don't exist
    if let Some(parent) = resolved_dst.parent() {
        if !parent.as_os_str().is_empty() {
            if let Err(e) = fs::create_dir_all(parent) {
                eprintln!("Error creating destination directory: {}", e);
                return;
            }
        }
    }

    match fs::copy(src, &resolved_dst) {
        Ok(bytes) => println!("Copied '{}' -> '{}' ({} bytes)", src_str, resolved_dst.display(), bytes),
        Err(e) => eprintln!("Error copying '{}' to '{}': {}", src_str, resolved_dst.display(), e),
    }
}

/// DELETE command: delete a file. Does not support directories (use RMDIR for that). Provides error messages if file doesn't exist or is a directory.  
fn handle_delete(rest: &str) {
    let filename = rest.trim();

    if filename.is_empty() {
        eprintln!("Usage: DEL <file>");
        return;
    }

    let path = Path::new(filename);

    if !path.exists() {
        eprintln!("Not found: '{}'", filename);
        return;
    }

    if path.is_dir() {
        eprintln!("Error: '{}' is a directory. Use RMDIR to remove directories.", filename);
        return;
    }

    match fs::remove_file(path) {
        Ok(_) => println!("Deleted: {}", filename),
        Err(e) => eprintln!("Error deleting '{}': {}", filename, e),
    }
}

/// CLEAR command: clear the terminal screen. Uses 'cls' on Windows and 'clear' on Unix-like systems.
fn handle_clear() {
    // ANSI escape: clear screen and move cursor to home position
    print!("\x1B[2J\x1B[H");
    io::stdout().flush().ok();
}

/// REN command: rename a file or directory. Usage: REN <oldname> <newname>. Provides error messages if old name doesn't exist, new name already exists, or if renaming fails. 
fn handle_ren(rest: &str) {
    let args: Vec<&str> = rest.split_whitespace().collect();

    if args.len() != 2 {
        eprintln!("Usage: REN <oldname> <newname>");
        return;
    }

    let (old_str, new_str) = (args[0], args[1]);
    let old_path = Path::new(old_str);
    let new_path = Path::new(new_str);

    if !old_path.exists() {
        eprintln!("Not found: '{}'", old_str);
        return;
    }

    if new_path.exists() {
        eprintln!("Error: '{}' already exists.", new_str);
        return;
    }

    match fs::rename(old_path, new_path) {
        Ok(_) => println!("Renamed '{}' -> '{}'", old_str, new_str),
        Err(e) => eprintln!("Error renaming '{}': {}", old_str, e),
    }
}
 
#[cfg(windows)]
fn setup_console() {
    // Enable UTF-8 output (code page 65001) and ANSI/VT processing on Windows 10+
    use std::os::windows::io::AsRawHandle;
    use std::ptr;
 
    // winapi equivalents via raw FFI — no external crate needed
    extern "system" {
        fn SetConsoleOutputCP(wCodePageID: u32) -> i32;
        fn GetStdHandle(nStdHandle: u32) -> *mut std::ffi::c_void;
        fn GetConsoleMode(hConsoleHandle: *mut std::ffi::c_void, lpMode: *mut u32) -> i32;
        fn SetConsoleMode(hConsoleHandle: *mut std::ffi::c_void, dwMode: u32) -> i32;
    }
 
    unsafe {
        // Set code page to UTF-8
        SetConsoleOutputCP(65001);
 
        // Enable ENABLE_VIRTUAL_TERMINAL_PROCESSING (0x0004) for ANSI colours
        let handle = GetStdHandle(0xFFFFFFF5u32); // STD_OUTPUT_HANDLE
        if !handle.is_null() {
            let mut mode: u32 = 0;
            if GetConsoleMode(handle, &mut mode) != 0 {
                SetConsoleMode(handle, mode | 0x0004);
            }
        }
    }
}
 
#[cfg(not(windows))]
fn setup_console() {
    // Linux/macOS terminals are UTF-8 by default — nothing to do
}