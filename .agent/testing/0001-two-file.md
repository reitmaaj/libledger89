# Testing scenarios

## Storage invariants

1. After recovery and between transactions: INDEX size is a multiple of 32; the
   first entry offset is 0; entries are contiguous; no offset+length overflows;
   every entry's extent lies within DATA; DATA size equals the committed end.

2. A reader returns exactly an appended payload or no record; no unindexed DATA
   becomes visible; no indexed record references unavailable DATA.

3. Any recoverable damaged state resolves to the longest valid prefix; recovery
   never keeps a later record while dropping an earlier one.

## Crash consistency

4. SIGKILL a writer after DATA publish (before INDEX publish): recovery removes
   the uncommitted DATA tail.

5. SIGKILL a writer after INDEX publish: the record is committed and survives.

6. For every (DATA cut, INDEX cut) pair of a short history, recovery yields a
   valid prefix of the append history.

## Concurrency

7. Many writer processes serialize: one total order, no duplicate, no gap, no
   overlap; each successful record appears exactly once.

## Fault injection

8. ENOSPC on DATA write, EIO on DATA sync, and EIO on INDEX sync each fail the
   append without corrupting the ledger; recovery resolves the ambiguous
   INDEX-sync case deterministically.

9. Short writes and EINTR are retried; a failed final unlock poisons the handle.

## Corruption

10. Flipping a checksum, offset, magic, or payload byte stops recovery at the
    first affected record, never skipping to a later record.
