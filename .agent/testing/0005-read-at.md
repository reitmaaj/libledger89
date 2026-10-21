# Direct read by payload offset

## Scene: complete record from a known payload offset

SCENARIO: A caller has a payload offset (from append or iterator) and reads the
record directly, without scanning from the beginning.
GIVEN a committed record whose payload begins at offset O
WHEN the caller invokes `ledger89_read_at` with O and a buffer large enough to
hold the payload
THEN the complete payload is returned byte-for-byte
AND the frame header at O - 16, the reserve cap, the stream bounds, and the
CRC-64/NVME payload checksum are verified first
AND the return value is the payload length.

SCENARIO: Reading an empty record directly.
GIVEN a committed record with length zero
WHEN the caller reads it by its payload offset
THEN the empty-input checksum is verified
AND the result is success with return value 0.

SCENARIO: Reading the maximum-size record directly.
GIVEN a record whose payload length is the reserve minus the frame header
WHEN the caller reads it by payload offset into a buffer of that length
THEN the complete payload is returned and verified.

SCENARIO: Direct reads work on both handle modes.
GIVEN a reader handle and a writer handle for the same ledger
WHEN either reads a record by payload offset
THEN the same verified payload is returned.

## Scene: rejected offsets and frames

SCENARIO: An offset cannot name a payload.
GIVEN a ledger and an offset below the first possible payload offset
WHEN the caller invokes `ledger89_read_at`
THEN the call fails with EINVAL before returning any payload.

SCENARIO: An offset lies at or beyond the end of the committed stream.
GIVEN a ledger and a payload offset whose frame header starts at or past the
logical end
WHEN the caller invokes `ledger89_read_at`
THEN the call fails with EINVAL.

SCENARIO: A frame is structurally invalid.
GIVEN a partial frame header, an oversize payload length, an overflowing frame,
or a payload that ends before its declared length
WHEN the caller reads it by payload offset
THEN the call fails with EILSEQ
AND the caller's buffer is unchanged.

SCENARIO: A payload does not match its checksum.
GIVEN a record whose stored checksum does not match its payload
WHEN the caller reads it by payload offset
THEN the call fails with EILSEQ
AND the caller's buffer is unchanged.

## Scene: buffer and argument handling

SCENARIO: The buffer is too small for the complete payload.
GIVEN a nonempty record and a buffer shorter than its payload
WHEN the caller reads it by payload offset
THEN the call fails with E2BIG
AND the caller's buffer is unchanged.

SCENARIO: A nonempty record is requested with zero capacity.
GIVEN a nonempty record and `size == 0`
WHEN the caller reads it by payload offset
THEN the call fails with E2BIG.

SCENARIO: Invalid arguments.
GIVEN a NULL ledger, a NULL data pointer with nonzero size, or a negative
payload offset
WHEN the caller invokes `ledger89_read_at`
THEN the call fails with EINVAL.

SCENARIO: A read I/O failure is reported without corruption.
GIVEN a read syscall failure while reading a record by payload offset
WHEN the caller invokes `ledger89_read_at`
THEN the call fails with the underlying errno
AND the caller's buffer is unchanged.

SCENARIO: Private payload storage cannot be allocated.
GIVEN a nonempty record and an allocation failure while reading its payload
WHEN the caller invokes `ledger89_read_at`
THEN the call fails with ENOMEM
AND the caller's buffer is unchanged.
