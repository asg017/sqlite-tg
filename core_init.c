/*
  This file is appended to the end of a sqlite3.c amalgammation
  file to include sqlite3-tg functions/tables statically in
  a build. This is used for the WASM implementations.
*/
#include "sqlite-tg.h"

int core_init(const char *dummy) {
  return sqlite3_auto_extension((void *)sqlite3_tg_init);
}
