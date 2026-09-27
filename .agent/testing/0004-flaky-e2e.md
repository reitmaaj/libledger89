# Flaky-writer long-running end-to-end scenarios

1. Sixteen concurrent writer processes, across several generations, append
   uniquely identifiable records until the final ledger reaches at least 10 MiB
   of logical bytes; writers exit cleanly at their per-writer byte quota.

2. Each writer occasionally crashes at a controlled point: before the reserve
   candidate is published (kill before ftruncate), immediately after
   publication (kill after ftruncate), or mid-candidate (kill after a fixed
   number of candidate bytes are written). A crash never tears a published
   frame; the prefix stays valid and the next writer continues without error.

3. Each writer occasionally shuts down cleanly and the parent spawns a
   replacement, and occasionally reopens its handle, so the ledger sees many
   open/close and process lifetimes.

4. Writers append zero-length records (valid 16-byte frames) and records of
   sizes up to the reserve cap; they also attempt oversized appends (payload
   plus header exceeding the reserve), which MUST fail E2BIG before any I/O
   and MUST leave the ledger unchanged.

5. Writers occasionally inject an EIO into the candidate sync or the publish
   sync; the append reports failure, and the record is either absent or
   present after recovery (ambiguous commit), but never torn.

6. At every quiet point (all writers dead), the parent runs exclusive
   recovery, which must succeed and be idempotent; the recovered prefix must
   contain every acknowledged record and nothing else.

7. Two reader processes poll the ledger throughout, except during recovery
   windows; every poll observes a valid committed prefix: contiguous frames,
   checksum-verified payloads, monotonically increasing record numbers, never
   a torn or partial frame.

8. Final accounting: every acknowledged record is present exactly once, every
   present record was attempted with the recorded size, no duplicate or
   fabricated record exists, and the final logical size lies in the target
   window.
