set shell := ["sh", "-eu", "-c"]

CC := env_var_or_default("CC", "gcc")
CXX := env_var_or_default("CXX", "g++")
CFLAGS := env_var_or_default("CFLAGS", "")
STRICT := "-std=c89 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition -Wundef -Wshadow -Wformat=2 -Wno-long-long"
POSIX := "-D_POSIX_C_SOURCE=200809L"
TESTFLAGS := "-Wno-unused-function -Wno-unused-result"
WRAPS := "-Wl,--wrap=open -Wl,--wrap=fstat -Wl,--wrap=flock -Wl,--wrap=fcntl -Wl,--wrap=pread -Wl,--wrap=pwrite -Wl,--wrap=write -Wl,--wrap=writev -Wl,--wrap=ftruncate -Wl,--wrap=fdatasync -Wl,--wrap=close -Wl,--wrap=malloc"

default: build

# Build the ledger89 library against its libappend89 substrate (sibling).
build:
	@mkdir -p build/obj
	cd ../libappend89 && just build
	@for f in src/*.c; do \
	    [ -e "$f" ] || continue; \
	    name=$(basename "$f" .c); \
	    {{CC}} {{STRICT}} {{POSIX}} -Iinclude -I../libappend89/include \
	        -c "src/$name.c" -o "build/obj/$name.o" || exit 1; \
	done
	@objs=""; \
	for f in build/obj/*.o; do [ -e "$f" ] || continue; objs="$objs $f"; done; \
	rm -f build/libledger89.a; \
	ar rcs build/libledger89.a $objs

# Compile and run every ledger89 test against the append89 substrate.
test: build
	@for t in test/test_*.c; do \
	    [ -e "$t" ] || continue; \
	    name=$(basename "$t" .c); \
	    {{CC}} {{STRICT}} {{POSIX}} {{CFLAGS}} {{TESTFLAGS}} \
	        -Iinclude -I../libappend89/include -I../libappend89/test/support \
	        -o "build/$name" "$t" ../libappend89/test/fault/fault.c \
	        build/libledger89.a ../libappend89/build/libappend89.a {{WRAPS}} || exit 1; \
	    ./build/$name || exit 1; \
	done
	{{CXX}} -std=c++23 -pedantic-errors -Wall -Wextra -Werror -Iinclude \
	    test/header.cpp build/libledger89.a ../libappend89/build/libappend89.a \
	    -o build/header-cpp
	./build/header-cpp

sanitize:
	CFLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all" just test

# Build the ledger89 record CLI (tool + library + substrate).
tool: build
	mkdir -p build
	{{CC}} {{STRICT}} {{POSIX}} -Iinclude -Itool -I../libappend89/include \
	    -o build/ledger89 tool/ledger89.c tool/ledger89_cli.c \
	    build/libledger89.a ../libappend89/build/libappend89.a

# CLI suite: pure-helper unit tests and end-to-end checks.
cli: tool
	mkdir -p build/tmp
	{{CC}} {{STRICT}} {{POSIX}} {{TESTFLAGS}} -Iinclude -Itool -I../libappend89/include \
	    -o build/test_cli test/cli/test_cli.c tool/ledger89_cli.c \
	    build/libledger89.a ../libappend89/build/libappend89.a
	./build/test_cli
	{{CC}} {{STRICT}} {{POSIX}} {{TESTFLAGS}} -Iinclude -Itool -I../libappend89/include \
	    -o build/test_reserve test/cli/test_reserve.c tool/ledger89_cli.c \
	    build/libledger89.a ../libappend89/build/libappend89.a
	./build/test_reserve
	sh scripts/cli-check.sh ./build/ledger89

api-convention: build
	sh scripts/check-api-convention.sh --symbols --lib .

check: test api-convention cli

clean:
	rm -rf build
