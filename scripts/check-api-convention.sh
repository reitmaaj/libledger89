#!/bin/sh -eu
# check-api-convention.sh - enforce the eightynine API convention (API.md).
#
# Usage:
#   check-api-convention.sh                    source-level checks
#   check-api-convention.sh --symbols          also audit built archives (nm)
#   check-api-convention.sh --lib DIR          restrict the audit to one library
#   check-api-convention.sh --symbols --lib DIR
#   check-api-convention.sh --conf FILE        use an alternate configuration
#
# Exits nonzero on the first violation set, printing every violation it found.

set -eu

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
CONF="$ROOT/scripts/api-convention.conf"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
SYMBOLS=0
LIB=""
while [ $# -gt 0 ]; do
    case "$1" in
        --symbols) SYMBOLS=1 ;;
        --lib)
            shift
            if [ $# -eq 0 ]; then
                printf 'check-api-convention: --lib needs a directory\n' >&2
                exit 2
            fi
            LIB=$1
            ;;
        --conf)
            shift
            if [ $# -eq 0 ]; then
                printf 'check-api-convention: --conf needs a file\n' >&2
                exit 2
            fi
            CONF=$1
            ;;
        *)
            printf 'check-api-convention: unknown argument: %s\n' "$1" >&2
            exit 2
            ;;
    esac
    shift
done

if [ ! -f "$CONF" ]; then
    printf 'check-api-convention: configuration not found: %s\n' "$CONF" >&2
    exit 2
fi

fail=0

report()
{
    printf 'api-convention: %s\n' "$1" >&2
    fail=1
}

# Identifiers named in a public header are public by definition, even when a
# declaration line is a function-pointer member or a type name.
drop_public()
{
    dir=$1
    prefix=$2
    ids=$3
    grep -rhoE "\\b${prefix}_[A-Za-z0-9_]+" "$ROOT/$dir/include" 2>/dev/null |
        sort -u > "$TMP/public-ids" || true
    if [ ! -s "$TMP/public-ids" ]; then
        printf '%s\n' "$ids"
        return
    fi
    printf '%s\n' "$ids" | grep -vxF -f "$TMP/public-ids" || true
}

# Audit one include/<prefix>/<name>.h extension header (hierarchical layout).
check_named_header()
{
    dir=$1
    prefix=$2
    h=$3
    family=$4
    base=$(basename "$h")
    name=${base%.h}

    case "$name" in
        priv|test)
            report "$dir: include/$prefix/$base is a private/test name"
            return
            ;;
    esac

    if [ "$name" != extension ]; then
        if ! grep -qE "#[[:space:]]*include[[:space:]]*[<\"]${prefix}\.h[>\"]" "$h"; then
            report "$dir: include/$prefix/$base does not include $prefix.h"
        fi
    fi

    bad=$(grep -nE "${prefix}_(priv|test)_" "$h" || true)
    if [ -n "$bad" ]; then
        report "$dir: include/$prefix/$base exposes priv/test identifiers"
        printf '%s\n' "$bad" >&2
    fi

    sib=$(grep -oE "#[[:space:]]*include[[:space:]]*[<\"]${prefix}/[A-Za-z0-9_]+\.h[>\"]" "$h" || true)
    refs=$(printf '%s\n' "$sib" | sed -E 's#.*[<"]([^>"]+)[>"].*#\1#')
    for ref in $refs; do
        ref=${ref##*/}
        [ "$ref" = "$base" ] && continue
        refname=${ref%.h}
        if [ -n "$family" ] && printf '%s' "$family" | tr ',' '\n' |
            grep -qxF "${name}>${refname}"; then
            continue
        fi
        report "$dir: include/$prefix/$base includes sibling $ref"
    done

    if [ "$name" = extension ]; then
        allow="${prefix}_extension_"
    else
        allow="${prefix}_${name}_"
    fi
    ids=$(grep -oE "\\b${prefix}_[A-Za-z0-9_]+" "$h" | sort -u || true)
    bad=$(printf '%s\n' "$ids" | grep -ivE "^(${allow}|${prefix}_${name}$)" || true)
    if [ -n "$bad" ]; then
        grep -rhoE "\\b${prefix}_[A-Za-z0-9_]+" "$ROOT/$dir/include/${prefix}.h" 2>/dev/null |
            sort -u > "$TMP/core-ids" || true
        for dep in $(printf '%s' "$family" | tr ',' '\n' |
            sed -n "s/^${name}>//p"); do
            if [ -f "$ROOT/$dir/include/$prefix/$dep.h" ]; then
                grep -rhoE "\\b${prefix}_[A-Za-z0-9_]+" \
                    "$ROOT/$dir/include/$prefix/$dep.h" 2>/dev/null >> "$TMP/core-ids" || true
            fi
        done
        sort -u "$TMP/core-ids" -o "$TMP/core-ids"
        if [ -s "$TMP/core-ids" ]; then
            bad=$(printf '%s\n' "$bad" | grep -vxF -f "$TMP/core-ids" || true)
        fi
    fi
    if [ -n "$bad" ]; then
        report "$dir: include/$prefix/$base declares non-extension identifiers"
        printf '%s\n' "$bad" >&2
    fi
}

