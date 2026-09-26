/* ledger89_recover.c - INDEX prefix validation and exclusive recovery. */

#include "ledger89_priv.h"

/* Validate one INDEX entry. Returns 1 to continue, 0 when the valid prefix
 * ends, or -1 on a hard I/O error. */
static int ledger89_priv_validate_step(ledger89 *l, off_t *pos, off_t *expected,
                                       off_t *valid)
{
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    struct ledger89_priv_entry e;
    off_t off;
    off_t len;
    unsigned long long crc;
    int header_ok;
    int complete;
    int rc;

    rc = ledger89_priv_read_entry(l->index_r, pos, raw, &complete);
    if (rc != 0)
    {
        return -1;
    }
    if (!complete)
    {
        return 0;
    }
    ledger89_priv_entry_decode(raw, &e, &header_ok);
    if (!header_ok)
    {
        return 0;
    }
    rc = ledger89_u64_to_off(e.offset, &off);
    if (rc != 0)
    {
        return 0;
    }
    rc = ledger89_u64_to_off(e.length, &len);
    if (rc != 0)
    {
        return 0;
    }
    if (off != *expected)
    {
        return 0;
    }
    if (off > (off_t)(ledger89_u64_off_max() - (unsigned long long)len))
    {
        return 0;
    }
    rc = ledger89_priv_extent_crc(l->data_r, off, len, &crc);
    if (rc != 0)
    {
        if (errno != EILSEQ)
        {
            return -1;
        }
        return 0;
    }
    if (crc != e.checksum)
    {
        return 0;
    }
    *expected = off + len;
    *valid = *valid + 1;
    return 1;
}

static int ledger89_priv_validate_loop(ledger89 *l, off_t *pos, off_t *expected,
                                       off_t *valid)
{
    int rc;

    for (;;)
    {
        rc = ledger89_priv_validate_step(l, pos, expected, valid);
        if (rc <= 0)
        {
            return rc;
        }
    }
}

static int ledger89_priv_tail_flag(off_t pos, off_t valid)
{
    if (pos > valid * (off_t)LEDGER89_PRIV_ENTRY_SIZE)
    {
        return 1;
    }
    return 0;
}

static int ledger89_priv_recover_fail_end(ledger89 *l)
{
    (void)append89_end(l->data_w);
    return -1;
}

static int ledger89_priv_recover_poison_fail(ledger89 *l)
{
    l->poisoned = 1;
    (void)ledger89_priv_error(EIO);
    return -1;
}

int ledger89_priv_validate(ledger89 *l, off_t *valid_entries,
                           off_t *committed_end, int *tail)
{
    off_t pos;
    off_t expected;
    off_t valid;
    int rc;

    pos = 0;
    expected = 0;
    valid = 0;
    rc = ledger89_priv_validate_loop(l, &pos, &expected, &valid);
    if (rc != 0)
    {
        return -1;
    }
    *valid_entries = valid;
    *committed_end = expected;
    *tail = ledger89_priv_tail_flag(pos, valid);
    return 0;
}

int ledger89_priv_recover_core(ledger89 *l, off_t index_size, off_t *before,
                               off_t *after)
{
    off_t data_size;
    off_t valid;
    off_t committed_end;
    off_t target;
    int tail;
    int index_modified;
    int data_modified;
    int rc;

    rc = append89_begin(l->data_w, &data_size);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_priv_validate(l, &valid, &committed_end, &tail);
    if (rc != 0)
    {
        rc = ledger89_priv_recover_fail_end(l);
        return rc;
    }
    index_modified = 0;
    data_modified = 0;
    target = valid * (off_t)LEDGER89_PRIV_ENTRY_SIZE;
    if (target != index_size)
    {
        rc = append89_truncate(l->index_w, target);
        if (rc != 0)
        {
            rc = ledger89_priv_recover_fail_end(l);
            return rc;
        }
        index_modified = 1;
    }
    if (committed_end != data_size)
    {
        rc = append89_truncate(l->data_w, committed_end);
        if (rc != 0)
        {
            rc = ledger89_priv_recover_fail_end(l);
            return rc;
        }
        data_modified = 1;
    }
    if (index_modified)
    {
        rc = append89_sync(l->index_w);
        if (rc != 0)
        {
            rc = ledger89_priv_recover_fail_end(l);
            return rc;
        }
    }
    if (data_modified)
    {
        rc = append89_sync(l->data_w);
        if (rc != 0)
        {
            rc = ledger89_priv_recover_fail_end(l);
            return rc;
        }
    }
    rc = append89_end(l->data_w);
    if (rc != 0)
    {
        rc = ledger89_priv_recover_poison_fail(l);
        return rc;
    }
    if (before != NULL)
    {
        *before = data_size;
    }
    if (after != NULL)
    {
        *after = committed_end;
    }
    return 0;
}
