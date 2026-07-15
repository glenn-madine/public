// rustpad — a Notepad-style text editor
// Built with eframe/egui (which uses winit under the hood for windowing/events on
// Windows, Linux (X11 + Wayland), and macOS) plus rfd for native file dialogs.


#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod highlight;

use eframe::egui;
use highlight::Language;
use std::fs;
use std::path::PathBuf;

// Glenn Madine 7/15/2026

const VERSION: &str = "1.0.1";

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([900.0, 650.0])
            .with_min_inner_size([300.0, 200.0])
            .with_icon(load_icon()),
        ..Default::default()
    };

    eframe::run_native(
        "Untitled - rustpad",
        options,
        Box::new(|cc| {
            // Use a plain monospace-ish look closer to Notepad.
            let mut style = (*cc.egui_ctx.style()).clone();
            style.override_font_id = Some(egui::FontId::new(15.0, egui::FontFamily::Monospace));
            cc.egui_ctx.set_style(style);
            Box::new(RustpadApp::default())
        }),
    )
}

/// Decodes the embedded .ico into the raw RGBA buffer eframe/winit wants for
/// the window's title-bar/control-menu icon, taskbar button, and Alt-Tab
/// thumbnail. (Separate from the .exe-level icon compiled in by build.rs —
/// Windows doesn't reuse that one for the live window automatically.)
fn load_icon() -> egui::IconData {
    let icon_bytes = include_bytes!("../assets/icon.ico");
    let image = image::load_from_memory(icon_bytes)
        .expect("failed to decode assets/icon.ico")
        .into_rgba8();
    let (width, height) = image.dimensions();
    egui::IconData {
        rgba: image.into_raw(),
        width,
        height,
    }
}

#[derive(Default)]
struct RustpadApp {
    text: String,
    file_path: Option<PathBuf>,
    dirty: bool,
    word_wrap: bool,
    language: Language,
    /// Char-index range of the current selection in the editor, refreshed
    /// each frame after the text widget is drawn. Used by the Edit menu's
    /// Cut/Copy/Paste actions.
    selection: Option<std::ops::Range<usize>>,
    status: String,
    // Pending action gates when there are unsaved changes.
    pending_action: Option<PendingAction>,
    show_unsaved_dialog: bool,
    show_about: bool,
    find_open: bool,
    find_query: String,
}

enum PendingAction {
    New,
    Open,
    Exit,
}

impl RustpadApp {
    fn window_title(&self) -> String {
        let name = self
            .file_path
            .as_ref()
            .map(|p| p.file_name().unwrap_or_default().to_string_lossy().to_string())
            .unwrap_or_else(|| "Untitled".to_string());
        let star = if self.dirty { "*" } else { "" };
        format!("{star}{name} - rustpad")
    }

    fn new_file(&mut self) {
        self.text.clear();
        self.file_path = None;
        self.dirty = false;
        self.language = Language::Plain;
        self.selection = None;
        self.status = "New file".to_string();
    }

    fn open_file(&mut self) {
        if let Some(path) = rfd::FileDialog::new()
            .add_filter("Text Files", &["txt"])
            .add_filter("C/C++/C# Files", &["c", "h", "cpp", "cs", "cc", "cxx", "hpp", "hh", "hxx"])
            .add_filter("Rust Files", &["rs"])
            .add_filter("Python Files", &["py", "pyw"])
            .add_filter("Visual Basic Files", &["vb", "vbs"])
            .add_filter("Batch Files", &["bat", "cmd"])
            .add_filter("HTML Files", &["htm", "html"])
            .add_filter("ASP.NET Files", &["aspx"])
            .add_filter("All Files", &["*"])
            .pick_file()
        {
            match fs::read_to_string(&path) {
                Ok(contents) => {
                    self.language = highlight::detect_language(&path);
                    self.text = contents;
                    self.file_path = Some(path);
                    self.dirty = false;
                    self.selection = None;
                    self.status = "File opened".to_string();
                }
                Err(e) => {
                    self.status = format!("Error opening file: {e}");
                }
            }
        }
    }

    fn save_file(&mut self) -> bool {
        if let Some(path) = self.file_path.clone() {
            self.write_to(&path)
        } else {
            self.save_file_as()
        }
    }

    fn save_file_as(&mut self) -> bool {
        if let Some(path) = rfd::FileDialog::new()
            .add_filter("Text Files", &["txt"])
            .add_filter("C/C++/C# Files", &["c", "h", "cpp", "cs", "cc", "cxx", "hpp", "hh", "hxx"])
            .add_filter("Rust Files", &["rs"])
            .add_filter("Python Files", &["py", "pyw"])
            .add_filter("Visual Basic Files", &["vb", "vbs"])
            .add_filter("Batch Files", &["bat", "cmd"])
            .add_filter("HTML Files", &["htm", "html"])
            .add_filter("ASP.NET Files", &["aspx"])
            .add_filter("All Files", &["*"])
            .set_file_name("Untitled.txt")
            .save_file()
        {
            self.write_to(&path)
        } else {
            false
        }
    }

