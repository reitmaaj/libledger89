# libledger89: two-file append-only ledger

A durable, ordered, append-only sequence of opaque byte records stored in two
`libappend89` byte streams: a `DATA` file of raw payloads and an `INDEX` file of
fixed 32-byte descriptors. The `INDEX` file defines record boundaries, order,
and commit state; the `DATA` file alone does not.

`spec/ledger89-spec.md` defines the format, write ordering, recovery, and crash
contract.

Build and test from the repository root:

```sh
just build
just test
just sanitize        # ASan + UBSan
just check           # test + api-convention
```

Link in order: `libledger89.a libappend89.a libcksum89.a`. Public includes need
`libledger89/include`, `libappend89/include`, and `libcksum89/include`.

Minimal lifecycle:

```c
ledger89 *l;
ledger89_iter *it;
unsigned long long count;
unsigned char buf[4096];
ssize_t n;

ledger89_create("events", 0600);        /* fails if it already exists */
ledger89_open_writer(&l, "events");
ledger89_append(l, "hello", 5, NULL);   /* commits: DATA sync + INDEX sync */
ledger89_append(l, "world", 5, NULL);

ledger89_count(l, &count);              /* 2 */
ledger89_read(l, 0, buf, sizeof(buf));  /* "hello" */

ledger89_iter_begin(l, &it);
while (ledger89_iter_next(it) == LEDGER89_OK)
{
    while ((n = ledger89_iter_read(it, buf, sizeof(buf))) > 0)
        /* consume n bytes */;
}
ledger89_iter_close(it);
ledger89_close(l);
```

After a system crash, open a writer and call `ledger89_recover` while the
application excludes all other access. Recovery removes a partial final INDEX
entry, validates the INDEX prefix (including CRC-64/NVME payload checksums),
truncates any invalid INDEX suffix and uncommitted DATA tail, and synchronizes
modified files. It is idempotent and always restores the longest valid prefix.

Records larger than the append reserve are written in reserve-sized chunks
within one transaction; there is no artificial maximum record size. Zero-length
records are permitted.

The public API is record-based (`append`/`count`/`read`/`iterate`/`recover`).
It does not provide a separate `sync`; an append is its own commit point.
`include/ledger89/admin.h` adds read-only `ledger89_admin_check` and
`ledger89_admin_repair`.

The legacy single-file fragmented implementation and `exploratory/libledger89`
are incompatible with this format; select one library, never link both.
