# libledger89 single-file ledger specification (format S1)

## 1. Scope

`libledger89` stores a logical append-only sequence of opaque byte records in
one `libappend89` byte stream at the caller-chosen pathname.

The file is a fixed 16-byte big-endian preamble followed by self-framed
records. Framing defines record boundaries, payload checksums, and the record
size cap; it does not participate in torn-tail recovery, because the
`libappend89` reserve protocol guarantees the logical stream always ends at a
whole-record boundary (section 7).

`libledger89` depends on `libappend89` (serialized reserve appends, lock-free
concurrent reads, shrink truncation, explicit durability, whole-file writer
locking) and `libchecksum89` (CRC-64/NVME payload checksums). Writers are
serialized with one ledger-wide exclusive lock on the single stream.

## 2. File

A ledger is exactly one file:

```text
<name>
```

Applications MUST NOT modify the file except through `libledger89`. The file
uses `libappend89` with its default reserve (`APPEND89_RESERVE`, 1 MiB).

## 3. Preamble

The first 16 logical bytes are the big-endian preamble:

| Field   | Bytes   | Meaning |
| ---     | ---:    | --- |
| magic   | 0 .. 7  | `"LEDG89S1"` = `4c 45 44 47 38 39 53 31` |
| reserve | 8 .. 15 | Append reserve capacity in bytes, u64 |

The preamble is written and synchronized at creation. Open validates it:
`magic` must match, and `reserve` must equal `append89_capacity()`. Any
mismatch, a short preamble, or a wrong-reserve file fails with EINVAL and
modifies nothing. The preamble exists so a non-ledger file is rejected cleanly
instead of being misparsed as records.

## 4. Records

After the preamble the file is the concatenation of record frames:

```text
preamble || record0 || record1 || ... || recordN
```

Each frame is:

```text
[size u64 BE][crc u64 BE][payload]
```

| Field   | Bytes   | Meaning |
| ---     | ---:    | --- |
| size    | 0 .. 7  | Payload length in bytes, u64 big-endian |
| crc     | 8 .. 15 | CRC-64/NVME of the payload, u64 big-endian |
| payload | 16 ..   | Opaque bytes |

```text
HEADER_SIZE = 16
```

For every committed record, `size + HEADER_SIZE <= reserve`. A record
violating the cap fails the append with E2BIG before any I/O; a frame found
violating it during recovery ends the valid prefix.

## 5. Record order

The file order of frames defines the logical ledger order. The first record
frame starts at byte 16 (`PREAMBLE_SIZE`), and each frame follows immediately
after the previous one, so committed frames form one contiguous run ending at
the committed end.

## 6. Empty records

Zero-length logical records are permitted (`size = 0`). Their frame is the
16-byte header alone, with the CRC-64/NVME of the empty input. Consecutive
empty records have distinct frame positions.

## 7. Writer serialization and the append frontier

At most one writer executes a ledger append at a time. The writer holds the
single stream's writer lock (`append89_begin`/`append89_end`) through the
candidate append and its synchronization point.

The `libappend89` reserve protocol publishes whole candidates: the candidate
is written and synchronized inside the fixed trailing reserve, and only
extending the physical file by the candidate length publishes it. Therefore:

- A live reader never observes a partially written frame.
- After any crash the logical stream is a byte prefix of the intended stream
  and hence always ends at a whole-frame boundary.

The committed end is therefore always `append89_begin`'s reported logical size
— O(1), with no scan, no index read, and no cross-file alignment step.

## 8. Append operation

Given payload `P` of length `L`, with `L + HEADER_SIZE <= reserve`,
`ledger89_append` performs the following steps while holding the ledger-wide
lock.

1. Obtain the committed end `E` from `append89_begin`.
2. Encode the frame header `{ L, CRC-64/NVME(P) }`.
3. Copy `header || P` into one contiguous buffer and append it as one
   `append89_append` candidate at offset `E`. For `L == 0` the candidate is
   the header alone.
4. Synchronize with `append89_sync`.
5. Release the ledger-wide writer lock.

Only after step 4 succeeds may `ledger89_append` report the record as
committed. The new record's payload offset is `E + HEADER_SIZE`.

## 9. Commit definition

A logical record is committed if and only if its complete frame belongs to
the recovered valid prefix (section 12). Presence of bytes in the file does
not commit a record; publication of the complete frame, durably synchronized,
does.

## 10. Required write ordering

The implementation preserves:

```text
candidate write  before  candidate sync  before  publication  before  file sync
```

which `libappend89` enforces internally: the candidate is synchronized inside
the reserve, published by extending the file, and `append89_sync` makes the
publication durable. A frame is never durably published before its payload
bytes are synchronized.

## 11. Append return semantics

`ledger89_append` reports success only after successful synchronization. A
failure before that point reports failure. Because publication is whole, a
failed append leaves either no new frame or a complete but unsynchronized
final frame; recovery resolves the latter deterministically. The caller MUST
NOT infer whether a failed append became committed solely from the return
value after an I/O error during synchronization; after reopening, the
recovered prefix determines the result.

