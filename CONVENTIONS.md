# Eightynine Conventions

This document defines project-wide interface and header conventions for the
`lib*89` libraries developed in the **eightynine** project.

The key words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY**
express requirements. A deviation from a **SHOULD** or **SHOULD NOT** requires a
documented rationale. A deviation from a **MUST** or **MUST NOT** changes the
library architecture and requires an explicit project-level decision.

## 1. Goals

Each library exposes a small, complete essentials API while permitting optional
facilities and third-party extensions without coupling ordinary users to them.

The interface structure must preserve these properties:

1. The essentials API works by itself.
2. Optional facilities depend inward on the essentials API; the essentials API
   never depends outward on them.
3. An extension's consumer interface remains distinct from the interface used
   to implement extensions.
4. Unrelated extensions remain independent.
5. Header dependencies reveal architectural dependencies rather than hiding
   them through transitive inclusion.
6. Public and private interfaces remain unambiguously separate.

## 2. Public interface layers

A library named `libfoo89` may expose three public interface layers.

| Layer | Installed header | Purpose | Audience |
| --- | --- | --- | --- |
| Essentials | `<foo89.h>` | Complete minimal expression of the library's primary abstraction | Every user |
| Extension construction | `<foo89/extension.h>` | Controlled mechanisms for implementing extensions | Extension authors |
| Named extension | `<foo89/name.h>` | Consumer API for one optional capability | Users of that capability |

Private implementation headers form a fourth, non-public layer. They MUST NOT
appear in the installed include tree.

### 2.1 Essentials API

`<foo89.h>` MUST provide a complete and usable essentials API. A program using
the core abstraction MUST NOT need any `<foo89/*.h>` header.

The essentials header:

- MUST NOT include any `<foo89/*.h>` header;
- MUST NOT expose a type whose complete or declared form requires an optional
  extension header;
- MUST NOT change its declarations according to which extensions get built or
  installed;
- MUST remain usable when the library ships with no extensions;
- SHOULD contain only operations fundamental to the core abstraction;
- MAY include standard, platform, or dependency headers required by its own
  declarations.

Small size alone does not define an essentials API. Removing an operation from
`<foo89.h>` makes sense only when the remaining interface still expresses a
coherent and useful abstraction.

### 2.2 Extension-construction API

`<foo89/extension.h>` exists only when the library defines a genuine generic
mechanism through which code outside the core implementation can add
functionality.

It MAY expose registration, lifecycle, callback, capability, or controlled
core-access interfaces required by extension implementations. It MUST NOT serve
as a miscellaneous collection of advanced operations.

An operation belongs in `<foo89/extension.h>` only when an independently
implemented extension needs it. An operation needed by an advanced application,
but not by extension implementations, does not qualify on that basis.

The extension-construction header:

- MUST include or depend only on `<foo89.h>` and legitimate external
  prerequisites;
- MUST NOT include any named extension header;
- MUST NOT expose private implementation types or permit extensions to depend
  on private object layouts;
- SHOULD present the narrowest stable surface sufficient to implement
  extensions;
- SHOULD use opaque handles, versioned operation tables, or explicit
  capability objects when binary compatibility requires them.

A library with role-specific extension points SHOULD name those points directly
instead of creating an artificial generic substrate. Examples include
`<foo89/backend.h>`, `<foo89/codec.h>`, `<foo89/store.h>`, and
`<foo89/transport.h>`.

### 2.3 Named extensions

A named extension adds an optional policy, integration, implementation, or
capability. Its consumer header uses the extension's descriptive name:

```c
#include <foo89/codec.h>
#include <foo89/index.h>
#include <foo89/verify.h>
```

A named extension header:

- MUST remain optional for essentials-only users;
- MUST NOT alter the declarations or semantics of `<foo89.h>` merely by being
  included;
- SHOULD depend directly on `<foo89.h>` when it uses core types;
- SHOULD NOT include `<foo89/extension.h>` merely because its implementation
  uses the extension-construction API;
- MUST NOT include an unrelated sibling extension;
- SHOULD expose only the declarations needed by consumers of that extension.

The implementation of an extension MAY include `<foo89/extension.h>` without
making that header part of the extension's consumer-facing dependency graph.

