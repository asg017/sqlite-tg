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


DEFINE_SQLITE_TG_DATE=-DSQLITE_TG_DATE="\"$(DATE)\""
DEFINE_SQLITE_TG_VERSION=-DSQLITE_TG_VERSION="\"v$(VERSION)\""
DEFINE_SQLITE_TG_SOURCE=-DSQLITE_TG_SOURCE="\"$(COMMIT)\""
DEFINE_SQLITE_TG=$(DEFINE_SQLITE_TG_DATE) $(DEFINE_SQLITE_TG_VERSION) $(DEFINE_SQLITE_TG_SOURCE)

TARGET_LOADABLE=$(prefix)/tg0.$(LOADABLE_EXTENSION)
TARGET_STATIC=$(prefix)/libsqlite_tg0.a
TARGET_STATIC_H=$(prefix)/sqlite-tg.h


loadable: $(TARGET_LOADABLE)
static: $(TARGET_STATIC)

$(TARGET_LOADABLE): sqlite-tg.c $(prefix)
	gcc -fPIC -shared \
	-Ivendor/sqlite -Ivendor/tg \
	-O3 \
	$(DEFINE_SQLITE_TG) $(CFLAGS) \
	$< vendor/tg/tg.c -o $@

$(TARGET_STATIC): sqlite-tg.c $(prefix)
	gcc -Ivendor/sqlite -Ivendor/tg $(DEFINE_SQLITE_TG) $(CFLAGS) -DSQLITE_CORE \
	-O3 -c  $< vendor/tg/tg.c -o $(prefix)/tg.o
	ar rcs $@ $(prefix)/tg.o

$(TARGET_STATIC_H): sqlite-tg.h $(prefix)
	cp $< $@


clean:
	rm -rf dist/*

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
	cp $(TARGET_LOADABLE_TG) $(INTERMEDIATE_PYPACKAGE_EXTENSION)
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

test-loadable:
	python3 tests/test-loadable.py
