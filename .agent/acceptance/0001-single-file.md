# Acceptance criteria

## Must exhibit

1. `ledger89_append` reports success only after the record frame is
   synchronized; the record survives a reopen.

2. Iteration returns exactly the committed records in order, byte-for-byte,
   for payloads including NUL and all 256 octet values, empty records, and
   records of the maximum permitted payload length (reserve minus 16).

3. `ledger89_recover` on any damaged state (truncated final header, oversize
   frame, payload past end, overflow, checksum corruption, invalid preamble)
   restores the longest valid prefix and is idempotent.

4. Concurrent writers produce one total order with no duplicates, gaps, or
   overlaps.

5. Simultaneous readers, while writers append with varying delays, observe
   only a valid committed prefix at every instant: monotonically increasing
   record numbers, contiguous frames, and complete checksum-verified payloads,
   never a torn or partial record.

6. The ledger is exactly one file at `<name>`; the first record frame starts
   at byte 16; a byte range is a record only when a valid frame header and CRC
   describe it.

7. `ledger89_iter_index`, `ledger89_iter_length`, and `ledger89_iter_offset`
   report the current record's number, payload length, and payload offset.

## Must reject

1. Opening a missing path, a directory, or a wrong-reserve file MUST fail
   without creating or modifying a file; a non-ledger preamble fails EINVAL.

2. Creating over an existing ledger MUST fail with EEXIST and install nothing.

3. A writer-only operation on a reader handle MUST fail with EBADF.

4. Appending a record with `size + 16 > reserve` MUST fail E2BIG before any
   I/O.

5. Invalid arguments (NULL out, NULL data with nonzero size) MUST fail with
   EINVAL or EOVERFLOW before any I/O.

6. The public API MUST NOT expose random-access functions that require a scan
   (`ledger89_count`, `ledger89_read`, `ledger89_length`,
   `ledger89_offset_of`); the archive symbol audit enforces their absence.

## Unacceptable behavior

1. A torn append becoming visible to readers as a valid record.

2. Recovery skipping a damaged record and retaining a later record.

3. An uncommitted or damaged tail surviving recovery.

4. Inventing an errno value, or leaving the ledger in a non-prefix state after
   any supported failure followed by recovery.
