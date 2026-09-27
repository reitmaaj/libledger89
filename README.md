# libledger89: single-file append-only ledger

A durable, ordered, append-only sequence of opaque byte records stored in one
`libappend89` byte stream: a fixed 16-byte preamble followed by self-framed
records `[size u64 BE][crc u64 BE][payload]` with CRC-64/NVME payload
checksums. Each record frame fits within the append reserve, so one reserve
append publishes the whole record; the append's sync is its commit point.
Framing defines record semantics — boundaries, checksums, the size cap,
iteration — while the `libappend89` reserve gives the committed end in O(1)
and guarantees readers never observe a torn frame.

`spec/ledger89-spec.md` defines the format (S1), write ordering, recovery, and
crash contract.

Build and test from the repository root:

```sh
just build
just test
just sanitize        # ASan + UBSan
just check           # green + test + api-convention + cli
just e2e             # long-running flaky-writer scenario (~10 MiB final)
just e2e-sanitize    # the same under ASan + UBSan
```

The flaky end-to-end gate (`test/e2e_flaky.c`, not part of `just test`) runs 16
concurrent writer processes per generation that occasionally crash at
controlled points, inject sync failures, shut down and restart, or attempt
oversized appends; two readers poll the committed prefix; the parent runs
exclusive recovery and full accounting verification at every quiet point and
proves `acked ⊆ present ⊆ attempted` with no duplicates. `E2E_SEED` selects
the deterministic seed (default 1).

Link in order: `libledger89.a libappend89.a libchecksum89.a`. Public includes
need `libledger89/include`, `libappend89/include`, and `libchecksum89/include`.

Minimal lifecycle:

```c
ledger89 *l;
ledger89_iter *it;
unsigned char buf[4096];
ssize_t n;

ledger89_create("events", 0600);        /* fails if it already exists */
ledger89_open_writer(&l, "events");
ledger89_append(l, "hello", 5, NULL);   /* commits: one frame, synchronized */
ledger89_append(l, "world", 5, NULL);

ledger89_iter_begin(l, &it);
while (ledger89_iter_next(it) == LEDGER89_OK)
{
    while ((n = ledger89_iter_read(it, buf, sizeof(buf))) > 0)
        /* consume n bytes */;
}
ledger89_iter_close(it);
ledger89_close(l);
```

After a crash, open a writer and call `ledger89_recover` while the application
excludes all other access. Recovery validates the record prefix (header
completeness, the size cap, stream bounds, payload checksums) and truncates
the invalid suffix. It is idempotent and always restores the longest valid
prefix.

A record's payload length plus the 16-byte frame header must not exceed the
append reserve; otherwise the append fails with E2BIG before any I/O.
Zero-length records are permitted. Random access by record number
(`count`/`read`/`length`/`offset_of`) is not provided: locating record N
requires a scan. Sequential access is iteration; the iterator reports each
record's index, length, and payload offset.

`include/ledger89/admin.h` adds read-only `ledger89_admin_check` and
`ledger89_admin_repair`.

The two-file (DATA+INDEX) format L1 remains available under git tag
`two-file-v1`. The older fragmented single-file implementation and
`libledger89-core` are incompatible with this format; select one library,
never link two.
