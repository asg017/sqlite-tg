use std::ffi::c_void;
#[link(name = "sqlite_tg0")]
extern "C" {
    pub fn sqlite3_tg_init(a:*mut c_void);
}

#[cfg(test)]
mod tests {
    use super::*;

    use rusqlite::{ffi::sqlite3_auto_extension, Connection};

    #[test]
    fn test_rusqlite_auto_extension() {
        let conn = Connection::open_in_memory().unwrap();
        unsafe {
          sqlite3_tg_init(conn.handle().cast());
        }

        let result: String = conn
            .query_row("select tg_version()", [], |x| x.get(0))
            .unwrap();

        assert!(result.starts_with("v"));
    }
}
