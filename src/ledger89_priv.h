#ifndef LEDGER89_PRIV_H
#define LEDGER89_PRIV_H

/*
 * ledger89_priv.h - cross-TU private declarations. Never installed.
 *
 * The ledger owns four libappend89 handles (DATA reader/writer and INDEX
 * reader/writer). The INDEX writer lock is the ledger-wide writer lock. The
 * DATA stream carries raw payload; the INDEX stream carries fixed 32-byte
 * big-endian descriptors with a CRC-64/NVME payload checksum.
 */

#include <errno.h>
#include <stdlib.h>

#include "append89.h"
#include "cksum89.h"
#include "ledger89.h"
#include "ledger89_u64.h"

#define LEDGER89_PRIV_ENTRY_SIZE 32U
#define LEDGER89_PRIV_MAGIC 0x4c443839ULL /* "LD89" */
#define LEDGER89_PRIV_VERSION 1U
#define LEDGER89_PRIV_FLAGS 0U

/* Fixed reserve for the INDEX stream. Entries are one ENTRY_SIZE each, so a
 * small reserve keeps the INDEX file compact while admitting one entry per
 * append. */
#define LEDGER89_PRIV_INDEX_RESERVE 4096U

struct ledger89
{
    append89 *data_r;
    append89 *data_w;
    append89 *index_r;
    append89 *index_w;
    int poisoned;
};

struct ledger89_iter
{
    ledger89 *owner;
    unsigned long long next_index; /* record number to fetch next */
    off_t expected;                /* committed end before the next record */
    off_t offset;                  /* current record DATA offset */
    off_t length;                  /* current record payload length */
    off_t read_pos;                /* absolute DATA offset of next byte */
    off_t read_left;               /* bytes remaining in the current record */
    unsigned long long checksum;   /* expected checksum of the current record */
    cksum89_crc64_nvme_ctx crc;    /* running checksum of the current record */
    int positioned;                /* 1 when the iterator is on a record */
};

/* One decoded INDEX entry. */
struct ledger89_priv_entry
{
    unsigned long long offset;
    unsigned long long length;
    unsigned long long checksum;
};

/* ---- format codec (ledger89_format.c) ---- */

void ledger89_priv_entry_encode(unsigned char out[LEDGER89_PRIV_ENTRY_SIZE],
                                const struct ledger89_priv_entry *e);

/* Decode offset/length/checksum. header_ok is 1 when magic/version/flags are
 * valid and 0 otherwise. */
void ledger89_priv_entry_decode(const unsigned char in[LEDGER89_PRIV_ENTRY_SIZE],
                                struct ledger89_priv_entry *e, int *header_ok);

/* ---- checksum (ledger89_format.c) ---- */

unsigned long long ledger89_priv_crc_data(const void *data, size_t size);

/* ---- I/O helpers (ledger89.c) ---- */

/* Read one 32-byte INDEX entry at *pos, advancing *pos. complete is 1 when a
 * full entry was read and 0 at EOF or on a partial final entry. Returns 0 on
 * success or -1 with errno on a read error. */
int ledger89_priv_read_entry(append89 *index_r, off_t *pos,
                             unsigned char out[LEDGER89_PRIV_ENTRY_SIZE],
                             int *complete);

/* Stream the DATA extent [offset, offset+length) and compute its CRC-64/NVME.
 * Returns 0 with *crc set, or -1 with errno EILSEQ when DATA ends before the
 * extent does, or errno from the read on a hard error. */
int ledger89_priv_extent_crc(append89 *data_r, off_t offset, off_t length,
                             unsigned long long *crc);

/* ---- shared open/usable (ledger89.c) ---- */

/* Validate the handle for the requested operation. writable selects a writer
 * handle requirement. Returns 0 or -1 with errno. */
int ledger89_priv_usable(const ledger89 *l, int writable);

/* Set errno and return -1. */
int ledger89_priv_error(int err);

/* ---- recovery (ledger89_recover.c) ---- */

/* Read-only prefix validation using the reader handles. Scans INDEX entries
 * from byte zero, validating magic/version/flags, offset continuity, overflow,
 * DATA bounds, and payload checksums, stopping at the first invalid or partial
 * entry. On success writes the count and committed end to valid_entries and
 * committed_end, and sets tail to 1 when INDEX has bytes beyond the valid
 * prefix (a partial final entry or an invalid complete entry). Returns 0, or
 * -1 on a hard I/O error with errno set. */
int ledger89_priv_validate(ledger89 *l, off_t *valid_entries,
                           off_t *committed_end, int *tail);

/* Perform recovery assuming the INDEX writer lock is already held and the
 * INDEX logical size is index_size. Truncates a partial final INDEX entry,
 * validates the prefix, truncates any invalid INDEX suffix and uncommitted DATA
 * tail, and synchronizes modified files. On success writes the DATA size before
 * and after to before/after (when non-NULL) and returns 0. */
int ledger89_priv_recover_core(ledger89 *l, off_t index_size, off_t *before,
                               off_t *after);

#endif /* LEDGER89_PRIV_H */
