/* Administrative inspection and repair over the essentials API. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ledger89/admin.h"
#include "ledger89_u64.h"

static int ledger89_admin_priv_error(int err)
{
    errno = err;
    return -1;
}

/* Read the 16-byte preamble and recover the immutable reserve capacity. */
static int ledger89_admin_priv_discover_reserve(const char *path,
                                                size_t *reserve)
{
    unsigned char preamble[16];
    struct stat st;
    unsigned long long value;
    size_t got;
    ssize_t n;
    int fd;
    int saved;

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return -1;
    }
    if (fstat(fd, &st) != 0)
    {
        saved = errno;
        (void)close(fd);
        return ledger89_admin_priv_error(saved);
    }
    if (!S_ISREG(st.st_mode))
    {
        (void)close(fd);
        return ledger89_admin_priv_error(EINVAL);
    }
    got = 0U;
    while (got < sizeof(preamble))
    {
        n = read(fd, preamble + got, sizeof(preamble) - got);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            saved = errno;
            (void)close(fd);
            return ledger89_admin_priv_error(saved);
        }
        if (n == 0)
        {
            break;
        }
        got += (size_t)n;
    }
    saved = errno;
    (void)close(fd);
    errno = saved;
    if (got != sizeof(preamble) || memcmp(preamble, "LEDG89F1", 8U) != 0)
    {
        return ledger89_admin_priv_error(EINVAL);
    }
    value = ledger89_u64_load_be(preamble + 8);
    if (ledger89_u64_to_size(value, reserve) != 0)
    {
        return ledger89_admin_priv_error(EOVERFLOW);
    }
    return 0;
}

/* Scan a ledger from its first boundary. Returns 0 on a clean END, 1 on a
 * trailing PARTIAL record, or -1 on error. Writes the record count and the
 * valid frontier (end of the largest complete-record prefix). */
static int ledger89_admin_priv_scan(ledger89 *a, unsigned long *records,
                                    ledger89_offset *end)
{
    ledger89_message *m;
    ledger89_offset cursor;
    unsigned long count;
    int rc;
    int saved;

    cursor = LEDGER89_BEGIN;
    count = 0UL;
    for (;;)
    {
        rc = ledger89_next(a, &cursor, &m);
        if (rc == LEDGER89_OK)
        {
            ++count;
            ledger89_message_close(m);
            continue;
        }
        *records = count;
        *end = cursor;
        if (rc == LEDGER89_END)
        {
            return 0;
        }
        if (rc == LEDGER89_PARTIAL)
        {
            return 1;
        }
        saved = errno;
        return ledger89_admin_priv_error(saved);
    }
}

int ledger89_admin_check(const char *path, ledger89_admin_report *report)
{
    ledger89 *a;
    ledger89_offset end;
    unsigned long records;
    size_t reserve;
    int rc;
    int saved;

    if (report != NULL)
    {
        memset(report, 0, sizeof(*report));
    }
    if (ledger89_admin_priv_discover_reserve(path, &reserve) != 0)
    {
        return -1;
    }
    if (ledger89_open_reader(&a, path, reserve) != 0)
    {
        return -1;
    }
    rc = ledger89_admin_priv_scan(a, &records, &end);
    if (rc < 0)
    {
        saved = errno;
        ledger89_close(a);
        return ledger89_admin_priv_error(saved);
    }
    if (report != NULL)
    {
        report->valid_end = end;
        report->records = records;
        report->incomplete_tail = rc == 1 ? 1 : 0;
    }
    ledger89_close(a);
    return 0;
}

int ledger89_admin_repair(const char *path, ledger89_admin_report *report)
{
    ledger89 *a;
    ledger89_offset before;
    ledger89_offset after;
    ledger89_offset end;
    unsigned long records;
    size_t reserve;
    int rc;
    int saved;
    int removed;

    if (report != NULL)
    {
        memset(report, 0, sizeof(*report));
    }
    if (ledger89_admin_priv_discover_reserve(path, &reserve) != 0)
    {
        return -1;
    }
    if (ledger89_open_writer(&a, path, reserve) != 0)
    {
        return -1;
    }
    if (ledger89_recover(a, &before, &after) != 0)
    {
        saved = errno;
        ledger89_close(a);
        return ledger89_admin_priv_error(saved);
    }
    removed = after < before ? 1 : 0;
    rc = ledger89_admin_priv_scan(a, &records, &end);
    if (rc != 0)
    {
        saved = rc < 0 ? errno : EILSEQ;
        ledger89_close(a);
        return ledger89_admin_priv_error(saved);
    }
    if (report != NULL)
    {
        report->valid_end = after;
        report->records = records;
        report->incomplete_tail = removed;
    }
    ledger89_close(a);
    return 0;
}
