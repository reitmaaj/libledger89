# Single-file ledger concept

`libledger89` stores a durable, append-only sequence of opaque byte records in
exactly one `libappend89` byte stream at the caller-chosen path:

```text
<name>
```

The file begins with a fixed 16-byte big-endian preamble
(`"LEDG89S1"` magic plus the append reserve capacity) followed by self-framed
records:

```text
preamble || record0 || record1 || ...
```

Each record is `[size u64 BE][crc u64 BE][payload]`; `crc` is the CRC-64/NVME
of the payload. A record frame, header included, never exceeds the append
reserve, so one candidate append publishes the whole record. The reserve
protocol (candidate written and synchronized in the reserve, then published
by extending the physical file) makes publication whole: a live reader, and
the file after any crash, always ends at a record boundary. Framing therefore
serves ledger semantics — boundaries, checksums, the size cap, iteration —
not torn-tail repair.

Writers serialize through the single stream's writer lock
(`append89_begin`/`append89_end`), held across the candidate append and its
`append89_sync` durability point. The sync is the commit point: an append
reports success only after the record is durably published.

Recovery is prefix validation, not crash-tail location: it re-checks the
preamble, each frame's size bounds, and each payload checksum, truncating at
the first invalid frame. It is idempotent and always restores the longest
valid prefix.

Random access by record number (`count`, `read`, `length`, `offset_of`) is not
provided: locating record N requires an O(N) scan, which is unfeasible as a
public API. Sequential access is iteration; the iterator reports the current
record's index, length, and payload offset as it walks forward.

Dependencies: `libappend89` (serialized reserve appends, lock-free concurrent
reads, shrink truncation, explicit durability) and `libchecksum89`
(CRC-64/NVME).
