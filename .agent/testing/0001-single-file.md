# Testing scenarios

## Storage invariants

1. After recovery and between transactions: the file starts with a valid
   preamble; the first record frame starts at byte 16; each frame has a
   complete header, `size + 16 <= reserve`, and a CRC matching its payload;
   the file ends exactly at the last frame boundary.

2. A reader returns exactly an appended payload or no record; no torn or
   partial frame becomes visible; no frame references payload beyond the
   logical stream.

3. Any recoverable damaged state resolves to the longest valid prefix;
   recovery never keeps a later record while dropping an earlier one.

## Record size cap

4. Appending a record with `size + 16 > reserve` fails E2BIG before any I/O;
   the ledger is unchanged.

5. A record whose payload length equals the cap appends and reads back
   byte-for-byte.

## Crash consistency

6. SIGKILL a writer after the candidate is written but before publication: the
   record is invisible; recovery leaves the prefix unchanged.

7. SIGKILL a writer after publication: the record survives complete.

8. For every prefix cut of a short history, recovery yields a valid prefix of
   the append history and never a torn frame.

## Concurrency

9. Many writer processes serialize: one total order, no duplicate, no gap, no
   overlap; each successful record appears exactly once.

10. Readers polling during concurrent appends observe exactly a valid prefix:
    monotonically increasing record numbers, contiguous frames, and complete
    checksum-verified payloads.

## Fault injection

11. ENOSPC on the candidate write, EIO on sync, and EIO on unlock each fail
    the append without corrupting the ledger; recovery resolves the ambiguous
    sync case deterministically.

12. Short writes and EINTR are retried; a failed final unlock poisons the
    handle.

## Corruption

13. Flipping a checksum, size, or payload byte, a truncated final header, an
    oversize frame, or a payload extending past the stream stops recovery at
    the first affected frame, never skipping to a later record.

14. Opening a file with a missing, mismatched, or wrong-reserve preamble fails
    EINVAL without modifying the file.