    fn write_to(&mut self, path: &PathBuf) -> bool {
        match fs::write(path, &self.text) {
            Ok(()) => {
                self.file_path = Some(path.clone());
                self.language = highlight::detect_language(path);
                self.dirty = false;
                self.status = "File saved".to_string();
                true
            }
            Err(e) => {
                self.status = format!("Error saving file: {e}");
                false
            }
        }
    }

    /// Copies the current selection to the system clipboard, if any.
    fn copy_selection(&mut self) {
        use egui::TextBuffer as _;
        let Some(range) = self.selection.clone() else {
            self.status = "Nothing selected".to_string();
            return;
        };
        if range.is_empty() {
            self.status = "Nothing selected".to_string();
            return;
        }
        let selected = self.text.char_range(range);
        match arboard::Clipboard::new().and_then(|mut cb| cb.set_text(selected.to_string())) {
            Ok(()) => self.status = "Copied".to_string(),
            Err(e) => self.status = format!("Copy failed: {e}"),
        }
    }

    /// Copies the current selection to the clipboard, then removes it from the document.
    fn cut_selection(&mut self) {
        use egui::TextBuffer as _;
        let Some(range) = self.selection.clone() else {
            self.status = "Nothing selected".to_string();
            return;
        };
        if range.is_empty() {
            self.status = "Nothing selected".to_string();
            return;
        }
        let selected = self.text.char_range(range.clone());
        match arboard::Clipboard::new().and_then(|mut cb| cb.set_text(selected.to_string())) {
            Ok(()) => {
                self.text.delete_char_range(range);
                self.dirty = true;
                self.selection = None;
                self.status = "Cut".to_string();
            }
            Err(e) => self.status = format!("Cut failed: {e}"),
        }
    }

    /// Inserts clipboard text at the cursor, replacing the current selection if any.
    fn paste_clipboard(&mut self) {
        use egui::TextBuffer as _;
        let clipboard_text = match arboard::Clipboard::new().and_then(|mut cb| cb.get_text()) {
            Ok(t) => t,
            Err(e) => {
                self.status = format!("Paste failed: {e}");
                return;
            }
        };
        let insert_at = match self.selection.clone() {
            Some(range) if !range.is_empty() => {
                self.text.delete_char_range(range.clone());
                range.start
            }
            Some(range) => range.start,
            None => self.text.chars().count(),
        };
        self.text.insert_text(&clipboard_text, insert_at);
        self.dirty = true;
        self.selection = None;
        self.status = "Pasted".to_string();
    }


    /// stashes it and pops the "save changes?" prompt (just like Notepad).
    fn guard(&mut self, action: PendingAction) {
        if self.dirty {
            self.pending_action = Some(action);
            self.show_unsaved_dialog = true;
        } else {
            self.run_action(action);
        }
    }

    fn run_action(&mut self, action: PendingAction) {
        match action {
            PendingAction::New => self.new_file(),
            PendingAction::Open => self.open_file(),
            PendingAction::Exit => std::process::exit(0),
        }
    }
}