## 3. Installed header layout

The canonical installed layout is:

```text
include/
    foo89.h
    foo89/
        extension.h
        codec.h
        index.h
        verify.h
```

`<foo89.h>` occupies the top level because it defines the library's essential
abstraction. The `foo89/` directory groups optional and extension-author
interfaces without promoting them to the same architectural level.

The project MUST NOT flatten these names into headers such as
`foo89_codec.h` or `foo89_extension.h` unless platform constraints make
hierarchical installed headers impossible.

Headers under `foo89/` MUST NOT receive an extra `ext/` directory merely to
mark them as optional. In particular, this layout is discouraged:

```text
foo89/ext/extension.h
foo89/ext/codec.h
```

The construction mechanism and a concrete extension occupy different roles;
placing both beneath `ext/` obscures that distinction.

## 4. Dependency rules

### 4.1 Required direction

The base dependency graph is:

```text
<foo89/name.h> --------> <foo89.h>
       |
       | public dependency only when required by exposed declarations
       v
<foo89/extension.h> ---> <foo89.h>
```

The following dependencies are forbidden:

```text
<foo89.h>             -> <foo89/*.h>
<foo89/extension.h>   -> <foo89/name.h>
<foo89/a.h>           -> <foo89/b.h>       unrelated siblings
any public header     -> private header
```

An implementation dependency does not imply a public-header dependency. For
example:

```c
/* application.c: essentials only */
#include <foo89.h>

/* application_with_codec.c: codec consumer */
#include <foo89.h>
#include <foo89/codec.h>

/* codec.c: codec implementation */
#include <foo89/codec.h>
#include <foo89/extension.h>
#include "foo89_priv.h"
```

### 4.2 Direct inclusion and self-sufficiency

Every public header MUST compile when included as the first project header in a
translation unit. It MUST include every prerequisite needed for its own
declarations and MUST NOT rely on inclusion order.

Consumers SHOULD include each interface they use directly. They MUST NOT rely
on one extension header to re-export unrelated extension declarations.

Self-sufficiency does not authorize convenience aggregation. An umbrella header
that includes every extension defeats dependency isolation and MUST NOT form
part of the essentials API.

### 4.3 No configuration-dependent public graph

Build options MAY control which named extensions get built and installed. They
MUST NOT cause `<foo89.h>` to include different project headers or expose a
different core API.

Feature macros MAY select platform representations only when the resulting ABI
and source-compatibility rules are explicitly documented. They MUST NOT turn
unrelated extensions into implicit core features.

## 5. Extension families

Closely related optional interfaces MAY form an explicit extension family when
one facility genuinely builds on another. The relationship MUST appear in both
naming and directory structure.

Example:

```text
include/
    foo89.h
    foo89/
        query.h
        query/
            schema.h
            index.h
```

Permitted family dependencies include:

```text
<foo89/query/index.h>
    -> <foo89/query/schema.h>
    -> <foo89/query.h>
    -> <foo89.h>
```

An extension family MUST satisfy all of these conditions:

1. The child interfaces share one public abstraction, lifecycle, or data model.
2. The parent header remains meaningful without every child.
3. Dependencies flow from more specialized children toward the family root.
4. The family does not provide a pretext for coupling otherwise independent
   facilities.
5. Each dependency appears directly in the dependent header rather than
   arriving accidentally through an umbrella include.

Two extensions that merely work well together do not form a family. Their
integration belongs in implementation code, an adapter extension, or the
application.

## 6. C identifier namespaces

Filesystem hierarchy does not replace C symbol namespacing.

| Interface | Identifier prefix |
| --- | --- |
| Essentials | `foo89_` |
| Extension construction | `foo89_extension_` |
| Named extension | `foo89_<name>_` |
| Extension family child | `foo89_<family>_<child>_` |
| Private implementation | `foo89_priv_` |

Examples:

```c
foo89_open(...);
foo89_close(...);

foo89_extension_register(...);
foo89_extension_context(...);

foo89_codec_open(...);
foo89_codec_encode(...);

foo89_query_index_build(...);
```

