fn main() {
    // Only Windows executables carry embedded icon resources; skip this on
    // Linux/macOS builds where it isn't applicable.
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        let mut res = winresource::WindowsResource::new();
        res.set_icon("assets/icon.ico");
        res.compile().expect("failed to compile Windows resource (icon)");
    }
}