fn main() {
    let mut build = cc::Build::new();
    build
        .file("sqlite-tg.c")
        .file("tg.c")
        .define("SQLITE_CORE", None);
    if cfg!(target_os = "windows") {
        build.flag("/std:c11").flag("/D_C11_ATOMICS");
    }
    build.compile("sqlite_tg0");
}
