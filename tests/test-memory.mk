# Build and run rules for the memory-abuse harness in tests/test-memory.c.
# Included from the root Makefile (which defines prefix, BUILD_DIR, CFLAGS);
# run from the repo root: `make test-memory` for all checkers, or one of
# test-memory-asan / test-memory-leaks / test-memory-gmalloc.

TARGET_TEST_MEMORY=$(prefix)/test-memory
TARGET_TEST_MEMORY_ASAN=$(prefix)/test-memory-asan

# Apple clang's ASan cannot do leak detection on macOS; Homebrew LLVM's can
ASAN_CC=$(shell [ -x /opt/homebrew/opt/llvm/bin/clang ] && echo /opt/homebrew/opt/llvm/bin/clang || echo clang)
ASAN_FLAGS=-g -O1 -fno-omit-frame-pointer -fsanitize=address

$(TARGET_TEST_MEMORY): tests/test-memory.c sqlite-tg.c sqlite-tg.h vendor/sqlite/sqlite3.c vendor/tg/tg.c $(prefix)
	cc \
	-Ivendor/sqlite -Ivendor/tg -I./ \
	-g -O1 \
	$(CFLAGS) \
	-DSQLITE_CORE \
	-DSQLITE_ENABLE_RTREE \
	$< vendor/sqlite/sqlite3.c sqlite-tg.c vendor/tg/tg.c -o $@

# vendored deps get ASan only; sqlite-tg.c and the harness get ASan + UBSan.
# separate objects so the big vendored files aren't recompiled on every run
$(BUILD_DIR)/sqlite3.asan.o: vendor/sqlite/sqlite3.c | $(BUILD_DIR)
	$(ASAN_CC) $(ASAN_FLAGS) -Ivendor/sqlite \
	-DSQLITE_CORE -DSQLITE_ENABLE_RTREE -DSQLITE_DEBUG \
	-c $< -o $@

$(BUILD_DIR)/tg.asan.o: vendor/tg/tg.c | $(BUILD_DIR)
	$(ASAN_CC) $(ASAN_FLAGS) -Ivendor/tg -c $< -o $@

$(TARGET_TEST_MEMORY_ASAN): tests/test-memory.c sqlite-tg.c sqlite-tg.h $(BUILD_DIR)/sqlite3.asan.o $(BUILD_DIR)/tg.asan.o $(prefix)
	$(ASAN_CC) $(ASAN_FLAGS) \
	-fsanitize=undefined -fno-sanitize-recover=undefined \
	-Ivendor/sqlite -Ivendor/tg -I./ \
	-DSQLITE_CORE -DSQLITE_ENABLE_RTREE -DSQLITE_DEBUG \
	$< sqlite-tg.c $(BUILD_DIR)/sqlite3.asan.o $(BUILD_DIR)/tg.asan.o -o $@

# all three memory checkers that work on macOS; each catches what the others miss
test-memory: test-memory-asan test-memory-leaks test-memory-gmalloc

# ASan (use-after-free, overflows) + LSan (leaks) + UBSan on our code
test-memory-asan: $(TARGET_TEST_MEMORY_ASAN)
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./$(TARGET_TEST_MEMORY_ASAN)

# malloc-history leak check with the macos leaks tool, amplified 4x.
# leaks deadlocks if its stdout is a pipe, so buffer the report through a file
test-memory-leaks: $(TARGET_TEST_MEMORY)
	@out=$$(mktemp); \
	MallocStackLogging=1 leaks -atExit -- ./$(TARGET_TEST_MEMORY) 4 > $$out 2>&1; \
	rc=$$?; cat $$out; rm -f $$out; exit $$rc

# Guard Malloc: each allocation on its own page, freed pages unmapped, so
# use-after-free and out-of-bounds reads crash at the faulting instruction.
# second pass guards the front of each allocation to catch underruns
test-memory-gmalloc: $(TARGET_TEST_MEMORY)
	DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib ./$(TARGET_TEST_MEMORY)
	DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib MALLOC_PROTECT_BEFORE=1 ./$(TARGET_TEST_MEMORY)

.PHONY: test-memory test-memory-asan test-memory-leaks test-memory-gmalloc
