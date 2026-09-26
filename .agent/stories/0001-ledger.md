# Stakeholders and user stories

## Application writer

AS an application process
I WANT to append one opaque record that commits only after its frame is
durably published
SO THAT no reader ever observes a record whose payload could disappear.

## Application reader

AS a reader (possibly concurrent with writers)
I WANT to iterate exactly the committed records in order, never partial
payloads or torn frames
SO THAT the data I return is a valid, complete prefix of the append history.

## Operator

AS an operator recovering after a crash or corruption
I WANT exclusive recovery to deterministically restore the longest valid
prefix
SO THAT a damaged frame never yields a malformed ledger, and recovery is
idempotent.

## Maintainer

AS a maintainer
I WANT structural corruption (oversize frame, truncated header, payload past
end, overflow, checksum mismatch) to stop recovery at the first affected frame
SO THAT later valid-looking records never survive the loss of an earlier one.

## Sequential consumer

AS a consumer without an index
I WANT append and forward iteration to need no full-file scans
SO THAT writers can locate the committed end in O(1) and random-access APIs
that would require scanning (count, read, length, offset_of) are not part of
the surface.
