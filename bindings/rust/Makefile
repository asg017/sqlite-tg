VERSION=$(shell cat ../../VERSION)

deps: Cargo.toml sqlite-tg.c sqlite-tg.h tg.c tg.h sqlite3ext.h sqlite3.h

Cargo.toml: ../../VERSION Cargo.toml.tmpl
		VERSION=$(VERSION) envsubst < Cargo.toml.tmpl > $@

sqlite-tg.c: ../../sqlite-tg.c
		cp $< $@

sqlite-tg.h: ../../sqlite-tg.h
		cp $< $@

tg.c: ../../vendor/tg/tg.c
		cp $< $@

tg.h: ../../vendor/tg/tg.h
		cp $< $@

sqlite3ext.h: ../../vendor/sqlite/sqlite3ext.h
		cp $< $@

sqlite3.h: ../../vendor/sqlite/sqlite3.h
		cp $< $@

.PHONY: deps
