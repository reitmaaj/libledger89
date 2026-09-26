# Format and layout design

## File

A ledger is exactly one `libappend89` stream, created and installed atomically
inside one exclusively created sibling temporary directory:

```text
<name>   reserve = APPEND89_RESERVE (1 MiB) - frame capacity, preamble-validated
```

The reserve is stored in the preamble and validated against
`append89_capacity()` at open, so mismatched openers are rejected without a
reserve parameter.

## Preamble

Fixed 16 bytes, big-endian, no native structs:

```text
bytes  0 .. 7   magic   "LEDG89S1" (4c 45 44 47 38 39 53 31)
bytes  8 .. 15  reserve u64 big-endian capacity in bytes
```

A file with a missing, mismatched, or wrong-reserve preamble is rejected with
EINVAL at open. The preamble lets the library reject a non-ledger file cleanly
instead of misparsing arbitrary bytes.

## Record framing

Each record frame is `[size u64 BE][crc u64 BE][payload]`:

| Field  | Bytes    | Encoding |
| ---    | ---:     | --- |
| size   | 0 .. 7   | u64 big-endian payload length in bytes |
| crc    | 8 .. 15  | u64 big-endian CRC-64/NVME of the payload |

`LEDGER89_PRIV_HEADER_SIZE` is 16. The payload follows the header; a
zero-length record is the 16-byte header alone with the CRC of the empty
input. The invariant `size + 16 <= reserve` holds for every frame; a record
violating it fails with E2BIG before any I/O, and a frame found violating it
during recovery ends the valid prefix.

## Handle

```c
struct ledger89 {
    append89 *r;        /* reader stream, preamble-validated */
    append89 *w;        /* writer stream, NULL on reader handles */
    int poisoned;
};
```

The writer lock on the single stream (held with
`append89_begin`/`append89_end` on `w`) is the ledger-wide writer lock.

## Append transaction

1. `append89_begin(w, &end)` — ledger lock; `end` is the committed end in
   O(1), because the reserve invariant guarantees the stream always ends at a
   whole-record boundary.
2. Encode the frame header `{size, crc}`, copy `header || payload` into one
   contiguous buffer, and `append89_append(w, frame, 16 + size, NULL)` — one
   candidate, one publication.
3. `append89_sync(w)` — the commit point.
4. `append89_end(w)`.

Failure anywhere reports failure; an end failure poisons the handle. The
committed end is never derived by scanning, and no cross-file alignment step
exists.

## Recovery

Under the ledger lock: re-validate the preamble, then scan frames from byte
16. Each frame must have a complete header, a size within the reserve cap, a
payload fully inside the logical stream, and a matching CRC. Truncate at the
first invalid frame and synchronize. Idempotent; always resolves to the
longest valid prefix. Crash never produces a torn frame in-cooperation (the
reserve publishes whole candidates), so recovery exists to enforce the
invariant and remove corruption.

## Reader

A reader walks frames forward: it reads the 16-byte header at the current
position (a short header means end-of-stream), validates the size cap, and
streams the payload while verifying the CRC. A live reader never observes a
torn frame because publication is whole; a payload ending early (external
damage) fails EILSEQ.