The project SHOULD spell `extension` in generic public identifiers. The
abbreviation `ext` remains ambiguous between an extension-construction API and
a collection of extra functions. `spi`, `xapi`, and `addon` MUST NOT name the
generic interface:

- `spi` imports terminology uncommon in small C APIs;
- `xapi` lacks a conventional meaning;
- `addon` names a concrete optional component, not the interface used to build
  one.

Role-specific terms such as `backend`, `provider`, `driver`, `codec`,
`transport`, `hooks`, `module`, or `plugin` SHOULD replace `extension` when they
state a narrower and accurate contract. `plugin` implies component registration
or loading and SHOULD NOT describe a static callback interface lacking plugin
lifecycle semantics.

## 7. Public and private types

Public headers MUST NOT expose private structure definitions solely to let an
extension reach core state. Use opaque handles and accessors instead.

Extension-construction interfaces MUST document:

- ownership and lifetime of every object and callback argument;
- whether pointers are borrowed, retained, or transferred;
- registration and deregistration order;
- callback reentrancy and thread-safety rules;
- error propagation across the core/extension boundary;
- behavior during partial initialization and teardown;
- ABI versioning rules when separately built extensions are supported;
- which core services an extension may call from each callback.

An extension MUST NOT cast an opaque core handle to a private structure or
include files from the core's source tree.

## 8. Source and package boundaries

The source tree SHOULD make the same boundaries visible. One suitable layout is:

```text
include/
    foo89.h
    foo89/
        extension.h
        codec.h
src/
    foo89_priv.h
    foo89_core.c
extensions/
    codec/
        foo89_codec.c
tests/
```

The exact source layout MAY differ, but installed artifacts MUST preserve the
public layout defined above.

If extensions ship as separate archives or packages:

- the core package MUST remain usable without them;
- each extension package MUST declare its direct dependencies;
- installing an extension MUST NOT replace or patch `<foo89.h>`;
- separate versioning MAY apply to an extension's consumer API;
- compatibility with the core extension-construction API MUST be explicit.

Bundling an extension with the core distribution does not make it essential.
Classification follows architectural role, not packaging convenience.

## 9. API classification procedure

Classify every proposed public operation in this order:

1. **Does the operation form part of the minimal coherent core abstraction?**
   If yes, place it in `<foo89.h>`.
2. **Does independently implemented functionality need the operation in order
   to attach to or cooperate with the core?** If yes, place it in
   `<foo89/extension.h>` or a more precise role-specific provider header.
3. **Does the operation expose one optional named capability to its consumers?**
   If yes, place it in `<foo89/name.h>`.
4. **Does only the library's own implementation need it?** If yes, keep it
   private.

Complexity, rarity of use, or perceived sophistication do not by themselves
move an operation out of the essentials API. Conversely, broad usefulness does
not justify exposing an implementation hook as a consumer API.

## 10. Compatibility policy

Each public layer carries its own compatibility contract:

- `<foo89.h>` receives the strongest source and ABI stability guarantees.
- `<foo89/extension.h>` remains stable when third-party extension
  implementations form a supported use case; versioned tables or capability
  negotiation SHOULD isolate future growth.
- each `<foo89/name.h>` defines an independently reviewable public contract;
  one extension's evolution MUST NOT force unrelated extensions or
  essentials-only consumers to change.
- private interfaces carry no compatibility guarantee.

Moving a declaration between public layers breaks source compatibility even
when its C signature remains unchanged. Such a move requires the same review as
renaming or removing the declaration.

## 11. Mechanical enforcement

Every library's CI MUST compile and link an essentials-only smoke program:

```c
#include <foo89.h>

int main(void)
{
    return 0;
}
```

CI MUST also compile one translation unit per installed public header, with that
header included first:

```c
#include <foo89/codec.h>

int main(void)
{
    return 0;
}
```

The checks MUST use the library's supported C language modes and warning policy.
For strict C89 libraries, at least one check MUST compile under the project's
strict C89 configuration.

CI SHOULD additionally verify that:

1. `<foo89.h>` contains no include of `foo89/`.
2. `<foo89/extension.h>` contains no include of a named extension.
3. sibling extension headers include no unrelated sibling.
4. public headers include no source-tree-private header.
5. every installed header appears in the isolated-header test matrix.
6. the essentials-only test links against a core build with optional
   extensions disabled.
