# Stakeholders and user stories

## Application writer

AS an application process
I WANT to append opaque records that commit only after both DATA and INDEX are
durable
SO THAT no reader ever observes a record whose payload could disappear.

## Application reader

AS a reader (possibly concurrent with writers)
I WANT to observe exactly the committed records, never partial payloads or
uncommitted DATA tails
SO THAT the data I return is a valid, complete prefix of the append history.

## Operator

AS an operator recovering after a crash
I WANT exclusive recovery to deterministically restore the longest valid prefix
SO THAT a torn append never yields a malformed ledger, and recovery is
idempotent.

## Maintainer

AS a maintainer
I WANT structural corruption (misaligned entries, gaps, overlaps, overflow,
beyond-EOF, checksum mismatch) to stop recovery at the first affected record
SO THAT later valid-looking records never survive the loss of an earlier one.
