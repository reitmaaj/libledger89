# libledger89: fragmented messages over libappend89

New single-file message format. A writer holds one append89 session across its
fragments; every fragment publishes atomically to live readers. A dead process
leaves zero or more complete fragments. The next writer starts at EOF without
scanning or truncating. Readers skip an abandoned message when a new one starts.

`spec/ledger89-spec.md` defines the format and recovery contract.

Build and test from the repository root:

```sh
just ledger-build
just ledger-test
# Equivalent without just:
sh scripts/build-fragmented-ledger.sh
sh scripts/test-fragmented-ledger.sh
```

Link in order: `libledger89.a libappend89.a`. Public includes need
`libledger89/include` and `libappend89/include`. Compile with the same
large-file ABI for every library and application (on ILP32 use
`_FILE_OFFSET_BITS=64`).

Minimal lifecycle:

```c
ledger89 *a;
ledger89_message *message;
ledger89_offset cursor;

/* Check every result in application code. Creation fails if path exists. */
ledger89_create("events", 0600, 0);
ledger89_open_writer(&a, "events", 0);
ledger89_append(a, "hello", 5, NULL);
ledger89_sync(a);
cursor = LEDGER89_BEGIN;
if (ledger89_next(a, &cursor, &message) == LEDGER89_OK)
{
    /* Read in caller-sized chunks until ledger89_message_read returns zero. */
    ledger89_message_close(message);
}
ledger89_close(a);
```

Before restarting normal access after a system crash, open a writer and call
`ledger89_recover` while the application excludes all other access. Recovery
does not run automatically on open. Do not use it alongside live readers.

No checksum, bit-rot detection, rotation, manifests, sequence numbers, or exact
durability frontier. No message-sized allocation: writes use iovec slices and
reads expose a constant-size view after validating complete framing.

The legacy `exploratory/libledger89` remains available for its existing adapters.
Its v2 disk format and API are incompatible with this implementation. Both
export `ledger89_*`; select one implementation, never link both. This new
implementation has focused regression gates; it is not a claim of release
qualification across filesystems and hardware.
