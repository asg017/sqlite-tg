fn main() {
    let mut build = cc::Build::new();
    build
        .file("sqlite-tg.c")
        .file("tg.c")
        .define("SQLITE_CORE", None);
    if cfg!(target_os = "windows") {
        build.define("TG_NOATOMICS", None);
    }
    build.compile("sqlite_tg0");
}