## 12. Startup recovery

Recovery runs under the ledger-wide exclusive lock. Let `S` be the logical
size. (The preamble was already validated at open; recovery re-checks the
record prefix.)

1. **Validate the prefix.** Set `pos = 16`, `records = 0`. While
   `pos < S`:
   - read the 16-byte header at `pos`; a short header ends the prefix;
   - decode `size`; `size + HEADER_SIZE` must not exceed the reserve and
     `pos + HEADER_SIZE + size` must not overflow and must not exceed `S`;
     any violation ends the prefix;
   - the CRC-64/NVME of `[pos + 16 .. pos + 16 + size)` must match the
     header; a mismatch or an extent ending before `S` ends the prefix;
   - on success `pos = pos + HEADER_SIZE + size`, `records = records + 1`.
2. **Truncate the invalid suffix.** If `pos != S`, truncate the stream to
   `pos`.
3. **Synchronize recovery changes.** Sync the file when it was modified.
4. **Release the lock.**

## 13. Recovery result

After successful recovery `size == committed_end`, where `committed_end` is
the end of the longest valid prefix: every retained frame has a complete
header, a size within the cap, a payload fully contained in the stream, and a
matching checksum. No partial, oversize, or checksum-invalid frame remains.
Recovery is idempotent. A malformed preamble is reported (EINVAL at open) and
is not silently truncated away.

## 14. Crash cases

| Crash point | Recovered state |
| --- | --- |
| before candidate write | unchanged |
| during candidate write | invisible; unchanged (candidate bytes remain in the reserve) |
| after candidate sync, before publication | invisible; unchanged |
| during publication | whole frame present or absent; recovered accordingly |
| after publication, before sync | frame may or may not survive; recovered accordingly |
| after sync | committed; retained |

Process death releases the OS lock and behaves as the table above with
publication already decided by the kernel. Power loss additionally requires
the durability barriers of section 10; with them, a surviving publication
implies a fully synchronized frame.

## 15. Corruption handling

The baseline detects structural inconsistency through preamble validation,
header completeness, the size cap, integer overflow, stream bounds, and
payload checksums. A damaged frame ends the valid prefix; recovery never
skips a damaged frame and continues with later frames. The ledger always
recovers to a prefix.

## 16. Reader semantics

A reader derives records exclusively from frame headers and never infers
records from raw bytes. While a writer has written but not yet published a
frame, readers observe the preceding committed state. A reader tolerates
concurrent appends; it computes nothing from stream size and simply walks
frames, treating a short header as end-of-stream. A payload that ends early
during streaming fails with EILSEQ.

## 17. Iteration

`ledger89_iter_begin` positions an iterator before the first record.
`ledger89_iter_next` reads the next frame header, validates the size cap, and
positions the iterator on the record, reporting its index (0-based), payload
length, and payload offset. `ledger89_iter_read` streams payload bytes and
verifies the CRC at the end of the record. `LEDGER89_END` reports no further
complete frame.

Random access by record number is not provided: `ledger89_count`,
`ledger89_read`, `ledger89_length`, and `ledger89_offset_of` do not exist in
format S1, because locating record N requires a scan.

## 18. Truncation

Normal public operation does not permit arbitrary truncation of committed
records. Internal recovery may truncate the invalid suffix only. Only suffix
truncation is valid; recovery never removes a valid record while preserving a
later record.

## 19. Core invariants

After recovery and between completed append transactions:

1. Bytes `0 .. 15` form a valid preamble whose reserve matches the stream's.
2. For a nonempty ledger, the first frame starts at byte 16.
3. Frames are contiguous: each frame starts where the previous one ended.
4. Every frame has `size + 16 <= reserve`.
5. Every frame's payload lies entirely within the logical stream.
6. Every frame's header CRC matches its payload.
7. For a recovered ledger, the stream ends exactly at the last frame boundary.
8. Any recoverable damaged state resolves to the longest valid prefix.

## 20. Conceptual model

```text
libappend89   committed byte prefix, reserve-published, O(1) frontier
libledger89   [frame][frame][frame]... framing, checksums, recovery, records
```

The authoritative ledger state is the valid frame prefix; the committed file
state is exactly the bytes up to its end.

## 21. Minimal state machine

```text
CLEAN -> CANDIDATE_WRITTEN -> CANDIDATE_SYNCED -> PUBLISHED -> COMMITTED
```

Recovery maps every non-committed intermediate state back to the previous
CLEAN state or, when the complete frame survived, the COMMITTED state. No
other persistent logical state exists.

## 22. Platform

ISO C89 source dialect; POSIX file I/O via `libappend89`. 8-bit bytes, signed
`off_t` of at most 64 bits. Public `unsigned long long` values use the native
type. `_POSIX_C_SOURCE=200809L` is supplied by the build.
