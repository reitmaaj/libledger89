# Concurrent writers and readers end-to-end scenarios

1. Several writer processes each append many uniquely identifiable records with
   per-writer, per-record varying delays, while several reader processes run
   simultaneously against the same ledger.

2. A reader, on every poll, re-iterates the committed prefix from the first
   record and observes exactly a valid prefix: monotonically increasing record
   numbers, contiguous frames, and complete checksum-verified payloads.

3. A reader never observes a torn or partial record, never a gap or overlap in
   frame positions, and never a record whose payload is unavailable.

4. When every writer has finished, every reader reaches the known total record
   count and exits successfully.

5. The final ledger contains every record exactly once in one total order, with
   the first frame beginning at byte 16 and frames laid out contiguously.
