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

.PHONY: version loadable static test clean gh-release \
	ruby

publish-release:
	./scripts/publish_release.sh

TARGET_WHEELS=$(prefix)/wheels
INTERMEDIATE_PYPACKAGE_EXTENSION=bindings/python/sqlite_tg/

$(TARGET_WHEELS): $(prefix)
	mkdir -p $(TARGET_WHEELS)

bindings/ruby/lib/version.rb: bindings/ruby/lib/version.rb.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@

bindings/python/sqlite_tg/version.py: bindings/python/sqlite_tg/version.py.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@
	echo "✅ generated $@"

bindings/datasette/datasette_sqlite_tg/version.py: bindings/datasette/datasette_sqlite_tg/version.py.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@
	echo "✅ generated $@"

python: $(TARGET_WHEELS) $(TARGET_LOADABLE) bindings/python/setup.py bindings/python/sqlite_tg/__init__.py scripts/rename-wheels.py
	cp $(TARGET_LOADABLE) $(INTERMEDIATE_PYPACKAGE_EXTENSION)
	rm $(TARGET_WHEELS)/*.wheel || true
	pip3 wheel bindings/python/ -w $(TARGET_WHEELS)
	python3 scripts/rename-wheels.py $(TARGET_WHEELS) $(RENAME_WHEELS_ARGS)
	echo "✅ generated python wheel"

datasette: $(TARGET_WHEELS) bindings/datasette/setup.py bindings/datasette/datasette_sqlite_tg/__init__.py
	rm $(TARGET_WHEELS)/datasette* || true
	pip3 wheel bindings/datasette/ --no-deps -w $(TARGET_WHEELS)

bindings/sqlite-utils/pyproject.toml: bindings/sqlite-utils/pyproject.toml.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@
	echo "✅ generated $@"

bindings/sqlite-utils/sqlite_utils_sqlite_tg/version.py: bindings/sqlite-utils/sqlite_utils_sqlite_tg/version.py.tmpl VERSION
	VERSION=$(VERSION) envsubst < $< > $@
	echo "✅ generated $@"

sqlite-utils: $(TARGET_WHEELS) bindings/sqlite-utils/pyproject.toml bindings/sqlite-utils/sqlite_utils_sqlite_tg/version.py
	python3 -m build bindings/sqlite-utils -w -o $(TARGET_WHEELS)

node: VERSION bindings/node/platform-package.README.md.tmpl bindings/node/platform-package.package.json.tmpl bindings/node/sqlite-tg/package.json.tmpl scripts/node_generate_platform_packages.sh
	scripts/node_generate_platform_packages.sh

deno: VERSION bindings/deno/deno.json.tmpl
	scripts/deno_generate_package.sh


version:
	make bindings/ruby/lib/version.rb
	make bindings/python/sqlite_tg/version.py
	make bindings/datasette/datasette_sqlite_tg/version.py
	make bindings/datasette/datasette_sqlite_tg/version.py
	make bindings/sqlite-utils/pyproject.toml bindings/sqlite-utils/sqlite_utils_sqlite_tg/version.py
	make node
	make deno

test-loadable: loadable
	$(PYTHON) -m pytest -vv tests/test-loadable.py

test-loadable-snapshot-update: loadable
	$(PYTHON) -m pytest -vv tests/test-loadable.py --snapshot-update

test-loadable-watch:
	watchexec -w sqlite-tg.c -w tests/test-loadable.py -w docs.md -w Makefile --clear -- make test-loadable



# ███████████████████████████████ WASM SECTION ███████████████████████████████

SQLITE_WASM_VERSION=3440000
SQLITE_WASM_YEAR=2023
SQLITE_WASM_SRCZIP=$(prefix)/sqlite-src.zip
SQLITE_WASM_COMPILED_SQLITE3C=$(prefix)/sqlite-src-$(SQLITE_WASM_VERSION)/sqlite3.c
SQLITE_WASM_COMPILED_MJS=$(prefix)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm/jswasm/sqlite3.mjs
SQLITE_WASM_COMPILED_WASM=$(prefix)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm/jswasm/sqlite3.wasm

TARGET_WASM_LIB=$(prefix)/libsqlite_tg.wasm.a
TARGET_WASM_MJS=$(prefix)/sqlite3.mjs
TARGET_WASM_WASM=$(prefix)/sqlite3.wasm
TARGET_WASM=$(TARGET_WASM_MJS) $(TARGET_WASM_WASM)

$(SQLITE_WASM_SRCZIP):
	curl -o $@ https://www.sqlite.org/$(SQLITE_WASM_YEAR)/sqlite-src-$(SQLITE_WASM_VERSION).zip

$(SQLITE_WASM_COMPILED_SQLITE3C): $(SQLITE_WASM_SRCZIP)
	unzip -q -o $< -d $(prefix)
	(cd $(prefix)/sqlite-src-$(SQLITE_WASM_VERSION)/ && ./configure --enable-all && make sqlite3.c)

$(TARGET_WASM_LIB): examples/wasm/wasm.c sqlite-tg.c vendor/tg/tg.c
	emcc -O3  -I./ -Ivendor/sqlite -Ivendor/tg $(CFLAGS) -DSQLITE_CORE -c examples/wasm/wasm.c -o $(prefix)/wasm.wasm.o
	emcc -O3  -I./ -Ivendor/sqlite -Ivendor/tg $(CFLAGS) -DSQLITE_CORE -c sqlite-tg.c -o $(prefix)/sqlite-tg.wasm.o
	emcc -O3  -I./ -Ivendor/sqlite -Ivendor/tg $(CFLAGS) -DSQLITE_CORE -c vendor/tg/tg.c  -o $(prefix)/tg.wasm.o
	emar rcs $@ $(prefix)/tg.wasm.o $(prefix)/wasm.wasm.o $(prefix)/sqlite-tg.wasm.o

$(SQLITE_WASM_COMPILED_MJS) $(SQLITE_WASM_COMPILED_WASM): $(SQLITE_WASM_COMPILED_SQLITE3C) $(TARGET_WASM_LIB)
	(cd $(prefix)/sqlite-src-$(SQLITE_WASM_VERSION)/ext/wasm && \
		make sqlite3_wasm_extra_init.c=../../../libsqlite_tg.wasm.a "emcc.flags=-s EXTRA_EXPORTED_RUNTIME_METHODS=['ENV'] -s FETCH")

$(TARGET_WASM_MJS): $(SQLITE_WASM_COMPILED_MJS)
	cp $< $@

$(TARGET_WASM_WASM): $(SQLITE_WASM_COMPILED_WASM)
	cp $< $@

wasm: $(TARGET_WASM)

# ███████████████████████████████   END WASM   ███████████████████████████████


# ███████████████████████████████ SITE SECTION ███████████████████████████████

WASM_TOOLKIT_NPM_TGZ=$(prefix)/sqlite-wasm-toolkit-npm.tgz

TARGET_SITE_DIR=$(prefix)/site
TARGET_SITE=$(prefix)/site/index.html

$(WASM_TOOLKIT_NPM_TGZ):
	curl -o $@ -q https://registry.npmjs.org/@alex.garcia/sqlite-wasm-toolkit/-/sqlite-wasm-toolkit-0.0.1-alpha.7.tgz

$(TARGET_SITE_DIR)/slim.js $(TARGET_SITE_DIR)/slim.css: $(WASM_TOOLKIT_NPM_TGZ)
	tar -xvzf $< -C $(TARGET_SITE_DIR) --strip-components=2 package/dist/slim.js package/dist/slim.css


$(TARGET_SITE_DIR):
	mkdir -p $(TARGET_SITE_DIR)

$(TARGET_SITE): site/index.html $(TARGET_SITE_DIR) $(TARGET_WASM_MJS) $(TARGET_WASM_WASM) $(TARGET_SITE_DIR)/slim.js $(TARGET_SITE_DIR)/slim.css
	cp $(TARGET_WASM_MJS) $(TARGET_SITE_DIR)
	cp $(TARGET_WASM_WASM) $(TARGET_SITE_DIR)
	cp $< $@
site: $(TARGET_SITE)
# ███████████████████████████████   END SITE   ███████████████████████████████
