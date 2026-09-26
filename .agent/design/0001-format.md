# Format and layout design

## Files

A ledger is exactly two `libappend89` streams, created and installed atomically
inside one exclusively created sibling temporary directory:

```text
<name>.data    reserve = APPEND89_RESERVE (1 MiB)  - chunk capacity
<name>.index   reserve = 4096                       - one 32-byte entry per append
```

The INDEX reserve is a compile-time constant, so no reserve is stored in either
file and no discovery is required. DATA uses `libappend89`'s default reserve.

## Entry encoding

Fixed 32 bytes, big-endian, no native structs:

| Field    | Bytes   | Encoding |
| ---      | ---:    | --- |
| offset   | 0 .. 7  | u64 big-endian |
| length   | 8 .. 15 | u64 big-endian |
| checksum | 16 .. 23 | u64 big-endian = CRC-64/NVME |
| magic    | 24 .. 27 | `4c 44 38 39` ("LD89") |
| version  | 28 .. 29 | `00 01` |
| flags    | 30 .. 31 | `00 00` |

The magic and version fields let recovery reject random or torn bytes
independently of entry-size alignment.

## Handle

```c
struct ledger89 {
    append89 *data_r, *data_w;
    append89 *index_r, *index_w;
    int poisoned;
};
```

A reader handle opens `data_r` and `index_r`. A writer handle additionally
opens `data_w` and `index_w`. The INDEX writer lock (held with
`append89_begin`/`append89_end` on `index_w`) is the ledger-wide lock.

## Append transaction

1. `append89_begin(index_w, &I)` — ledger lock.
2. Derive committed end `O` from the last complete INDEX entry; read DATA size
   `D`. If `D != O`, run recovery inline.
3. Append the payload in `APPEND89_RESERVE`-sized chunks to DATA.
4. `append89_sync(data_w)`.
5. Encode `{O, L, crc, LD89, 1, 0}` and append to INDEX.
6. `append89_sync(index_w)`.
7. `append89_end(index_w)`.

Failure anywhere before step 6 reports failure; recovery resolves the tail.

## Recovery

Under the ledger lock: truncate a partial final INDEX entry, validate the INDEX
prefix (alignment, magic/version/flags, offset continuity, overflow, DATA
bounds, checksum), truncate the invalid INDEX suffix and uncommitted DATA tail,
and synchronize modified files. Idempotent; always resolves to the longest
valid prefix.

## Reader

Readers count `floor(size(INDEX)/32)` complete entries and read each record's
DATA extent directly, verifying its checksum while streaming. A partial final
entry is ignored. Random access `ledger89_read(l, n, ...)` validates entry n's
header, bounds, and checksum.
