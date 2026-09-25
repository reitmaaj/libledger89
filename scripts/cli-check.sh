#!/bin/sh -eu
# cli-check.sh <binary> - end-to-end checks for the ledger89 record CLI.

bin=${1:?usage: cli-check.sh <binary>}

tmp=build/tmp
mkdir -p "$tmp"
file="$tmp/ledger89-cli-$$.dat"
out="$tmp/ledger89-cli-$$.out"
err="$tmp/ledger89-cli-$$.err"
expected="$tmp/ledger89-cli-$$.expected"
rm -f "$file" "$out" "$err" "$expected"

fail()
{
    echo "ledger89-cli: FAIL: $1" >&2
    exit 1
}

expect_exit()
{
    want=$1
    shift
    rc=0
    "$@" >/dev/null 2>"$err" || rc=$?
    [ "$rc" = "$want" ] || fail "expected exit $want, got $rc: $*"
}

# init creates a valid ledger; a second init fails (EEXIST).
"$bin" init "$file" || fail "init"
expect_exit 1 "$bin" init "$file"

# append reads all of stdin as one record and prints its offset.
printf 'hello' | "$bin" append "$file" >"$out" || fail "append"
off=$(cat "$out")
[ -n "$off" ] || fail "append printed no offset"
[ "$off" = 16 ] || fail "first record offset, got $off"

# read returns only the payload bytes.
"$bin" read "$file" "$off" >"$out" || fail "read"
[ "$(cat "$out")" = hello ] || fail "read round-trip"

# append a second record and scan: OFFSET<TAB>LENGTH per record.
printf 'world' | "$bin" append "$file" >"$out" || fail "append 2"
"$bin" scan "$file" >"$out" || fail "scan"
printf '16\t5\n45\t5\n' >"$expected"
cmp "$out" "$expected" || fail "scan output"

# binary payload survives a round-trip (NUL included).
printf 'a\0b' | "$bin" append "$file" >"$out" || fail "append binary"
off=$(cat "$out")
"$bin" read "$file" "$off" >"$out" || fail "read binary"
[ "$(wc -c <"$out" | tr -d ' ')" = 3 ] || fail "binary length"
[ "$(od -An -tx1 "$out" | tr -d ' \n')" = 610062 ] || fail "binary content"

# check and repair succeed on a clean ledger.
"$bin" check "$file" || fail "check clean"
"$bin" repair "$file" || fail "repair clean"

# tail follows from the current end: existing records are not replayed.
"$bin" tail "$file" >"$out" 2>"$err" &
tail_pid=$!
sleep 1
printf 'ZZ' | "$bin" append "$file" >/dev/null || fail "append for tail"
sleep 1
kill "$tail_pid" 2>/dev/null || true
wait "$tail_pid" 2>/dev/null || true
[ "$(cat "$out")" = ZZ ] || fail "tail output"

# usage errors exit 2.
expect_exit 2 "$bin"
expect_exit 2 "$bin" bogus "$file"
expect_exit 2 "$bin" read "$file"
expect_exit 2 "$bin" read "$file" nope
expect_exit 2 "$bin" tail

# runtime and integrity errors exit 1.
expect_exit 1 "$bin" read "$tmp/ledger89-cli-missing-$$" 16
expect_exit 1 "$bin" check "$tmp/ledger89-cli-missing-$$"
expect_exit 1 "$bin" append "$tmp"
expect_exit 1 "$bin" read "$file" 0
expect_exit 1 "$bin" tail "$tmp/ledger89-cli-missing-$$"

rm -f "$file" "$out" "$err" "$expected"
echo "ledger89-cli: ok"
