# Fragmented ledger acceptance scenarios

1. Create a ledger with reserve 32. Append a 100-byte message, then empty and
   short messages. Iterate complete messages with a 3-byte payload buffer.
2. Start a message, publish some complete fragments, terminate the process.
   Another process appends at logical EOF without scanning. Iteration skips the
   abandoned message, even when the replacement has the same length.
3. Cut the physical file at every byte boundary in a multi-fragment message,
   preserving the reserve. Exclusive recovery removes only the incomplete
   suffix, syncs the boundary, and permits correct later append and iteration.
4. Reject impossible lengths and continuation identities without truncating.
   A short final header is PARTIAL, never interpreted as an offset.
5. Concurrent processes append many fragmented messages. No fragments interleave
   within a completed message; offsets increase in iteration order.
6. Inject short writes, EINTR, ENOSPC, failed sync/publication/unlock; failure
   never makes an incomplete message deliverable. An unlock failure after final
   publication reports an uncertain outcome and poisons the local handle.
7. Invalid arguments, reserve mismatch, existing path creation, and malformed
   preamble fail before mutations. Initialization never installs an empty file.
8. Message payload reads use bounded memory after validation, including lengths
   above the reserve. Views borrow their ledger handle; recovery excludes views.

All system-crash assertions assume the append substrate's synchronized byte
prefix model. Process-kill tests are not evidence of power-loss persistence.
