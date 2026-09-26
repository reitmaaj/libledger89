# Two-file libledger89

Strict C89 syntax; POSIX file API via libappend89; dependencies libappend89 and
libchecksum89. A durable append-only sequence of opaque byte records in two
libappend89 byte streams: `DATA` (raw payloads) and `INDEX` (fixed 32-byte
big-endian descriptors with CRC-64/NVME checksums).

Single public essentials header `include/ledger89.h`; one named extension
`include/ledger89/admin.h`; cross-TU private declarations in
`src/ledger89_priv.h`. The INDEX writer lock is the ledger-wide writer lock.
Specification in `spec/ledger89-spec.md` is normative.

Do not confuse this implementation with `exploratory/libledger89` (legacy v2) or
`libledger89-core`; their formats and APIs are incompatible and their public
symbols overlap, so select one library and never link two.

Changes follow scenario -> failing test -> implementation -> verification.
Four spaces, Allman braces. Do not commit or push without explicit permission.
