# libledger89 two-file ledger specification (format L1)

## 1. Scope

`libledger89` stores a logical append-only sequence of opaque byte records in
two `libappend89` byte streams named `DATA` and `INDEX`.

`DATA` stores the concatenated record payload bytes. `INDEX` stores fixed-size
encoded record descriptors. The `INDEX` file alone defines record boundaries,
logical order, and commit state; the `DATA` file alone does not.

`libledger89` depends on `libappend89` (serialized suffix appends, concurrent
positional reads, shrink truncation, explicit durability, whole-file writer
locking) and `libchecksum89` (CRC-64/NVME payload checksums). Writers are
serialized with one ledger-wide exclusive lock covering both files.

## 2. Files

A ledger consists of exactly two files:

```text
<name>.data
<name>.index
```

Both files use `libappend89`. The pair forms one logical storage object.
Applications MUST NOT modify either file except through `libledger89`.

## 3. DATA file

The `DATA` file is the concatenation of committed logical record payloads:

```text
record0 || record1 || record2 || ... || recordN
```

No framing bytes, headers, length prefixes, separators, sentinels, or checksums
occur in `DATA` unless such bytes belong to the caller's payload.

For every committed record `n`:

```text
record[n] = DATA[ entry[n].offset .. entry[n].offset + entry[n].length )
```

## 4. INDEX file

The `INDEX` file is a sequence of fixed-size encoded entries. Conceptually:

```c
struct ledger89_entry {
    u64 offset;
    u64 length;
};
```

The encoded entry is 32 bytes, all fields big-endian:

| Field    | Bytes   | Meaning |
| ---      | ---:    | --- |
| offset   | 0 .. 7  | Absolute DATA offset of the record start |
| length   | 8 .. 15 | Payload length |
| checksum | 16 .. 23 | CRC-64/NVME of `DATA[offset .. offset+length)` |
| magic    | 24 .. 27 | `0x4c443839` ("LD89") |
| version  | 28 .. 29 | Format version, currently 1 |
| flags    | 30 .. 31 | Reserved, MUST be zero |

```text
ENTRY_SIZE = 32
```

The `INDEX` file contains `entry0 || entry1 || ... || entryN`. A partial final
entry does not form part of the valid index.

The `magic` and `version` fields let recovery distinguish a structurally valid
entry from random or torn bytes, independently of entry-size alignment.

## 5. Record order

The order of entries in `INDEX` defines the logical ledger order.

For all committed records `entry[0].offset = 0`, and for every `n > 0`:

```text
entry[n].offset = entry[n-1].offset + entry[n-1].length
```

Therefore committed data always forms one contiguous prefix of `DATA`. For a
valid prefix containing `N` entries the committed end is:

```text
committed_end = 0                                    if N == 0
committed_end = entry[N-1].offset + entry[N-1].length otherwise
```

The bytes `DATA[0 .. committed_end)` constitute committed ledger data. Any bytes
beyond `committed_end` are uncommitted.

## 6. Empty records

Zero-length logical records are permitted (`entry[n].length = 0`). Its offset
equals the current committed end, so consecutive zero-length records may share
an offset. Contiguity holds because `entry[n+1].offset = entry[n].offset + 0`.

## 7. Writer serialization

At most one writer executes a ledger append transaction at a time. The writer
acquires one ledger-wide exclusive inter-process lock before examining either
file for append purposes. The lock remains held through DATA append, DATA sync,
INDEX append, and INDEX sync, and is released only after the transaction
finishes or fails.

The ledger-wide lock is the `INDEX` stream's writer lock, held with
`append89_begin`/`append89_end`. The `DATA` stream's own per-call locks are
short-lived and never overlap another writer because the ledger lock excludes
them. Readers need not acquire the writer lock; their read algorithm tolerates
concurrent append and reads only committed INDEX entries.

## 8. Append operation

Given payload `P` of length `L`, `ledger89_append` performs the following steps
while holding the ledger-wide lock.

1. Obtain the append offset `O = committed_end` from the valid INDEX state.
2. Append exactly `L` bytes of `P` to `DATA` starting at offset `O`, in chunks
   no larger than the configured `DATA` reserve. For `L == 0` no DATA byte is
   written.
3. Synchronize `DATA` with `append89_sync`. This completes before publication
   of the corresponding INDEX entry.
4. Construct `entry = { O, L, checksum, LD89, version, 0 }`, where `checksum`
   is the CRC-64/NVME of `P`.
5. Append the complete entry to `INDEX`.
6. Synchronize `INDEX` with `append89_sync`.
7. Release the ledger-wide writer lock.

Only after step 6 succeeds may `ledger89_append` report the record as committed.

## 9. Commit definition

A logical record is committed if and only if its complete valid entry belongs
to the recovered valid prefix of `INDEX`. Presence of payload bytes in `DATA`
does not commit a record. An INDEX entry is the publication and commit record
for its corresponding DATA extent.

## 10. Required write ordering

The implementation preserves:

```text
DATA append  before  DATA sync  before  INDEX append  before  INDEX sync
```

An INDEX entry is never published before the DATA bytes it references are
synchronized.

## 11. Append return semantics

