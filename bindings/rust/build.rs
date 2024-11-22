fn main() {
    cc::Build::new()
        .file("sqlite-tg.c")
        .file("tg.c")
        .define("SQLITE_CORE", None)
        .compile("sqlite_tg0");
}
