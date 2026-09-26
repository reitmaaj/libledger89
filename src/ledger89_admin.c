/* ledger89_admin.c - administrative inspection and repair over the essentials
 * API, using the library-private validation primitive. */

#include <string.h>

#include "ledger89/admin.h"
#include "ledger89_priv.h"

static int ledger89_admin_priv_poison_fail(ledger89 *l)
{
    l->poisoned = 1;
    (void)ledger89_priv_error(EIO);
    return -1;
}

static int ledger89_admin_priv_fail_close(ledger89 *l)
{
    int saved;

    saved = errno;
    ledger89_close(l);
    (void)ledger89_priv_error(saved);
    return -1;
}

static int ledger89_admin_priv_inspect(ledger89 *l,
                                       ledger89_admin_report *report)
{
    off_t data_size;
    off_t valid;
    off_t committed_end;
    int tail;
    int rc;

    rc = append89_begin(l->data_w, &data_size);
    if (rc != 0)
    {
        return -1;
    }
    rc = append89_end(l->data_w);
    if (rc != 0)
    {
        rc = ledger89_admin_priv_poison_fail(l);
        return rc;
    }
    rc = ledger89_priv_validate(l, &valid, &committed_end, &tail);
    if (rc != 0)
    {
        return -1;
    }
    report->valid_end = committed_end;
    report->records = (unsigned long long)valid;
    report->incomplete_tail = 0;
    if (tail)
    {
        report->incomplete_tail = 1;
    }
    else if (committed_end != data_size)
    {
        report->incomplete_tail = 1;
    }
    return 0;
}

int ledger89_admin_check(const char *name, ledger89_admin_report *report)
{
    ledger89 *l;
    int rc;
    int saved;

    if (report != NULL)
    {
        memset(report, 0, sizeof(*report));
    }
    rc = ledger89_open_writer(&l, name);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_admin_priv_inspect(l, report);
    saved = errno;
    ledger89_close(l);
    if (rc != 0)
    {
        ledger89_priv_error(saved);
        return -1;
    }
    return 0;
}

int ledger89_admin_repair(const char *name, ledger89_admin_report *report)
{
    ledger89 *l;
    ledger89_admin_report pre;
    int rc;
    int saved;

    if (report != NULL)
    {
        memset(report, 0, sizeof(*report));
    }
    rc = ledger89_open_writer(&l, name);
    if (rc != 0)
    {
        return -1;
    }
    memset(&pre, 0, sizeof(pre));
    rc = ledger89_admin_priv_inspect(l, &pre);
    if (rc != 0)
    {
        rc = ledger89_admin_priv_fail_close(l);
        return rc;
    }
    rc = ledger89_recover(l, NULL, NULL);
    if (rc != 0)
    {
        rc = ledger89_admin_priv_fail_close(l);
        return rc;
    }
    rc = ledger89_admin_priv_inspect(l, report);
    saved = errno;
    ledger89_close(l);
    if (rc != 0)
    {
        ledger89_priv_error(saved);
        return -1;
    }
    /* Report what the repair observed (a tail present before recovery), not
     * the clean post-repair state. */
    report->incomplete_tail = pre.incomplete_tail;
    return 0;
}
