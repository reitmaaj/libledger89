# Implementation validation

Format S1 single-file redesign of libledger89, developed on branch
`single-file-v1` off `bdc20a2` (tagged `two-file-v1`).

Executed with GCC and Clang on the available LP64 Linux environment:

- Strict C89 build with pedantic errors and conversion/sign-conversion
  warnings; the green matrix compiles every source as strict C89 and C23
  with both compilers.
- Green semantic checks (hidden-control, transition-boundary,
  effect-boundary, pure-contract, cast-boundary, null, declaration,
  fallthrough, preprocessor, toolchain-branching, flat, reserved-suffix,
  braces) and the canonical clang-format profile.
- Unit tests for the preamble and frame-header codecs, big-endian scalars,
  and CRC-64/NVME.
- Smoke, API (including the reserve cap and E2BIG rejection), format,
  recovery, corruption, crash (kill-before/kill-after/mid-candidate and
  every prefix cut), fault injection (ENOSPC, EIO, short writes, EINTR,
  poisoned unlock), fuzz, exhaustive prefix model, property, concurrent
  writers, concurrent end-to-end readers, stress (20k records, maximum-size
  record, 5k empty records), admin, and CLI suites.
- C++23 public-header compile/link check.
- AddressSanitizer and fatal UndefinedBehaviorSanitizer across the suite.
- Valgrind memcheck on the deterministic and representative suites.
- API convention check with archive symbol audit; the audit confirms the
  removed scan-requiring functions (`ledger89_count`, `ledger89_read`,
  `ledger89_length`, `ledger89_offset_of`) are absent from the archive.

Commands:

```sh
just build
just smoke
just test
just sanitize
just valgrind
just green
just api-convention
just check
```

Unavailable gates: 32-bit libc headers/toolchain; LeakSanitizer under this
environment's process tracing; real power-loss hardware validation.

These checks test the specified prefix model and live-kernel behavior. They do
not establish real power-loss behavior on a specific filesystem/device, or
support filesystems outside libappend89's documented environment.