check_headers()
{
    dir=$1
    prefix=$2
    ext=$3
    extra=$4
    layout=$5
    family=$6
    inc="$ROOT/$dir/include"
    core="$inc/$prefix.h"
    extf="$inc/${prefix}_ext.h"
    hier="$inc/$prefix"

    if [ ! -d "$inc" ]; then
        report "$dir: include/ missing"
        return
    fi
    if [ ! -f "$core" ]; then
        report "$dir: include/$prefix.h missing"
    fi

    for h in "$inc"/*.h; do
        [ -e "$h" ] || continue
        base=$(basename "$h")
        case "$base" in
            "$prefix.h") ;;
            "${prefix}_ext.h")
                if [ "$ext" != yes ]; then
                    report "$dir: unexpected public header $base"
                fi
                ;;
            *)
                allowed=0
                for x in $(printf '%s' "$extra" | tr ',' ' '); do
                    if [ "$x" = "$base" ]; then
                        allowed=1
                    fi
                done
                if [ "$allowed" != 1 ]; then
                    report "$dir: unexpected public header $base"
                fi
                ;;
        esac
    done

    if [ -f "$core" ]; then
        bad=$(grep -nE "${prefix}_(ext|priv|test)_|test_limit" "$core" || true)
        if [ -n "$bad" ]; then
            report "$dir: include/$prefix.h exposes a non-public class"
            printf '%s\n' "$bad" >&2
        fi
        bad=$(grep -nE "#[[:space:]]*include[[:space:]]*[<\"]${prefix}(_ext|_priv|/)" "$core" || true)
        if [ -n "$bad" ]; then
            report "$dir: include/$prefix.h includes an ext/priv/extension header"
            printf '%s\n' "$bad" >&2
        fi
    fi

    if [ "$ext" = yes ] && [ ! -f "$extf" ] && [ ! -d "$hier" ]; then
        report "$dir: include/${prefix}_ext.h missing"
    fi
    if [ -f "$extf" ]; then
        if ! grep -qE "#[[:space:]]*include[[:space:]]*[<\"]${prefix}\.h[>\"]" "$extf"; then
            report "$dir: include/${prefix}_ext.h does not include $prefix.h"
        fi
        bad=$(grep -oE "${prefix}_[a-z0-9_]+" "$extf" | grep -vE "^${prefix}_ext(_|$)" | sort -u || true)
        if [ -n "$bad" ]; then
            grep -rhoE "\\b${prefix}_[A-Za-z0-9_]+" "$ROOT/$dir/include/${prefix}.h" 2>/dev/null |
                sort -u > "$TMP/core-ids" || true
            if [ -s "$TMP/core-ids" ]; then
                bad=$(printf '%s\n' "$bad" | grep -vxF -f "$TMP/core-ids" || true)
            fi
        fi
        if [ -n "$bad" ]; then
            report "$dir: include/${prefix}_ext.h declares non-ext identifiers"
            printf '%s\n' "$bad" >&2
        fi
        bad=$(grep -nE "${prefix}_(priv|test)_" "$extf" || true)
        if [ -n "$bad" ]; then
            report "$dir: include/${prefix}_ext.h exposes priv/test identifiers"
            printf '%s\n' "$bad" >&2
        fi
    fi

    if [ -d "$hier" ]; then
        for h in "$hier"/*.h; do
            [ -e "$h" ] || continue
            check_named_header "$dir" "$prefix" "$h" "$family"
        done
    fi

    case "$layout" in
        flat)
            if [ -d "$hier" ]; then
                report "$dir: hierarchical headers present but layout=flat"
            fi
            ;;
        hier)
            if [ -f "$extf" ]; then
                report "$dir: flat ${prefix}_ext.h present but layout=hier"
            fi
            ;;
    esac
}

check_priv()
{
    dir=$1
    prefix=$2
    priv=$3
    ph="$ROOT/$dir/src/${prefix}_priv.h"

    if [ "$priv" = yes ] && [ ! -f "$ph" ]; then
        report "$dir: src/${prefix}_priv.h missing"
    fi
    if [ -f "$ph" ]; then
        bad=$(grep -oE "${prefix}_[a-z0-9_]+[[:space:]]*\(" "$ph" |
            sed 's/[[:space:]]*($//' |
            grep -vE "^${prefix}_priv_" | sort -u || true)
        bad=$(drop_public "$dir" "$prefix" "$bad")
        if [ -n "$bad" ]; then
            report "$dir: src/${prefix}_priv.h declares non-priv functions"
            printf '%s\n' "$bad" >&2
        fi
    fi
}

check_test()
{
    dir=$1
    prefix=$2
    th="$ROOT/$dir/src/${prefix}_test.h"

    if [ -f "$th" ]; then
        bad=$(grep -oE "${prefix}_[a-z0-9_]+[[:space:]]*\(" "$th" |
            sed 's/[[:space:]]*($//' |
            grep -vE "^${prefix}_test_" | sort -u || true)
        bad=$(drop_public "$dir" "$prefix" "$bad")
        if [ -n "$bad" ]; then
            report "$dir: src/${prefix}_test.h declares non-test functions"
            printf '%s\n' "$bad" >&2
        fi
    fi
}

check_legacy()
{
    dir=$1
    legacy=$2

    if [ "$legacy" = - ]; then
        return
    fi
    bad=$(grep -rnE "\\b(${legacy})" "$ROOT/$dir/src" "$ROOT/$dir/include" \
        "$ROOT/$dir/test" 2>/dev/null || true)
    if [ -n "$bad" ]; then
        report "$dir: legacy private dialect remains"
        printf '%s\n' "$bad" >&2
    fi
}

check_sources()
{
    dir=$1
    prefix=$2

    for f in "$ROOT/$dir"/src/*.c; do
        [ -e "$f" ] || continue
        base=$(basename "$f")
        case "$base" in
            "$prefix.c"|"$prefix"_*.c) ;;
            *) report "$dir: source file $base lacks the $prefix prefix" ;;
        esac
    done

    nested=$(find "$ROOT/$dir/src" -mindepth 2 -type f -name '*.c' || true)
    if [ -n "$nested" ]; then
        report "$dir: nested source files outside src/:"
        printf '%s\n' "$nested" >&2
    fi

    internal=$(find "$ROOT/$dir/src" -name '*_internal.h' || true)
    if [ -n "$internal" ]; then
        report "$dir: *_internal.h remains:"
        printf '%s\n' "$internal" >&2
    fi
}

check_symbols()
{
    dir=$1
    prefix=$2
    bundled=$3
    priv=$4
    lib="$ROOT/$dir/build/lib${prefix}.a"

    if [ ! -f "$lib" ]; then
        printf 'api-convention: %s: no build/lib%s.a (symbol check skipped)\n' \
            "$dir" "$prefix"
        return
    fi

    patre="^(${prefix}_"
    for b in $(printf '%s' "$bundled" | tr ',' ' '); do
        if [ "$b" != - ]; then
            patre="$patre|${b}"
        fi
    done
    patre="$patre)"

    for s in $(nm -g --defined-only "$lib" | awk 'NF >= 3 { print $NF }' | sort -u); do
        if ! printf '%s\n' "$s" | grep -qE "$patre"; then
            report "$dir: unexpected exported symbol $s"
        fi
        case "$s" in
            *__*) report "$dir: double-underscore private dialect in $s" ;;
        esac
        case "$s" in
            *_test_*) report "$dir: test hook exported from production archive: $s" ;;
        esac
        if [ "$priv" != yes ]; then
            case "$s" in
                "${prefix}_priv_"*)
                    report "$dir: private symbol exported with priv=no: $s"
                    ;;
            esac
        fi
    done
}

matched=0
while read -r dir prefix ext priv legacy bundled extra layout family; do
    case "$dir" in
        ''|\#*) continue ;;
    esac
    if [ -n "$LIB" ] && [ "$dir" != "$LIB" ]; then
        continue
    fi
    matched=1
    if [ ! -d "$ROOT/$dir" ]; then
        report "$dir: configured library directory missing"
        continue
    fi
    check_headers "$dir" "$prefix" "$ext" "$extra" "$layout" "$family"
    check_priv "$dir" "$prefix" "$priv"
    check_test "$dir" "$prefix"
    check_legacy "$dir" "$legacy"
    check_sources "$dir" "$prefix"
    if [ "$SYMBOLS" = 1 ]; then
        check_symbols "$dir" "$prefix" "$bundled" "$priv"
    fi
done < "$CONF"

if [ -n "$LIB" ] && [ "$matched" -eq 0 ]; then
    report "$LIB: not configured in api-convention.conf"
fi

if [ "$fail" -ne 0 ]; then
    printf 'api-convention: FAILED\n' >&2
    exit 1
fi
printf 'api-convention: ok\n'
