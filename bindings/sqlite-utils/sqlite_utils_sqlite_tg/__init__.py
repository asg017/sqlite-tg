from sqlite_utils import hookimpl
import sqlite_tg

from sqlite_utils_sqlite_tg.version import __version_info__, __version__


@hookimpl
def prepare_connection(conn):
    conn.enable_load_extension(True)
    sqlite_tg.load(conn)
    conn.enable_load_extension(False)
