set shell := ["sh", "-eu", "-c"]

CC := env_var_or_default("CC", "gcc")
CXX := env_var_or_default("CXX", "g++")
CFLAGS := env_var_or_default("CFLAGS", "")
GREEN := env_var_or_default("GREEN", "../green/.agent/tmp/build/green")
STRICT := "-std=c89 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition -Wundef -Wshadow -Wformat=2 -Wno-long-long"
POSIX := "-D_POSIX_C_SOURCE=200809L"
TESTFLAGS := "-Wno-unused-function -Wno-unused-result"
INC := "-Iinclude -Isrc -I../libappend89/include -I../libappend89/test/support -I../libchecksum89/include"
WRAPS := "-Wl,--wrap=open -Wl,--wrap=fstat -Wl,--wrap=flock -Wl,--wrap=fcntl -Wl,--wrap=pread -Wl,--wrap=pwrite -Wl,--wrap=write -Wl,--wrap=writev -Wl,--wrap=ftruncate -Wl,--wrap=fdatasync -Wl,--wrap=close -Wl,--wrap=malloc"
LIBS := "build/libledger89.a ../libappend89/build/libappend89.a ../libchecksum89/build/libchecksum89.a"
FAULT := "../libappend89/test/fault/fault.c"

default: build

# Build the ledger89 library against its append89 and checksum89 substrates.
build:
	@rm -rf build/obj
	@mkdir -p build/obj
	cd ../libappend89 && just build
	cd ../libchecksum89 && just build
	@for f in src/*.c; do \
	    [ -e "$f" ] || continue; \
	    name=$(basename "$f" .c); \
	    {{CC}} {{STRICT}} {{POSIX}} {{CFLAGS}} -Iinclude -Isrc \
	        -I../libappend89/include -I../libchecksum89/include \
	        -c "src/$name.c" -o "build/obj/$name.o" || exit 1; \
	done
	@objs=""; \
	for f in build/obj/*.o; do [ -e "$f" ] || continue; objs="$objs $f"; done; \
	rm -f build/libledger89.a; \
	ar rcs build/libledger89.a $objs

# One end-to-end real-I/O path.
smoke: build
	{{CC}} {{STRICT}} {{POSIX}} {{CFLAGS}} {{TESTFLAGS}} {{INC}} \
	    -o build/test_smoke test/test_smoke.c {{FAULT}} {{LIBS}} {{WRAPS}}
	./build/test_smoke

# Build the ledger89 record CLI (tool + library + substrates).
tool: build
	mkdir -p build
	{{CC}} {{STRICT}} {{POSIX}} -Iinclude -Itool -I../libappend89/include \
	    -I../libchecksum89/include -o build/ledger89 tool/ledger89.c \
	    tool/ledger89_cli.c {{LIBS}}

# CLI suite: pure-helper unit tests and end-to-end checks.
cli: tool
	mkdir -p build/tmp
	{{CC}} {{STRICT}} {{POSIX}} {{TESTFLAGS}} -Iinclude -Itool \
	    -I../libappend89/include -I../libchecksum89/include \
	    -o build/test_cli test/cli/test_cli.c tool/ledger89_cli.c {{LIBS}}
	./build/test_cli
	sh scripts/cli-check.sh ./build/ledger89

# Compile and run every ledger89 test against the append89 + checksum89 substrates.
test: build
	@for t in test/test_*.c; do \
	    [ -e "$t" ] || continue; \
	    name=$(basename "$t" .c); \
	    {{CC}} {{STRICT}} {{POSIX}} {{CFLAGS}} {{TESTFLAGS}} {{INC}} \
	        -o "build/$name" "$t" {{FAULT}} {{LIBS}} {{WRAPS}} || exit 1; \
	    ./build/$name || exit 1; \
	done
	{{CXX}} -std=c++23 -pedantic-errors -Wall -Wextra -Werror {{CFLAGS}} -Iinclude \
	    test/header.cpp {{LIBS}} -o build/header-cpp
	./build/header-cpp

sanitize:
	CFLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all" just test

# Best-effort Valgrind memcheck over the deterministic and representative
# suites. Skips cleanly when valgrind is absent.
valgrind:
	@command -v valgrind >/dev/null 2>&1 || { echo "valgrind: SKIPPED: not installed"; exit 0; }
	just build
	@for t in test/test_smoke.c test/test_unit.c test/test_api.c test/test_format.c \
	    test/test_recovery.c test/test_fault.c test/test_model.c test/test_corrupt.c; do \
	    [ -e "$t" ] || continue; \
	    name=$(basename "$t" .c); \
	    {{CC}} {{STRICT}} {{POSIX}} {{TESTFLAGS}} {{INC}} \
	        -o "build/$name" "$t" {{FAULT}} {{LIBS}} {{WRAPS}} || exit 1; \
	    valgrind --error-exitcode=1 --leak-check=full ./build/$name || exit 1; \
	done

# Full nightly gate: strict build, every suite, sanitizers, and Valgrind.
nightly: test sanitize valgrind

api-convention: build
	sh scripts/check-api-convention.sh --symbols --lib .

# Generate compile databases and run the green matrix.
green:
	sh scripts/gen_compile_db gcc build/gcc/compile_commands.json
	sh scripts/gen_compile_db clang build/clang/compile_commands.json
	cd . && {{GREEN}} check

check: green test api-convention cli

clean:
	rm -rf build
