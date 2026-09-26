# Two-file ledger concept

`libledger89` stores a durable, append-only sequence of opaque byte records in
exactly two `libappend89` byte streams:

```text
<name>.data    raw record payload arena (no framing, no headers)
<name>.index   fixed 32-byte descriptors (framing, order, commit record)
```

The `INDEX` file is authoritative: it defines record boundaries, logical order,
and commit state. `DATA` bytes without a committed `INDEX` entry are
uncommitted and disposable.

Each 32-byte `INDEX` entry is big-endian:

```text
offset    u64   absolute DATA offset of the record start
length    u64   payload length
checksum  u64   CRC-64/NVME over DATA[offset .. offset+length)
magic     u32   "LD89" (0x4c443839)
version   u16   1
flags     u16   0 (reserved)
```

Writers serialize through one ledger-wide lock (the INDEX writer lock), held
across DATA append, DATA sync, INDEX append, and INDEX sync. The write order
guarantees that a durable committed INDEX entry implies its referenced DATA was
synchronized first.

Recovery maps any crash state to the longest valid prefix: it removes a partial
final INDEX entry, validates entry alignment, offset continuity, overflow
safety, DATA bounds, and payload checksums, then truncates any invalid INDEX
suffix and uncommitted DATA tail.

Dependencies: `libappend89` (serialized appends, concurrent reads, shrink
truncation, durability, locking) and `libcksum89` (CRC-64/NVME).