7. each optional extension can be disabled without changing or regenerating
   `<foo89.h>`.
8. extension-family dependencies match an explicit allowlist.

Textual include checks do not replace compilation tests; compilation tests do
not replace architectural dependency checks. The project SHOULD run both.

## 12. Review checklist

Every new or changed public header should answer these questions:

- Does `<foo89.h>` still compile and provide a usable API by itself?
- Does any core declaration now require an optional header?
- Does this header describe consumers, extension implementers, or private
  implementation code—and exactly one of those audiences?
- Could an implementation-only dependency move from the public header into its
  `.c` file?
- Does a sibling-extension dependency reflect a real, named extension family?
- Does the include hierarchy make the dependency direction visible?
- Do symbol prefixes identify the owning interface without ambiguity?
- Have ownership, lifetime, errors, callbacks, teardown, and ABI versioning been
  specified where the extension boundary requires them?
- Do isolated-header and extensions-disabled builds enforce the claimed
  boundary?

## 13. Summary rule

The project applies this architectural rule:

> `<foo89.h>` completely expresses the essential abstraction.
> `<foo89/extension.h>` exposes only controlled construction points for
> independently implemented extensions. `<foo89/name.h>` exposes one optional
> named capability. Dependencies flow toward the essentials API, and unrelated
> extensions do not depend on one another.

## 14. Result, error, and diagnostic conventions

### 14.1 Three classes of return

Every public operation MUST distinguish:

1. **Success** — the requested operation completed.
2. **Ordinary outcome** — no operation failure occurred, but no value was produced or progress requires another action.
3. **Failure** — the operation could not satisfy its contract.

Examples of ordinary outcomes include end-of-input, not found, retry required, conflict detected, and iteration complete. They MUST NOT masquerade as failures.

For status-returning APIs:

```c
typedef enum foo89_status
{
    FOO89_OK        = 0,

    FOO89_END       = 1,
    FOO89_AGAIN     = 2,
    FOO89_NOT_FOUND = 3,
    FOO89_CONFLICT  = 4,

    FOO89_EINVAL    = -1,
    FOO89_ENOMEM    = -2,
    FOO89_EIO       = -3,
    FOO89_EFORMAT   = -4,
    FOO89_ESTATE    = -5,
    FOO89_ENOTSUP   = -6,
    FOO89_EINTERNAL = -7
} foo89_status;
```

The sign partition forms part of the contract:

```c
status == 0  /* completed successfully */
status > 0   /* ordinary non-success outcome */
status < 0   /* failure */
```

Callers MAY classify by sign. Therefore a library MUST NOT assign a failure a positive value or an ordinary outcome a negative value.

A library declares only the statuses its abstraction can actually produce.

### 14.2 Names

The result type SHOULD use the name:

```c
foo89_status
```

Constants use:

```c
FOO89_OK
FOO89_END
FOO89_EINVAL
```

Use `status`, not `result`, for the operational return code. Reserve `result` for the value produced by an operation.

Use `error` for diagnostic information or a domain object that itself represents an error. For example, `jsonrpc89_error` correctly names a JSON-RPC error response; it does not name a failure of `libjsonrpc89`.

### 14.3 Stable numeric values

Every public status enumerator MUST receive an explicit numeric value.

Published values MUST NOT change, get reused, or acquire a different classification. New values SHOULD append within the appropriate positive or negative range.

Code MUST NOT assume that statuses form a dense range.

### 14.4 Statuses versus sentinels

A function MAY return its value directly when all of these conditions hold:

* failure cannot occur; or
* one unambiguous sentinel represents the only possible failure;
* the API needs no distinction between several failures;
* the sentinel cannot represent a valid result.

Examples:

```c
size_t foo89_count(const foo89 *foo);       /* total */
int foo89_equal(const foo89 *a,
                const foo89 *b);            /* total Boolean */
foo89_node *foo89_new(...);                 /* NULL means ENOMEM only */
```

A function MUST return `foo89_status` and write its value through an output parameter when it can produce multiple failures or ordinary outcomes:

