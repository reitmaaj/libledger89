#ifndef LEDGER89_PRIV_H
#define LEDGER89_PRIV_H

/*
 * ledger89_priv.h - cross-TU private declarations. Never installed.
 *
 * The ledger owns one libappend89 stream (a reader handle and, on writer
 * handles, a writer handle). The stream carries a 16-byte preamble followed
 * by self-framed records: [size u64 BE][crc u64 BE][payload] with a
 * CRC-64/NVME payload checksum. The stream's writer lock is the ledger-wide
 * writer lock; the reserve protocol makes the committed end O(1).
 */

#include <errno.h>
#include <stdlib.h>

#include "append89.h"
#include "checksum89.h"
#include "ledger89.h"
#include "ledger89_u64.h"

#define LEDGER89_PRIV_PREAMBLE_SIZE 16U
#define LEDGER89_PRIV_HEADER_SIZE 16U
#define LEDGER89_PRIV_MAGIC 0x4c45444738395331ULL /* "LEDG89S1" */
#define LEDGER89_PRIV_CHUNK 4096U

struct ledger89
{
    append89 *r; /* reader stream, preamble-validated */
    append89 *w; /* writer stream, NULL on reader handles */
    int poisoned;
};

struct ledger89_iter
{
    ledger89 *owner;
    unsigned long long next_index; /* record number to fetch next */
    off_t pos;                     /* frame offset of the next record */
    off_t offset;                  /* current record payload offset */
    off_t length;                  /* current record payload length */
    off_t read_pos;                /* absolute offset of next byte */
    off_t read_left;               /* bytes remaining in the current record */
    unsigned long long checksum;   /* expected checksum of the current record */
    checksum89_crc64_nvme_ctx crc; /* running checksum of the current record */
    int positioned;                /* 1 when the iterator is on a record */
};

/* One decoded record frame header. */
struct ledger89_priv_header
{
    unsigned long long size;
    unsigned long long checksum;
};

/* ---- format codec (ledger89_format.c) ---- */

/* Encode the 16-byte preamble. reserve is the append reserve capacity. */
void ledger89_priv_preamble_encode(
    unsigned char out[LEDGER89_PRIV_PREAMBLE_SIZE], unsigned long long reserve);

/* Decode a preamble. ok is 1 when the magic matches and 0 otherwise. The
 * caller compares reserve against append89_capacity(). */
void ledger89_priv_preamble_decode(
    const unsigned char in[LEDGER89_PRIV_PREAMBLE_SIZE],
    unsigned long long *reserve, int *ok);

/* Encode one 16-byte record frame header. */
void ledger89_priv_header_encode(unsigned char out[LEDGER89_PRIV_HEADER_SIZE],
                                 const struct ledger89_priv_header *h);

/* Decode one 16-byte record frame header. */
void ledger89_priv_header_decode(
    const unsigned char in[LEDGER89_PRIV_HEADER_SIZE],
    struct ledger89_priv_header *h);

/* CRC-64/NVME of data as a native unsigned long long. */
unsigned long long ledger89_priv_crc_data(const void *data, size_t size);

/* ---- I/O helpers (ledger89.c) ---- */

/* Read exactly want bytes at *pos, advancing *pos. Returns 1 on success, 0 at
 * EOF, or -1 with errno on a read error. */
int ledger89_priv_read_exact(append89 *r, off_t *pos, unsigned char *out,
                             size_t want);

/* Stream the payload [offset, offset+length) and compute its CRC-64/NVME.
 * Returns 0 with *crc set, or -1 with errno EILSEQ when the stream ends
 * before the payload does, or errno from the read on a hard error. */
int ledger89_priv_extent_crc(append89 *r, off_t offset, off_t length,
                             unsigned long long *crc);

/* ---- shared open/usable (ledger89.c) ---- */

/* Validate the handle for the requested operation. writable selects a writer
 * handle requirement. Returns 0 or -1 with errno. */
int ledger89_priv_usable(const ledger89 *l, int writable);

/* Set errno and return -1. */
int ledger89_priv_error(int err);

/* ---- recovery (ledger89_recover.c) ---- */

/* Read-only prefix validation using the reader handle. Scans frames from byte
 * 16, validating header completeness, the size cap, stream bounds, overflow,
 * and payload checksums, stopping at the first invalid or partial frame. On
 * success writes the record count and committed end to valid_records and
 * committed_end. Returns 0, or -1 on a hard I/O error with errno set. */
int ledger89_priv_validate(ledger89 *l, off_t *valid_records,
                           off_t *committed_end);

/* Perform recovery assuming the writer lock is already held and the logical
 * size is size. Truncates the invalid suffix and synchronizes modified state.
 * On success writes the committed end before and after to before/after (when
 * non-NULL) and returns 0. */
int ledger89_priv_recover_core(ledger89 *l, off_t size, off_t *before,
                               off_t *after);

#endif /* LEDGER89_PRIV_H */
