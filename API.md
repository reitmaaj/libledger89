# eightynine API convention

This document is normative for every `lib*89` library in this repository.
`scripts/check-api-convention.sh` enforces it mechanically. `CONVENTIONS.md`
defines the public interface layers; this document defines the concrete,
installed shape that the checker audits. The error-model rules are `CONVENTIONS.md`
§14.

Libraries live at the repository top level (mature tier) or under
`exploratory/` (experimental tier). `scripts/api-convention.conf` names each
configured directory; the checker treats the directory column as a path.

```text
lib*89 API convention
======================

foo89_*             Essential public API.
                    The smallest natural interface defining libfoo89.

foo89_<name>_*      Named extension public API.
                    One optional capability exposed to its consumers.
                    The extension-construction API uses foo89_extension_*.

foo89_priv_*        Library-private API.
                    Intended only for cooperation between components
                    of libfoo89. It may use external C linkage and
                    carries no consumer compatibility guarantee.

foo89_test_*        Test-only hook.
                    Compiled only into test builds; never part of any
                    consumer-facing interface.

static              Translation-unit-local implementation.
                    Uses C internal linkage.
```

## Headers

```text
include/foo89.h            essential public API
include/foo89/<name>.h     optional; named extension public API
include/foo89/extension.h  optional; extension-construction API
src/foo89_priv.h           cross-TU private declarations; never installed
src/foo89_test.h           cross-TU test hooks; never installed
```

Rules:

- `include/foo89.h` declares only `foo89_*` and `FOO89_*` names. It never
  includes any `foo89/*.h` header or `foo89_priv.h`.
- `include/foo89/<name>.h` includes `foo89.h` and declares only `foo89_<name>_*`
  additions. It may use core `foo89_*` names and types.
- `include/foo89/extension.h`, when present, includes `foo89.h` and exposes
  only the controlled construction points for independently implemented
  extensions. It never includes a named extension header.
- A named extension never includes an unrelated sibling extension.
- `src/foo89_priv.h` is the single canonical declaration point for
  cross-TU private interfaces. A subsidiary `src/foo89_priv_<topic>.h` is
  permitted only where a subsystem's size demands it.
- `src/foo89_test.h` declares only `foo89_test_*` hooks.
- No public header contains test hooks or test-only fields.

Dependency direction:

```text
foo89/<name>.h     -> foo89.h
foo89/extension.h  -> foo89.h
foo89_priv.h       -> whichever public headers its implementation needs
foo89.h            -> never foo89/*.h or foo89_priv.h
```

`include/foo89_ext.h` is the pre-migration flat spelling of
`include/foo89/<name>.h`. It remains accepted only while a library is being
converted; see the transition note under Verification.

## Names

The class prefixes apply to every identifier: functions, types, struct tags,
typedefs, enums, macros and variables.

```c
/* essential public */
typedef struct foo89 foo89;
int foo89_open(void);
#define FOO89_OK 0

/* named extension */
typedef struct foo89_codec foo89_codec;
int foo89_codec_open(foo89_codec **out);

/* extension construction */
int foo89_extension_register(void);

/* library-private */
struct foo89_priv_node;
int foo89_priv_scan(struct foo89_priv_node *node);
#define FOO89_PRIV_BUCKETS 256

/* test-only */
void foo89_test_fail_alloc_at(unsigned long n);
```

`priv` expresses intended use only. It makes no statement about C linkage,
object-file visibility, or language-enforced access. A `foo89_priv_*`
function legitimately has external linkage when multiple translation units
of `libfoo89` use it.

A helper used in exactly one `.c` file is `static` and carries no class
prefix requirement beyond ordinary project style.

## Errors

`CONVENTIONS.md` §14 is normative. In summary:

- The operational return type is `foo89_status`; reserve `result` for a value
  produced by an operation and `error` for diagnostic information or a domain
  object that represents an error.
- Every public status enumerator carries an explicit numeric value that MUST
  NOT change, be reused, or be reclassified.
- `status == 0` is success, `status > 0` is an ordinary outcome, and
  `status < 0` is a failure. A failure MUST NOT be positive; an ordinary
  outcome MUST NOT be negative.
- Each API surface declares exactly one profile: the portable status profile,
  or the POSIX syscall profile (`-1` with meaningful `errno`). Under the
  portable profile `errno` carries no public meaning. Library-specific
  conditions MUST NOT use invented integer values in the `errno` namespace.
- A library MAY provide `foo89_status_name()` and `foo89_status_message()`,
  both total over unknown integer values and allocation-free.

## Source files

```text
foo89_append.c          ordinary implementation of the library machinery
foo89_codec.c           a file that principally implements a named extension
foo89_priv_codec.c      a subsystem that principally provides a
                        cross-TU private interface
```

`priv` classifies an interface, not every line of implementation code.
Every `src/*.c` basename is `foo89.c` or `foo89_<topic>.c`, with `ext` or
`priv` inserted when the file's principal role warrants it.

## Exemptions

- `libdledger89`, `llm89` and `libbpf` are excluded from this naming
  convention. They remain subject to the `CONVENTIONS.md` §14 error-model
  rules.
- `libbpf` has no `89` suffix and is excluded from the naming convention.

## Verification

`just check-all` at this repository root builds every library, runs the
source-level convention checks, audits the built archives' exported symbol
classes, runs the error-convention check, and then runs every library's own
test suite.

The checker itself is `scripts/check-api-convention.sh`:

```text
sh scripts/check-api-convention.sh                  source-level checks
sh scripts/check-api-convention.sh --symbols        also audit built archives
sh scripts/check-api-convention.sh --lib DIR        restrict to one library
sh scripts/check-api-convention.sh --symbols --lib DIR
```

`scripts/check-error-convention.sh` audits the §14 error surface:

```text
sh scripts/check-error-convention.sh                source-level error checks
sh scripts/check-error-convention.sh --lib DIR      restrict to one library
```

Every library's `Justfile` exposes `just api-convention`, which builds the
library and runs `--symbols --lib <dir>`, and `just error-convention`. A
library must pass the source-level checks, the symbol-level checks, the
error-convention check, and its own test suite. Libraries absent from
`scripts/api-convention.conf` are exempt from the naming convention.

### Transition

The family migrated from flat `include/foo89_ext.h` (symbols
`foo89_ext_*`) to `include/foo89/<name>.h` (symbols `foo89_<name>_*`). Every
extension-bearing library is now converted; the flat spelling is no longer
accepted for a library whose `api-convention.conf` `layout` column is
`hier`.
