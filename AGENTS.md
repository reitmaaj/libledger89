# Single-file libledger89

Strict C89 syntax; POSIX file API via libappend89; dependencies libappend89 and
libchecksum89. A durable append-only sequence of opaque byte records in one
libappend89 byte stream: a 16-byte preamble (`"LEDG89S1"` magic + reserve)
followed by self-framed records `[size u64 BE][crc u64 BE][payload]` with
CRC-64/NVME checksums (format S1).

Single public essentials header `include/ledger89.h`; one named extension
`include/ledger89/admin.h`; cross-TU private declarations in
`src/ledger89_priv.h`. The stream's writer lock is the ledger-wide writer lock.
Specification in `spec/ledger89-spec.md` is normative.

Random access by record number (count, read, length, offset_of) is deliberately
absent: locating record N requires a scan. Sequential access is iteration.
The two-file (DATA+INDEX) format L1 lives under git tag `two-file-v1`.

Do not confuse this implementation with the fragmented single-file experiment
or `libledger89-core`; their formats and APIs are incompatible and their public
symbols overlap, so select one library and never link two.

Changes follow scenario -> failing test -> implementation -> verification.
Four spaces, Allman braces. Do not commit or push without explicit permission.