```c
foo89_status foo89_lookup(foo89 *foo,
                          const void *key,
                          size_t key_len,
                          foo89_value *out);
```

A sentinel MUST NOT require consulting ambient state to distinguish its meanings.

### 14.5 Output atomicity

Unless a function explicitly documents incremental output:

* output parameters MUST receive their final values only on `FOO89_OK`;
* every output parameter MUST remain unchanged on a positive outcome or negative failure;
* a constructor returning through `foo89 **out` MAY instead set `*out = NULL` before beginning and guarantee `NULL` on every non-OK return;
* partial internal objects MUST get destroyed before return;
* ownership MUST transfer only on `FOO89_OK`.

Example:

```c
foo89 *foo;

foo = existing_value;
status = foo89_open(&foo, path);

/* status != FOO89_OK:
 * foo remains existing_value, unless the function explicitly uses the
 * constructor convention that sets it to NULL.
 */
```

Each constructor family MUST choose one of the two output rules and document it consistently.

### 14.6 Preconditions and reported invalid input

A public contract MUST distinguish unreportable contract violations from rejected runtime input.

Unreportable preconditions include:

* dangling or invalid object pointers;
* use after destruction;
* violation of documented aliasing restrictions;
* concurrent use forbidden by the object’s threading contract;
* passing storage smaller than its declared size.

Violating such a precondition invokes undefined behaviour. Debug builds MAY assert.

Representable invalid input SHOULD produce `FOO89_EINVAL`, including:

* a null pointer where the function can safely detect it;
* an invalid length/pointer combination;
* an out-of-range enumeration;
* an invalid option combination;
* an offset outside the operation’s accepted domain.

Libraries MUST NOT describe the same condition as both `EINVAL` and undefined behaviour.

### 14.7 Standard failure meanings

When applicable, library-local statuses SHOULD carry these meanings:

| Status            | Meaning                                                           |
| ----------------- | ----------------------------------------------------------------- |
| `FOO89_EINVAL`    | Rejected argument or option combination                           |
| `FOO89_ENOMEM`    | Allocation failed                                                 |
| `FOO89_EIO`       | External I/O or operating-system operation failed                 |
| `FOO89_EFORMAT`   | Input or persistent representation violates its format            |
| `FOO89_ESTATE`    | Object remains valid, but its current state forbids the operation |
| `FOO89_ENOTSUP`   | Valid request requires an unsupported capability                  |
| `FOO89_EINTERNAL` | Library invariant failed; indicates a defect or corruption        |

More precise domain failures SHOULD replace a generic code when callers can act differently. For example, a parser should distinguish malformed input from allocation failure.

`FOO89_EINTERNAL` MUST NOT serve as a catch-all for ordinary external failures.

### 14.8 Ordinary outcomes

Expected control-flow outcomes SHOULD use positive status values:

| Outcome           | Meaning                                           |
| ----------------- | ------------------------------------------------- |
| `FOO89_END`       | Iteration or input completed                      |
| `FOO89_AGAIN`     | No completion yet; retry after external progress  |
| `FOO89_NOT_FOUND` | Lookup completed and found no matching item       |
| `FOO89_EXISTS`    | Creation condition failed because the item exists |
| `FOO89_CONFLICT`  | Valid concurrent state prevented commitment       |

An ordinary outcome MUST leave the object usable unless documented otherwise.

Do not encode these outcomes as `EINVAL`, `EIO`, or generic failure.

### 14.9 `errno` profiles

A library MUST choose one error profile for each coherent API surface.

#### Portable status profile

The default project profile returns `foo89_status`.

Under this profile:

* `errno` carries no public meaning;
* callers MUST NOT consult `errno`;
* the implementation MUST NOT require a successful call to clear `errno`;
* operating-system failures map to a library status;
* APIs needing the original system error expose it explicitly.

#### POSIX syscall profile

A thin POSIX abstraction MAY deliberately use the syscall convention:

```c
/* success: non-negative or zero
 * failure: -1 with errno meaningful
 */
```

Under this profile:

* the header MUST identify the interface as POSIX-specific;
* `errno` matters only after the documented failure return;
* success does not imply that `errno` equals zero;
* library-specific errors MUST NOT use invented integer values in the platform `errno` namespace;
* library-specific conditions should use a status API or a separate documented result domain.

