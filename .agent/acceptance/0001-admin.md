# Administrative extension acceptance

## Must exhibit

1. `ledger89_admin_check` on a clean ledger returns 0, fills valid_end, records,
   and incomplete_tail == 0, and changes nothing on disk.
2. `ledger89_admin_check` on an incomplete-tail ledger returns 0 with
   incomplete_tail == 1 and valid_end equal to the last complete boundary.
3. `ledger89_admin_repair` removes exactly the incomplete suffix and returns 0;
   prefix bytes before valid_end are byte-for-byte unchanged.
4. `ledger89_admin_repair` on a clean ledger is idempotent: running it twice
   yields the same valid_end and the same bytes.
5. Both operations discover the reserve from the file preamble, so they work
   on ledgers created with any valid reserve.

## Must reject

1. An interior-corrupt ledger MUST NOT be repaired by truncating to an earlier
   boundary; `check` and `repair` fail with EILSEQ and preserve the file.
2. A missing path MUST fail without creating a file.
3. A directory, a short (< 16 byte) file, or a wrong magic prefix MUST fail.
4. A failed repair MUST NOT leave the report partially populated; fields are
   only written on success.

## Unacceptable behavior

1. Silently truncating valid later records when an earlier interior record is
   corrupt.
2. Returning 0 while an incomplete tail remains after repair.
3. Inventing an errno value or leaving `errno` meaningful on success.
