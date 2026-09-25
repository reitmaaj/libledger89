# Administrative inspection and repair scenarios

1. Inspect a clean ledger: check reports valid_end equal to the logical end,
   records equal to the number of complete messages, and no incomplete tail.
2. Inspect a ledger whose final message is cut at every byte boundary: check
   reports the last complete-record boundary as valid_end and flags an
   incomplete tail, without modifying the file.
3. Repair a ledger with an incomplete final message: the incomplete suffix is
   removed exactly at its start, the prefix bytes are unchanged, and a later
   append begins exactly at the repaired boundary.
4. Repair an already clean ledger: it is an idempotent no-op that syncs and
   reports the same boundary; before and after coincide.
5. Inspect or repair a ledger whose interior is structurally corrupt: the
   operation fails with EILSEQ and must not truncate any history.
6. Inspect or repair a ledger created with a non-default reserve: the reserve
   is discovered from the preamble, so no caller-supplied reserve is needed.
7. Inspect or repair a missing path, a directory, a truncated preamble, or a
   wrong magic prefix: the operation fails without creating or mutating a file.
8. The report output fields are written only on success; on failure they remain
   unchanged (the caller zeroes them before the call).
