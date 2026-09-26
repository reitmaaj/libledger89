/* ledger89_recover.c - record prefix validation and exclusive recovery. */

#include "ledger89_priv.h"

/* Validate one frame. Returns 1 to continue, 0 when the valid prefix ends, or
 * -1 on a hard I/O error. */
static int ledger89_priv_validate_step(ledger89 *l, off_t *pos, off_t *valid)
{
    unsigned char raw[LEDGER89_PRIV_HEADER_SIZE];
    struct ledger89_priv_header h;
    off_t len;
    off_t frame;
    off_t cursor;
    size_t capacity;
    unsigned long long crc;
    int got;
    int rc;

    frame = *pos;
    cursor = frame;
    got =
        ledger89_priv_read_exact(l->r, &cursor, raw, LEDGER89_PRIV_HEADER_SIZE);
    if (got != 1)
    {
        if (got == 0)
        {
            return 0;
        }
        return -1;
    }
    ledger89_priv_header_decode(raw, &h);
    rc = ledger89_u64_to_off(h.size, &len);
    if (rc != 0)
    {
        return 0;
    }
    capacity = append89_capacity(l->r);
    if ((unsigned long long)len +
            (unsigned long long)LEDGER89_PRIV_HEADER_SIZE >
        (unsigned long long)capacity)
    {
        return 0;
    }
    if ((unsigned long long)frame >
        ledger89_u64_off_max() - (unsigned long long)len -
            (unsigned long long)LEDGER89_PRIV_HEADER_SIZE)
    {
        return 0;
    }
    rc = ledger89_priv_extent_crc(
        l->r, frame + (off_t)LEDGER89_PRIV_HEADER_SIZE, len, &crc);
    if (rc != 0)
    {
        if (errno != EILSEQ)
        {
            return -1;
        }
        return 0;
    }
    if (crc != h.checksum)
    {
        return 0;
    }
    *pos = frame + (off_t)LEDGER89_PRIV_HEADER_SIZE + len;
    *valid = *valid + 1;
    return 1;
}

static int ledger89_priv_validate_loop(ledger89 *l, off_t *pos, off_t *valid)
{
    int rc;

    for (;;)
    {
        rc = ledger89_priv_validate_step(l, pos, valid);
        if (rc <= 0)
        {
            return rc;
        }
    }
}

int ledger89_priv_validate(ledger89 *l, off_t *valid_records,
                           off_t *committed_end)
{
    off_t pos;
    off_t valid;
    int rc;

    pos = (off_t)LEDGER89_PRIV_PREAMBLE_SIZE;
    valid = 0;
    rc = ledger89_priv_validate_loop(l, &pos, &valid);
    if (rc != 0)
    {
        return -1;
    }
    *valid_records = valid;
    *committed_end = pos;
    return 0;
}

int ledger89_priv_recover_core(ledger89 *l, off_t size, off_t *before,
                               off_t *after)
{
    off_t valid;
    off_t committed_end;
    int modified;
    int rc;

    rc = ledger89_priv_validate(l, &valid, &committed_end);
    if (rc != 0)
    {
        return -1;
    }
    (void)valid;
    modified = 0;
    if (committed_end != size)
    {
        rc = append89_truncate(l->w, committed_end);
        if (rc != 0)
        {
            return -1;
        }
        modified = 1;
    }
    if (modified)
    {
        rc = append89_sync(l->w);
        if (rc != 0)
        {
            return -1;
        }
    }
    if (before != NULL)
    {
        *before = size;
    }
    if (after != NULL)
    {
        *after = committed_end;
    }
    return 0;
}
