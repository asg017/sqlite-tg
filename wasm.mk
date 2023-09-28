# The below is mostly borrowed from https://github.com/sql-js/sql.js/blob/master/Makefile

prefix=dist


DEFINE_SQLITE_TG_DATE=-DSQLITE_TG_DATE="\"$(DATE)\""
DEFINE_SQLITE_TG_VERSION=-DSQLITE_TG_VERSION="\"v$(VERSION)\""
DEFINE_SQLITE_TG_SOURCE=-DSQLITE_TG_SOURCE="\"$(COMMIT)\""
DEFINE_SQLITE_TG=$(DEFINE_SQLITE_TG_DATE) $(DEFINE_SQLITE_TG_VERSION) $(DEFINE_SQLITE_TG_SOURCE)


TARGET_SQLJS_JS=$(prefix)/sqljs.js
TARGET_SQLJS_WASM=$(prefix)/sqljs.wasm
TARGET_SQLJS=$(TARGET_SQLJS_JS) $(TARGET_SQLJS_WASM)

TARGET_SQLITE3_EXTRA_C=$(prefix)/sqlite3-extra.c

$(TARGET_SQLITE3_EXTRA_C): vendor/sqlite/sqlite3.c core_init.c
	cat vendor/sqlite/sqlite3.c core_init.c > $@


all: $(TARGET_SQLJS)

SQLJS_CFLAGS = \
	-O2 \
	-DSQLITE_OMIT_LOAD_EXTENSION \
	-DSQLITE_DISABLE_LFS \
	-DSQLITE_ENABLE_JSON1 \
	-DSQLITE_ENABLE_RTREE \
	-DSQLITE_ENABLE_GEOPOLY \
	-DSQLITE_ENABLE_MATH_FUNCTIONS \
	-DSQLITE_THREADSAFE=0 \
	-DSQLITE_ENABLE_NORMALIZE \
	$(DEFINE_SQLITE_TG) \
	-DSQLITE_EXTRA_INIT=core_init

SQLJS_EMFLAGS = \
	--memory-init-file 0 \
	-s RESERVED_FUNCTION_POINTERS=64 \
	-s ALLOW_TABLE_GROWTH=1 \
	-s EXPORTED_FUNCTIONS=@bindings/wasm/exported_functions.json \
	-s EXPORTED_RUNTIME_METHODS=@bindings/wasm/exported_runtime_methods.json \
	-s SINGLE_FILE=0 \
	-s NODEJS_CATCH_EXIT=0 \
	-s NODEJS_CATCH_REJECTION=0 \
	-s LLD_REPORT_UNDEFINED \
	-s LEGACY_RUNTIME=1

SQLJS_EMFLAGS_WASM = \
	-s WASM=1 \
	-s ALLOW_MEMORY_GROWTH=1

SQLJS_EMFLAGS_OPTIMIZED= \
	-s INLINING_LIMIT=1 \
	-O3 \
	-flto \
	--closure 1

SQLJS_EMFLAGS_DEBUG = \
	-s INLINING_LIMIT=1 \
	-s ASSERTIONS=1 \
	-O1

$(prefix):
	mkdir -p $@

$(TARGET_SQLJS): $(prefix) $(shell find bindings/wasm/ -type f) sqlite-tg.c $(TARGET_SQLITE3_EXTRA_C)
	emcc $(SQLJS_CFLAGS) $(SQLJS_EMFLAGS) $(SQLJS_EMFLAGS_DEBUG) $(SQLJS_EMFLAGS_WASM) \
		-I./ -Ivendor/sqlite -Ivendor/tg \
		sqlite-tg.c vendor/tg/tg.c $(TARGET_SQLITE3_EXTRA_C) \
		--pre-js bindings/wasm/api.js \
		-o $(TARGET_SQLJS_JS)
	mv $(TARGET_SQLJS_JS) tmp.js
	cat bindings/wasm/shell-pre.js tmp.js bindings/wasm/shell-post.js > $(TARGET_SQLJS_JS)
	rm tmp.js


