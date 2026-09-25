# Implementation validation

Ported onto `main` (integration base `cdfb5cd`) with the error model mapped to
platform errno per CONVENTIONS.md section 14 (EFORMAT->EINVAL,
ECORRUPT->EILSEQ, EUNCERTAIN->EIO). The original patch was developed against
base revision `556ddcbf4dff4ac0dc212c24281a07ecfa3f716e`.
Local branch: `feat/fragmented-ledger`. No commit or remote publication.

Executed with GCC 13 on the available LP64 Linux environment:

- Strict C89 build with pedantic errors and conversion/sign-conversion warnings.
- Existing append smoke, unit, state/crash model, concurrent, thread, fuzz,
  contract, process-crash and syscall-fault tests.
- New append reserve/session and competing-creation regression tests.
- Ledger concurrent writers, writer death, every recovered prefix of a
  three-fragment fixture, torn headers, I/O failures and short/EINTR retries.
- More-than-1-MiB messages with default and enlarged reserves.
- Corrupt-header refusal, reserve mismatch, polling, empty messages, input
  overflow, failed recovery synchronization and uncertain final unlock.
- C++23 public-header compile/link check.
- AddressSanitizer and fatal UndefinedBehaviorSanitizer across the focused gate.
- GCC static analyzer on ledger89.c and API/source/export convention checks.

Commands:

```sh
sh scripts/test-fragmented-ledger.sh
ASAN_OPTIONS=detect_leaks=0 \
CFLAGS='-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all' \
sh scripts/test-fragmented-ledger.sh
sh scripts/check-api-convention.sh --symbols --lib libappend89
sh scripts/check-api-convention.sh --symbols --lib libledger89
```

Unavailable gates: LeakSanitizer fails under this environment's process tracing;
32-bit libc headers/toolchain, Clang, Valgrind and the sibling green driver are
absent. `just` is absent, so equivalent checked-in shell entry points were used.
The full repository release, mutation and large stress gates were not run.

These checks test the specified prefix model and live-kernel behavior. They do
not establish real power-loss behavior on a specific filesystem/device, or
support filesystems outside libappend89's documented environment.
