# Fragmented ledger specification (format F1)

## Scope and platform

Opaque messages in one libappend89 logical byte stream. Cooperating processes
serialize complete message writes through a writer session. Depends on
libappend89, POSIX. C89 source syntax; 8-bit bytes, signed off_t of at
most 64 bits, and the append substrate's local-filesystem assumptions.
No arbitrary corruption detection, checksums, repair of interior corruption,
rotation, replication, index, snapshot, or exact stable frontier.

## Persistent format

All integers use canonical unsigned 64-bit big-endian encoding via native
`unsigned long long` values.
Native offsets/lengths must fit positive off_t; reserve must also fit size_t.

The first 16 logical bytes are `"LEDG89F1"` followed by reserve capacity. Reserve
capacity is immutable and must exceed 24. Open takes this capacity (zero means
the append default, currently 1 MiB); it validates the header before allowing
mutation. Changing a build default does not migrate files. Raw append clients
must never mutate a ledger outside this protocol.

Every fragment is:

| Field | Bytes | Meaning |
| --- | ---: | --- |
| message_start | 8 | Logical offset of the first fragment header |
| message_length | 8 | Total message payload, excluding all headers |
| fragment_length | 8 | This fragment's payload length |
| payload | fragment_length | Opaque bytes |

The first fragment has message_start equal to its own position. Subsequent
fragments repeat message_start and message_length. Each header plus payload
fits within the reserve and is submitted in one append89_appendv call. Fragment
payloads are positive except for a single-fragment empty message `(start,0,0)`.
Accumulated payload never exceeds message_length; equality completes a message.

A new start while accumulating an incomplete message abandons its predecessor.
The abandoned bytes remain, and readers skip them. Continuations with the wrong
identity or total length, invalid offsets, overflow and illegal lengths are
structural corruption. A file-size prefix ending inside an otherwise legal
fragment is PARTIAL. A complete header cannot diagnose arbitrary corruption of
its fields or payload; the storage failure model promises exact retained bytes.

## Creation and opening

`ledger89_create` constructs a file inside an exclusively created sibling
temporary directory, writes and synchronizes the preamble, then installs it
with a non-replacing hard link. Existing target paths fail EEXIST, including
symlinks. Mode follows umask. Ordinary failures clean up temporary artifacts;
process death may leave an orphan temporary directory. Only initialized files
become target paths. Callers synchronize the parent directory for durable
creation. File sync alone does not make the name durable.

Open never creates or repairs a file. A writer opens its read descriptor before
its write descriptor. Path replacement is outside the cooperation model. Close
does not sync. Reader and writer handles permit iteration; only writer handles
permit append, sync and recovery. Handles are caller-serialized, not internally
thread-safe. No other same-process descriptor for this inode may be closed
during an operation: POSIX fcntl locks are process-scoped. The application must
coordinate all same-inode handles and their lifetime, including reader closes.
After fork, children close inherited handles and reopen before library use.

## Append and failure

Append/appendv validate arguments and checked total length, allocate an iovec
slice array, acquire one writer session and obtain logical EOF. They check the
complete encoded file-size bound before writing. Each fragment uses slices of
the caller's payload, without payload copying or a message-sized allocation.
The payload total is limited by size_t and physical off_t capacity, not reserve.
Appendv accepts zero vectors as an empty message; vector length overflow fails
before writing. Caller buffers remain borrowed throughout the call.

One session covers all publications. Success returns zero and optionally writes
the first fragment's offset. Failure returns -1 and preserves the output offset.
An ordinary I/O failure before final publication leaves zero or more complete
fragments; no complete message from this call is delivered. There is no rollback
or automatic tail repair. A later message starts at EOF and abandons this one.

If session release fails, return -1 with EIO and poison the local handle. The
message might already be complete; blind retries can duplicate it. Close the
handle and reconcile the outcome before retrying. Close releases any remaining
lock. A poisoned handle rejects operations with EIO. Other errors preserve the
underlying errno, including allocation, capacity and I/O failures.

Completed visible messages are not necessarily durable. `ledger89_sync` holds a
writer session around append89_sync; success protects the current byte stream
under the substrate assumptions. It promises neither exactly which unsynced
messages a later crash retains nor exactly-once delivery.

## Iteration and views

Initialize a cursor to LEDGER89_BEGIN (16). Cursors must identify valid fragment
boundaries reached by iteration, not arbitrary byte offsets. `ledger89_next`
validates fragments until it finds a complete message, EOF, an incomplete suffix,
or an error. It reads headers and probes the final byte of each payload; the
append prefix invariant ensures preceding bytes exist. It does not checksum
payloads. Working memory is independent of message length.

- OK (0): return a view, advance cursor past the complete message.
- END (1): no pending message; retain the current boundary.
- PARTIAL (2): retain the unfinished message start (or torn new header start)
  for retry. Fully superseded abandoned messages may be skipped.
- -1: meaningful errno; cursor unchanged, view output NULL.

END/PARTIAL can change after later appends. Polling rescans the current unfinished
message. This avoids stateful cursors at the cost of repeated header reads for a
slow producer. Reads are not snapshots and may observe growth during scanning.

Views borrow the ledger handle. Close all views before closing it. Payload reads
return a positive byte count, zero at message end or for a zero-sized request,
or -1 on error. A call may stop at a fragment boundary. Errors preserve the view
position. Views return only previously validated complete messages; no tentative
payload delivery. All access must cease before recovery/shrink. Message offsets
are locators, not permanently unique IDs: truncation allows suffix offsets to be
reused. Retained completed message offsets do not change.

## Process death versus system crash

Process death releases the session. Live-kernel fragment publication is whole:
the next writer computes EOF from file size and appends a new start without
scanning. Readers skip the abandoned predecessor. They may report PARTIAL while
a writer is still active and cannot infer death from that status.

System-crash recovery can retain only part of a fragment. The application MUST
run exclusive recovery before allowing normal writers after such a restart.
The API cannot enforce cross-process lifecycle admission; no persistent clean
marker or extra lifecycle lock is provided.

## Exclusive recovery

Caller excludes all other access and new openers. Acquire the writer session,
scan from byte 16, preserve completed messages and abandoned fragments before
them. A final incomplete message is removed from its start. A torn first header
following a completed message is removed at that header position. A torn new
header following an unfinished message conservatively removes that unfinished
message too. Never trust offsets decoded from a short header.

Malformed complete headers fail EILSEQ without truncation. On a clean or
repaired tail, synchronize before returning success. Before/after offsets change
only on success. A truncate followed by failed sync may already change the live
file: stay in exclusive recovery and retry; never admit normal writers until
recovery succeeds. Directory persistence remains the application's concern.
Recovery requires an initial forward scan; storing message_start eliminates a
backward search but does not permit locating the final fragment from raw EOF.

## Compatibility and verification

This format/API replaces neither the legacy v2 API nor its existing adapters in
exploratory/libledger89. Select one library at link time. No implicit migration.
The focused gate is `sh scripts/test-fragmented-ledger.sh`, covering strict C89,
C++23 header linking, append regressions, process death, every prefix cut in a
multi-fragment fixture, concurrency, fault injection and structural validation.
Power-loss prefix-cut tests simulate the specified recovery outcomes; they do
not validate a filesystem or device's real crash persistence behavior.