One function MUST NOT return both a library status and an independently meaningful `errno` unless it explicitly returns the native error through an output field.

### 14.10 Native system errors

A status API that needs lossless system diagnostics SHOULD use an optional explicit diagnostic:

```c
typedef struct foo89_error
{
    foo89_status status;
    int system_error;
} foo89_error;
```

Rules:

* `status` contains the library classification;
* `system_error` contains the captured `errno` value, or zero when none applies;
* the caller owns the structure;
* the library performs no allocation to report an error;
* passing `NULL` discards extended diagnostics;
* diagnostic failure MUST NOT replace the original failure.

Do not store the only copy of an error in global state or thread-local state.

A handle-local “last error” SHOULD NOT form the primary error channel because it complicates reentrancy, callbacks, multiple failures, and concurrent use.

### 14.11 Messages

Machine decisions MUST depend on status values and structured fields, never diagnostic prose.

A library MAY provide:

```c
const char *foo89_status_name(foo89_status status);
const char *foo89_status_message(foo89_status status);
```

`foo89_status_name()` SHOULD return stable ASCII tokens such as `"EINVAL"` or `"NOT_FOUND"`.

`foo89_status_message()` MAY return human-readable static text. Its wording does not form part of the compatibility contract.

Both functions:

* return pointers to immutable static storage;
* perform no allocation;
* accept unknown integer values;
* return a defined fallback for unknown values.

Dynamic details such as paths, offsets, and parser locations belong in an explicit diagnostic structure, not a static message.

### 14.12 Callback failures

A callback returning the enclosing library’s `foo89_status` uses the same status domain. The caller MUST propagate an unrecognized callback status unchanged after restoring library invariants and cleaning partial output.

The library MUST NOT translate every callback failure to `FOO89_ECALLBACK`, because doing so destroys actionable information.

When a callback uses a foreign error domain, the adapter SHOULD return `FOO89_ECALLBACK` and preserve the foreign code in explicit diagnostic storage.

Callback documentation MUST specify:

* accepted positive outcomes;
* whether arbitrary negative statuses propagate;
* output ownership after callback failure;
* cleanup performed before propagation;
* whether retry can re-invoke the callback.

### 14.13 Object state after failure

Every mutating operation MUST document one of these guarantees:

* **error-atomic:** externally observable state remains unchanged;
* **valid-prefix:** a documented prefix of the operation may remain;
* **recoverable:** state requires a named recovery operation;
* **poisoned:** only destruction or reset remains permitted.

Error-atomic behavior SHOULD form the default.

A failure MUST NOT silently leave an object in an undocumented state.

### 14.14 Error precedence

When several detectable problems apply, the contract SHOULD define precedence:

1. validate arguments requiring no external access;
2. validate object mode and state;
3. perform size and representability checks;
4. acquire required resources or locks;
5. perform external I/O;
6. commit observable state.

A function reports the first failure encountered in this order. Tests MAY rely on documented precedence.

Cleanup failures MUST NOT replace the primary failure. When extended diagnostics support secondary failures, cleanup failure MAY appear there.

### 14.15 Extensions and adapters

An extension MUST preserve core statuses unchanged when it merely forwards a core operation.

An adapter between libraries MUST translate statuses explicitly. It MUST NOT depend on coincidentally equal enum values.

Named extensions add their statuses to their own namespace when their failures cannot arise from the essentials API:

```c
foo89_codec_status
FOO89_CODEC_EENCODING
```

If extension operations already return `foo89_status`, extension-specific values belong in the core status enum only when the essentials ABI intentionally reserves and owns that domain.

### 14.16 Testing requirements

Every fallible public operation MUST receive tests covering:

* every declared status or documented reason why a status cannot receive direct injection;
* output state after failure;
* object state after failure;
* ownership and cleanup;
* error precedence;
* repeated failure;
* successful use after recoverable failure;
* callback-status propagation;
* unknown-status handling by status-name functions;
* `errno` behavior for POSIX-profile APIs;
* captured native errors for status-profile APIs;
* allocation failure at every allocation point.

