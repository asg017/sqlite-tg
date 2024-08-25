COMMIT=$(shell git rev-parse HEAD)
VERSION=$(shell cat VERSION)
DATE=$(shell date +'%FT%TZ%z')


ifeq ($(shell uname -s),Darwin)
CONFIG_DARWIN=y
else ifeq ($(OS),Windows_NT)
CONFIG_WINDOWS=y
else
CONFIG_LINUX=y
endif

ifdef CONFIG_DARWIN
LOADABLE_EXTENSION=dylib
endif

ifdef CONFIG_LINUX
LOADABLE_EXTENSION=so
endif

ifdef CONFIG_WINDOWS
LOADABLE_EXTENSION=dll
endif


ifdef python
PYTHON=$(python)
else
PYTHON=python3
endif


ifdef IS_MACOS_ARM
RENAME_WHEELS_ARGS=--is-macos-arm
else
RENAME_WHEELS_ARGS=
endif

prefix=dist
$(prefix):
	mkdir -p $(prefix)

TARGET_LOADABLE=$(prefix)/tg0.$(LOADABLE_EXTENSION)
TARGET_STATIC=$(prefix)/libsqlite_tg0.a
TARGET_STATIC_H=$(prefix)/sqlite-tg.h

TARGET_TEST_MEMORY=$(prefix)/test-memory

loadable: $(TARGET_LOADABLE)
static: $(TARGET_STATIC)
test-memory: $(TARGET_TEST_MEMORY)

BUILD_DIR=$(prefix)/.build

$(BUILD_DIR): $(prefix)
	mkdir -p $@


$(TARGET_LOADABLE): sqlite-tg.c sqlite-tg.h vendor/tg/tg.c $(prefix)
	gcc -fPIC -shared \
	-Ivendor/sqlite -Ivendor/tg \
	-O3 \
	$(CFLAGS) \
	$< vendor/tg/tg.c -o $@

$(TARGET_STATIC): sqlite-tg.c sqlite-tg.h $(prefix)
	gcc -Ivendor/sqlite -Ivendor/tg $(CFLAGS) -DSQLITE_CORE \
	-O3 -c  $< vendor/tg/tg.c -o $(prefix)/tg.o
	ar rcs $@ $(prefix)/tg.o

sqlite-tg.h: sqlite-tg.h.tmpl VERSION
	VERSION=$(shell cat VERSION) DATE=$(shell date -r VERSION +'%FT%TZ%z') SOURCE=$(shell git log -n 1 --pretty=format:%H -- VERSION) envsubst < $< > $@

$(TARGET_STATIC_H): sqlite-tg.h $(prefix)
	cp $< $@

$(TARGET_TEST_MEMORY): tests/test-memory.c sqlite-tg.c vendor/tg/tg.c $(prefix)
	gcc \
	-Ivendor/sqlite -Ivendor/tg -I./ \
	-O3 \
	$(CFLAGS) \
	-lsqlite3 \
	-DSQLITE_CORE \
	$< sqlite-tg.c vendor/tg/tg.c -o $@

clean:
	rm -rf dist/*


FORMAT_FILES=sqlite-tg.h sqlite-tg.c
format: $(FORMAT_FILES)
	clang-format -i $(FORMAT_FILES)
	black tests/test-loadable.py

lint: SHELL:=/bin/bash
lint:
	diff -u <(cat $(FORMAT_FILES)) <(clang-format $(FORMAT_FILES))

test:
	sqlite3 :memory: '.read test.sql'

.PHONY: version loadable static test clean gh-release

publish-release:
	./scripts/publish_release.sh


test-loadable: loadable
	$(PYTHON) -m pytest -vv tests/test-loadable.py

test-loadable-snapshot-update: loadable
	$(PYTHON) -m pytest -vv tests/test-loadable.py --snapshot-update

test-loadable-watch:
	watchexec -w sqlite-tg.c -w tests/test-loadable.py -w docs.md -w Makefile --clear -- make test-loadable




# ███████████████████████████████ WASM SECTION ███████████████████████████████

WASM_DIR=$(prefix)/.wasm

$(WASM_DIR): $(prefix)
	mkdir -p $@

SQLITE_WASM_VERSION=3450300
SQLITE_WASM_YEAR=2024
SQLITE_WASM_SRCZIP=$(BUILD_DIR)/sqlite-src.zip
SQLITE_WASM_COMPILED_SQLITE3C=$(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/sqlite3.c
SQLITE_WASM_COMPILED_MJS=$(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm/jswasm/sqlite3.mjs
SQLITE_WASM_COMPILED_WASM=$(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm/jswasm/sqlite3.wasm

TARGET_WASM_LIB=$(WASM_DIR)/libsqlite_tg.wasm.a
TARGET_WASM_MJS=$(WASM_DIR)/sqlite3.mjs
TARGET_WASM_WASM=$(WASM_DIR)/sqlite3.wasm
TARGET_WASM=$(TARGET_WASM_MJS) $(TARGET_WASM_WASM)

$(SQLITE_WASM_SRCZIP): $(BUILD_DIR)
	curl -o $@ https://www.sqlite.org/$(SQLITE_WASM_YEAR)/sqlite-src-$(SQLITE_WASM_VERSION).zip
	touch $@

$(SQLITE_WASM_COMPILED_SQLITE3C): $(SQLITE_WASM_SRCZIP) $(BUILD_DIR)
	rm -rf $(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ || true
	unzip -q -o $< -d $(BUILD_DIR)
	(cd $(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ && ./configure --enable-all && make sqlite3.c)
	touch $@

$(TARGET_WASM_LIB): examples/wasm/wasm.c sqlite-tg.c $(BUILD_DIR) $(WASM_DIR)
	emcc -O3  -I./ -Ivendor/sqlite -DSQLITE_CORE -c examples/wasm/wasm.c -o $(BUILD_DIR)/wasm.wasm.o
	emcc -O3  -I./ -Ivendor/tg -c vendor/tg/tg.c -o $(BUILD_DIR)/tg.wasm.o
	emcc -O3  -I./ -Ivendor/sqlite -Ivendor/tg -DSQLITE_CORE -c sqlite-tg.c -o $(BUILD_DIR)/sqlite-tg.wasm.o
	emar rcs $@ $(BUILD_DIR)/wasm.wasm.o $(BUILD_DIR)/sqlite-tg.wasm.o $(BUILD_DIR)/tg.wasm.o

$(SQLITE_WASM_COMPILED_MJS) $(SQLITE_WASM_COMPILED_WASM): $(SQLITE_WASM_COMPILED_SQLITE3C) $(TARGET_WASM_LIB)
	(cd $(BUILD_DIR)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm && \
		make sqlite3_wasm_extra_init.c=../../../../.wasm/libsqlite_tg.wasm.a jswasm/sqlite3.mjs jswasm/sqlite3.wasm \
	)

$(TARGET_WASM_MJS): $(SQLITE_WASM_COMPILED_MJS)
	cp $< $@

$(TARGET_WASM_WASM): $(SQLITE_WASM_COMPILED_WASM)
	cp $< $@

wasm: $(TARGET_WASM)

# ███████████████████████████████   END WASM   ███████████████████████████████