`ledger89_append` reports success only after successful INDEX synchronization.
A failure before that point reports failure. A failed append may leave no new
bytes, an uncommitted DATA tail, a partial final INDEX entry, or a complete but
not durably synchronized INDEX entry. Recovery resolves all such states
deterministically. The caller MUST NOT infer whether a failed append became
committed solely from the return value after an I/O error during INDEX
synchronization; after reopening, the recovered INDEX determines the result.

## 12. Startup recovery

Recovery runs under the ledger-wide exclusive lock. Let `D = size(DATA)` and
`I = size(INDEX)`.

1. **Remove partial INDEX entry.** Compute `I_complete = floor(I / 32) * 32`.
   If `I != I_complete`, truncate `INDEX` to `I_complete`.
2. **Validate INDEX prefix.** Initialize `expected = 0`, `valid_entries = 0`.
   Process complete entries from the beginning. For each entry validate
   `magic`/`version`/`flags`, `entry.offset == expected`, that
   `entry.offset + entry.length` does not overflow, that
   `entry.offset + entry.length <= D`, and that the checksum matches the DATA
   extent. On any failure stop. On success set `expected = entry.offset +
   entry.length` and increment `valid_entries`.
3. **Truncate invalid INDEX suffix.** Truncate `INDEX` to
   `valid_entries * 32`.
4. **Truncate uncommitted DATA tail.** If `D > expected`, truncate `DATA` to
   `expected`.
5. **Synchronize recovery changes.** Sync each modified file.
6. **Release the lock.**

## 13. Recovery result

After successful recovery `size(DATA) == committed_end` and
`size(INDEX) == valid_entries * 32`. Every INDEX entry references an extent
entirely contained in DATA, and entries describe DATA contiguously from byte
zero. No uncommitted DATA bytes and no partial or invalid INDEX entries remain.
Recovery is idempotent.

## 14. Crash cases

| Crash point | Recovered state |
| --- | --- |
| before DATA append | DATA/INDEX unchanged |
| during DATA append | DATA = old prefix + partial bytes; truncated to committed_end |
| after DATA append, before DATA sync | truncated to committed_end |
| after DATA sync, before INDEX append | new payload uncommitted; truncated away |
| during INDEX append | partial final entry removed; DATA truncated |
| after INDEX append, before INDEX sync | entry may or may not survive; recovered accordingly |
| after INDEX sync | committed; retained |

## 15. INDEX entry beyond DATA EOF

An entry requiring `entry.offset + entry.length > size(DATA)` is invalid. The
valid prefix ends immediately before it. Recovery truncates INDEX to the
preceding valid entry and DATA to the preceding committed end, salvaging the
longest provably valid ledger prefix.

## 16. Corruption handling

The baseline detects structural inconsistency through INDEX entry alignment,
magic/version/flags, offset continuity, integer overflow, DATA bounds, and
checksums. A damaged record ends the valid prefix; recovery never skips a
damaged record and continues with later records. The ledger always recovers to
a prefix.

## 17. Reader semantics

A reader derives records exclusively from complete committed INDEX entries. For
entry `e`, the reader returns `DATA[e.offset .. e.offset + e.length)`. A reader
never infers records from DATA size or contents. Bytes in DATA lacking a
committed INDEX entry are invisible. While a writer has appended and
synchronized DATA but not yet published the INDEX entry, readers observe the
preceding committed state.

## 18. Reader consistency during concurrent append

Because INDEX publication occurs after DATA synchronization, a reader that
observes a complete new INDEX entry may read the referenced DATA extent. A
reader never interprets a partial INDEX entry as valid; it computes
`complete_entries = floor(size(INDEX) / 32)` and ignores trailing bytes.

## 19. Truncation

Normal public operation does not permit arbitrary truncation of committed
records. Internal recovery may truncate the INDEX invalid suffix and the DATA
uncommitted suffix. Only suffix truncation is valid; recovery never removes a
valid record while preserving a later record.

## 20. Core invariants

After recovery and between completed append transactions:

1. `size(INDEX) % 32 == 0`
2. For a nonempty ledger, `entry[0].offset == 0`
3. For every `n > 0`, `entry[n].offset == entry[n-1].offset + entry[n-1].length`
4. For every entry, `entry[n].offset + entry[n].length <= size(DATA)`
5. For a recovered ledger, `size(DATA) == committed_end`
6. A DATA byte range is a record only when a valid committed INDEX entry
   references it
7. Any recoverable damaged state resolves to the longest valid ledger prefix

## 21. Conceptual model

```text
DATA    raw byte arena
INDEX   framing, ordering, publication, commit record
libledger89  cross-file coordination, recovery, logical record API
```

The authoritative ledger state is the valid INDEX prefix; the corresponding
committed DATA state is exactly `DATA[0 .. committed_end)`.

## 22. Minimal state machine

```text
CLEAN -> DATA_WRITTEN -> DATA_DURABLE -> INDEX_WRITTEN -> COMMITTED
```

Recovery maps every non-committed intermediate state back to the previous CLEAN
state or, when the complete INDEX entry survived, the COMMITTED state. No other
persistent logical state exists.

## 23. Platform

ISO C89 source dialect; POSIX file I/O via `libappend89`. 8-bit bytes, signed
`off_t` of at most 64 bits. Public `unsigned long long` values use the native
type. `_POSIX_C_SOURCE=200809L` is supplied by the build.
