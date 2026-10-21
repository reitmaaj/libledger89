# Direct read by payload offset: acceptance criteria

## Must exhibit

1. `ledger89_read_at` returns the complete payload of the committed record
   whose payload begins at the supplied payload offset, byte-for-byte, without
   scanning earlier records.

2. The function validates the frame header at `payload_offset - 16`, the
   reserve size cap, `off_t` representability and overflow, the payload extent
   within the committed stream, and the CRC-64/NVME payload checksum before
   producing the payload.

3. It returns the payload length on success and 0 for a zero-length record.

4. It works on reader handles and writer handles.

5. Empty records, consecutive empty records, binary payloads, and the maximum
   permitted payload length read back exactly.

6. The caller's buffer is unchanged when the call fails because of a bad
   checksum, bad framing, truncated payload, or insufficient capacity.

## Must reject

1. A NULL ledger, a NULL data pointer with nonzero size, or an offset below the
   first possible payload offset (32) MUST fail with EINVAL.

2. An offset whose frame header begins at or past the logical end MUST fail
   with EINVAL.

3. A partial frame header, a frame whose size violates the reserve cap, a frame
   that overflows `off_t`, a payload that ends before its declared length, or a
   checksum mismatch MUST fail with EILSEQ.

4. A buffer smaller than the complete payload MUST fail with E2BIG and MUST NOT
   return a partial record.

5. A read I/O failure MUST propagate the underlying errno.

6. An allocation failure while preparing the verified payload MUST fail with
   ENOMEM and leave the caller's buffer unchanged.

## Unacceptable behavior

1. Returning payload bytes that have not been checksum-verified.

2. Treating a short buffer as a successful partial read of a complete record.

3. Modifying the caller's buffer on any failure path.

4. Implementing `ledger89_read_at` by scanning from byte 16 or by requiring
   record-number lookup.

5. Reintroducing scan-requiring `ledger89_count`, `ledger89_read`,
   `ledger89_length`, or `ledger89_offset_of`.
