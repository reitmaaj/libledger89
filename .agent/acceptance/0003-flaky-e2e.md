# Flaky-writer long-running end-to-end acceptance

## Must exhibit

1. The final ledger contains at least 10 MiB of logical bytes and no more than
   the target plus a bounded overshoot window.

2. Every record whose append returned success is present exactly once in the
   final ledger, byte-for-byte, with its recorded size.

3. Every present record was attempted by some writer with exactly its recorded
   size; no fabricated record exists.

4. Oversized append attempts fail E2BIG before any I/O; no oversized frame
   ever appears in the ledger; zero-length records commit as valid records.

5. Writer crashes at any of the three controlled points (before publication,
   after publication, mid-candidate) never tear a frame; the next writer and
   the readers observe a valid prefix.

6. Exclusive recovery succeeds and is idempotent at every quiet point; after
   recovery the prefix contains every acknowledged record.

7. Readers polling outside recovery windows observe only complete,
   checksum-verified, contiguous frames with monotonically increasing record
   numbers.

8. The test is deterministic for a given seed and completes without
   sanitizer, Valgrind, or compiler warnings under the strict C89 flags.

## Must reject

1. A writer attempt whose append reports failure but that the test treats as
   acknowledged (only success-returning appends count as acknowledged).

2. Recovery being run concurrently with live writers or readers.

## Unacceptable behavior

1. A duplicate, missing-acknowledged, torn, or fabricated record in the final
   ledger.

2. An oversized frame surviving anywhere in the ledger.

3. A reader observing a partial frame or a checksum failure at any poll.

4. The test passing while its accounting disagrees with the ledger contents.

5. The test leaving temporary files, the ledger, or accounting files behind.