impl eframe::App for RustpadApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        ctx.send_viewport_cmd(egui::ViewportCommand::Title(self.window_title()));

        // --- Keyboard shortcuts (Notepad-standard) ---
        let input = ctx.input(|i| i.clone());
        let cmd = input.modifiers.command;
        if cmd && input.key_pressed(egui::Key::N) {
            self.guard(PendingAction::New);
        }
        if cmd && input.key_pressed(egui::Key::O) {
            self.guard(PendingAction::Open);
        }
        if cmd && input.modifiers.shift && input.key_pressed(egui::Key::S) {
            self.save_file_as();
        } else if cmd && input.key_pressed(egui::Key::S) {
            self.save_file();
        }
        if cmd && input.key_pressed(egui::Key::F) {
            self.find_open = true;
        }

        // Intercept the OS "close window" request so we don't lose work.
        if ctx.input(|i| i.viewport().close_requested()) && self.dirty && self.pending_action.is_none()
        {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.guard(PendingAction::Exit);
        }

        // --- Menu bar ---
        egui::TopBottomPanel::top("menu_bar").show(ctx, |ui| {
            egui::menu::bar(ui, |ui| {
                ui.menu_button("File", |ui| {
                    if ui.button("New\tCtrl+N").clicked() {
                        self.guard(PendingAction::New);
                        ui.close_menu();
                    }
                    if ui.button("Open...\tCtrl+O").clicked() {
                        self.guard(PendingAction::Open);
                        ui.close_menu();
                    }
                    if ui.button("Save\tCtrl+S").clicked() {
                        self.save_file();
                        ui.close_menu();
                    }
                    if ui.button("Save As...\tCtrl+Shift+S").clicked() {
                        self.save_file_as();
                        ui.close_menu();
                    }
                    ui.separator();
                    if ui.button("Exit").clicked() {
                        self.guard(PendingAction::Exit);
                        ui.close_menu();
                    }
                });
                ui.menu_button("Edit", |ui| {
                    if ui.button("Cut\tCtrl+X").clicked() {
                        self.cut_selection();
                        ui.close_menu();
                    }
                    if ui.button("Copy\tCtrl+C").clicked() {
                        self.copy_selection();
                        ui.close_menu();
                    }
                    if ui.button("Paste\tCtrl+V").clicked() {
                        self.paste_clipboard();
                        ui.close_menu();
                    }
                    ui.separator();
                    if ui.button("Select All\tCtrl+A").clicked() {
                        self.status = "Use Ctrl+A inside the text area".to_string();
                        ui.close_menu();
                    }
                    if ui.button("Find...\tCtrl+F").clicked() {
                        self.find_open = true;
                        ui.close_menu();
                    }
                    ui.separator();
                    ui.label("Undo/Redo: Ctrl+Z / Ctrl+Y (built into the text field)");
                });
                ui.menu_button("Format", |ui| {
                    ui.checkbox(&mut self.word_wrap, "Word Wrap");
                    ui.separator();
                    ui.menu_button(format!("Language: {}", self.language.label()), |ui| {
                        for lang in Language::ALL {
                            if ui.radio_value(&mut self.language, lang, lang.label()).clicked() {
                                ui.close_menu();
                            }
                        }
                    });
                });
                ui.menu_button("Help", |ui| {
                    if ui.button("About rustpad").clicked() {
                        self.show_about = true;
                        ui.close_menu();
                    }
                });
            });
        });

        // --- Status bar ---
        egui::TopBottomPanel::bottom("status_bar").show(ctx, |ui| {
            ui.horizontal(|ui| {
                let lines = self.text.lines().count().max(1);
                let chars = self.text.chars().count();
                ui.label(format!("Lines: {lines}   Chars: {chars}"));
                ui.separator();
                ui.label(if self.dirty { "Modified" } else { "Saved" });
                ui.separator();
                ui.label(self.language.label());
                ui.separator();
                ui.label(&self.status);
            });
        });

        // --- Find bar ---
        if self.find_open {
            egui::TopBottomPanel::top("find_bar").show(ctx, |ui| {
                ui.horizontal(|ui| {
                    ui.label("Find:");
                    let resp = ui.text_edit_singleline(&mut self.find_query);
                    if ui.button("Next").clicked() || resp.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                        if let Some(pos) = self.text.find(&self.find_query) {
                            self.status = format!("Found at byte offset {pos}");
                        } else if !self.find_query.is_empty() {
                            self.status = "Not found".to_string();
                        }
                    }
                    if ui.button("Close").clicked() {
                        self.find_open = false;
                    }
                });
            });
        }

        // --- Main text area ---
        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::both().show(ui, |ui| {
                let language = self.language;
                let mut layouter = move |ui: &egui::Ui, text: &str, wrap_width: f32| {
                    highlight::highlight(ui, text, language, wrap_width)
                };
                let editor = egui::TextEdit::multiline(&mut self.text)
                    .desired_width(if self.word_wrap { ui.available_width() } else { f32::INFINITY })
                    .desired_rows(30)
                    .lock_focus(true)
                    .frame(false)
                    .layouter(&mut layouter);
                let desired_size = ui.available_size();
                let output = ui.allocate_ui(desired_size, |ui| editor.show(ui)).inner;
                if output.response.changed() {
                    self.dirty = true;
                }
                self.selection = output
                    .cursor_range
                    .map(|cursor_range| cursor_range.as_sorted_char_range());
            });
        });

        // --- Unsaved-changes prompt ---
        if self.show_unsaved_dialog {
            egui::Window::new("rustpad")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label("Do you want to save changes to this file?");
                    ui.horizontal(|ui| {
                        if ui.button("Save").clicked() {
                            let saved = self.save_file();
                            self.show_unsaved_dialog = false;
                            if saved {
                                if let Some(action) = self.pending_action.take() {
                                    self.run_action(action);
                                }
                            } else {
                                self.pending_action = None;
                            }
                        }
                        if ui.button("Don't Save").clicked() {
                            self.dirty = false;
                            self.show_unsaved_dialog = false;
                            if let Some(action) = self.pending_action.take() {
                                self.run_action(action);
                            }
                        }
                        if ui.button("Cancel").clicked() {
                            self.show_unsaved_dialog = false;
                            self.pending_action = None;
                        }
                    });
                });
        }

        // --- About dialog ---
        if self.show_about {
            egui::Window::new("About rustpad")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label("rustpad — a small Notepad-style text editor");
					ui.label(format!("Version {VERSION}"));
                    ui.label("Built with Rust, egui/eframe (winit-backed), and rfd.");
                    if ui.button("Close").clicked() {
                        self.show_about = false;
                    }
                });
        }
    }
}
