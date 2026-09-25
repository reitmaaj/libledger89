# Test matrix and invariants

The suite reduces the ledger contract to four invariants and checks them from
several angles. Each test is an executable and re-validates the whole ledger
against an independent expectation at the end.

| Test | Angle | Key assertion |
| --- | --- | --- |
| test_smoke | end-to-end real I/O | create, append, sync, reopen, scan, append again |
| test_roundtrip | basics | append/read round-trip, empty message, reserve mismatch |
| test_format | framing boundaries | empty, sub-fragment, exact-fragment, first multi-fragment sizes |
| test_api | public contract | open/writer/reader/next/message_read argument and errno contract; stale errno |
| test_cursor | temporal semantics | END is transient across processes; PARTIAL retains a stable retry boundary |
| test_concurrent | total order | 4 processes x 12 records: no interleave, no duplicate, sorted offsets |
| test_crash | process death | SIGKILL at publish boundaries; abandoned predecessor is skipped |
| test_model | prefix recovery | every stream cut recovers exactly the complete-record prefix |
| test_fault | failure injection | ENOSPC/short-write/EINTR/uncertain unlock; error-atomic or poisoned |
| test_validation | structural rejection | malformed headers, polling, arguments, no interior truncation |
| test_admin | administration | check reports valid_end/incomplete_tail; repair truncates only the tail |
| test/cli | command interface | pure helpers plus end-to-end pipes (scripts/cli-check.sh); non-default reserve (test_reserve) |

Invariants (from CONVENTIONS.md section 14 and the spec):

1. A reader returns exactly an appended payload or no record.
2. Successful appends admit one total order; no fragment interleaves.
3. Crash recovery yields a complete-record prefix of that order.
4. `sync` protects every earlier record; a dead writer never blocks progress.
