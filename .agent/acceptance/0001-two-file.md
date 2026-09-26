# Acceptance criteria

## Must exhibit

1. `ledger89_append` reports success only after the INDEX entry is synchronized;
   the record survives a reopen.

2. `ledger89_count`, `ledger89_read`, and iteration return exactly the committed
   records in order, byte-for-byte, for payloads including NUL and all 256
   octet values, empty records, and records larger than the DATA reserve.

3. `ledger89_recover` on any torn state (partial final entry, uncommitted DATA
   tail, beyond-EOF entry, gap, overlap, backward offset, overflow, invalid
   first entry, middle checksum corruption) restores the longest valid prefix
   and is idempotent.

4. Concurrent writers produce one total order with no duplicates, gaps, or
   overlaps.

5. Simultaneous readers, while writers append with varying delays, observe only
   a valid committed prefix at every instant: monotonically increasing record
   numbers, contiguous DATA offsets, and complete checksum-verified payloads,
   never a torn or partial record.

6. The two files are exactly `<name>.data` and `<name>.index`; DATA contains no
   framing bytes; a byte range is a record only when a valid INDEX entry
   references it.

## Must reject

1. Opening a missing path, a directory, or a wrong-reserve file MUST fail
   without creating or modifying a file.

2. Creating over an existing ledger MUST fail with EEXIST and install nothing.

3. A writer-only operation on a reader handle MUST fail with EBADF.

4. Invalid arguments (NULL out, NULL data with nonzero size, negative index
   overflow) MUST fail with EINVAL or EOVERFLOW before any I/O.

## Unacceptable behavior

1. A torn append becoming visible to readers as a valid record.

2. Recovery skipping a damaged record and retaining a later record.

3. An uncommitted DATA tail surviving recovery.

4. Inventing an errno value, or leaving the ledger in a non-prefix state after
   any supported failure followed by recovery.