### 14.17 Rewrite inspection

After converting a library to this convention, the reviewer MUST inspect the complete public and internal error surface. Passing tests alone does not establish a successful rewrite.

The inspection MUST verify:

1. Every public fallible function follows one declared profile: portable status or POSIX syscall.
2. Every return value has exactly one classification: success, ordinary outcome, or failure.
3. Status constants follow the zero/positive/negative partition and carry explicit stable values.
4. No removed status, sentinel, `errno` value, failed flag, or last-error channel remains reachable.
5. No caller still tests obsolete numeric values, sentinel meanings, or `errno`.
6. Output parameters and object state follow the documented failure-atomicity rule on every return path.
7. Cleanup paths preserve the primary failure and release every partially acquired resource exactly once.
8. Callback failures propagate or translate according to the callback contract.
9. Extension and adapter boundaries translate foreign status domains explicitly.
10. Headers, implementations, documentation, examples, tests, fuzz targets, and bindings describe the same error set.
11. Exported symbols and ABI changes match the intended compatibility decision.
12. Every declared status has a production return path or receives removal as unreachable API.
13. Every production failure path has a corresponding test, fault-injection point, or documented reason why deterministic injection cannot reach it.
14. Successful operations remain successful when `errno` contains a stale nonzero value.
15. Unknown status values produce defined results from inspection functions such as `foo89_status_name()`.

Mechanical inspection SHOULD include searches equivalent to:

```sh
rg -n '\berrno\b|strerror|perror|_error\b|_failed\b|_status\b|_result\b'
rg -n 'return[[:space:]]+(-1|NULL|[A-Z0-9_]*BAD)\b'
rg -n '[A-Z0-9_]+_(E[A-Z0-9_]+|END|AGAIN|NOT_FOUND|CONFLICT)\b'
```

The reviewer MUST inspect each match rather than treating an empty or nonempty search result as sufficient evidence.

A rewrite counts as complete only when:

* the old and new error channels cannot disagree because only the intended channel remains;
* all callers compile against the new contract without compatibility shims accidentally masking obsolete usage;
* the full test matrix passes with allocation, callback, I/O, and cleanup failures injected;
* public documentation can determine every return value, output state, object state, ownership consequence, and recovery action without consulting implementation code.

---

## Application to the current repository

The repository currently exposes several incompatible patterns:

| Library                | Current pattern                                                | Recommended direction                                                                                                                                                              |
| ---------------------- | -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `libappend89`          | `-1` plus `errno`, including invented values `2001` and `2002` | Retain POSIX profile, but stop placing project-defined values in `errno`; either map oversize to `E2BIG` and malformed storage to a standard error, or introduce `append89_status` |
| `libjsonrpc89`            | Flat status enum mixing protocol-core and transport failures   | Apply signed classification; move transport-only statuses into `<jsonrpc89/transport.h>`                                                                                              |
| `libkv89`              | Domain result enum                                             | Rename the operational type to `kv89_status`; classify not-found/conflict as positive outcomes                                                                                     |
| `libhm89` / `libadt89` | Status plus structured semantic diagnostics                    | Closest to the proposed convention; separate failure classification from diagnostic payload consistently                                                                           |
| `libjson89`               | sentinel, arena failed flag, mutable textual last error        | Preserve only if arena poisoning forms an intentional abstraction; add a structured status accessor before expanding the API                                                       |
| `libjalg89`            | pointer sentinels for allocation plus status for callbacks     | Acceptable only while pointer constructors can fail solely from allocation; document allocator failure as the single sentinel meaning                                              |

The most urgent correction concerns `libappend89`: `APPEND89_ETOOBIG = 2001` and `APPEND89_EFORMAT = 2002` claim space in the process-wide `errno` domain that the platform owns. The clean choices:

1. Keep the syscall-shaped API and use standard `errno` values: `E2BIG` for requests exceeding the fixed reserve and `EINVAL` or `EILSEQ` for invalid persistent format.
2. Switch to `append89_status` and return the captured native `errno` explicitly.

Given `libappend89`’s deliberately kernel-shaped abstraction, choice 1 fits best. For the rest of eightynine, the signed library-local status convention provides the cleanest uniform model.
