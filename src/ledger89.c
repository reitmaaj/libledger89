/* Fragmented message layer. The append substrate alone owns byte publication. */
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "append89.h"
#include "ledger89.h"
#include "ledger89_u64.h"

static void ledger89_u64_store_be(unsigned char out[8],
                                  unsigned long long value)
{
    int i;
    for (i = 7; i >= 0; --i)
    {
        out[i] = (unsigned char)(value & 0xffULL);
        value >>= 8;
    }
}

#define LEDGER89_PRIV_HEADER 24U

struct ledger89
{
    append89 *reader;
    append89 *writer;
    size_t reserve;
    int poisoned;
};

struct ledger89_message
{
    ledger89 *owner;
    off_t start;
    off_t length;
    off_t left;
    off_t next;
    off_t fragment_left;
};

typedef struct
{
    off_t start;
    off_t length;
    off_t end;
} ledger89_priv_record;

static int ledger89_priv_error(int err)
{
    errno = err;
    return -1;
}

static off_t ledger89_priv_maxoff(void)
{
    off_t value;
    size_t i;
    value = 1;
    for (i = 1U; i < sizeof(off_t) * CHAR_BIT - 1U; ++i)
    {
        value = value * 2 + 1;
    }
    return value;
}

static unsigned long long ledger89_priv_from_off(off_t value)
{
    unsigned char bytes[8];
    int i;
    for (i = 7; i >= 0; --i)
    {
        bytes[i] = (unsigned char)(value % 256);
        value /= 256;
    }
    return ledger89_u64_load_be(bytes);
}

static int ledger89_priv_to_off(unsigned long long value, off_t *out)
{
    unsigned char bytes[8];
    off_t result;
    int i;
    if (value > ledger89_priv_from_off(ledger89_priv_maxoff()))
    {
        return ledger89_priv_error(EOVERFLOW);
    }
    ledger89_u64_store_be(bytes, value);
    result = 0;
    for (i = 0; i < 8; ++i)
    {
        result = result * 256 + bytes[i];
    }
    *out = result;
    return 0;
}

static int ledger89_priv_reserve(size_t *reserve)
{
    unsigned long long value;
    off_t converted;
    if (*reserve == 0U)
    {
        *reserve = (size_t)APPEND89_RESERVE;
    }
    if (*reserve <= LEDGER89_PRIV_HEADER || sizeof(off_t) > 8U || CHAR_BIT != 8)
    {
        return ledger89_priv_error(EINVAL);
    }
    value = (unsigned long long)*reserve;
    if (ledger89_priv_to_off(value, &converted) != 0)
    {
        return ledger89_priv_error(EOVERFLOW);
    }
    return 0;
}

/* Returns the bytes actually obtained, including EOF inside a header. */
static int ledger89_priv_exact(append89 *r, off_t *at, unsigned char *data,
                               size_t size, size_t *got)
{
    ssize_t n;
    *got = 0U;
    while (*got < size)
    {
        n = append89_read(r, at, data + *got, size - *got);
        if (n < 0)
        {
            return -1;
        }
        if (n == 0)
        {
            break;
        }
        *got += (size_t)n;
    }
    return 0;
}

static int ledger89_priv_validate(append89 *r, size_t reserve)
{
    unsigned char header[16];
    off_t at;
    size_t got;
    unsigned long long expected;
    at = 0;
    if (ledger89_priv_exact(r, &at, header, sizeof(header), &got) != 0)
    {
        return -1;
    }
    expected = (unsigned long long)reserve;
    if (got != sizeof(header) || memcmp(header, "LEDG89F1", 8U) != 0 ||
        ledger89_u64_load_be(header + 8) != expected)
    {
        return ledger89_priv_error(EINVAL);
    }
    return 0;
}

static int ledger89_priv_usable(const ledger89 *a, int writable)
{
    if (a == NULL)
    {
        return ledger89_priv_error(EINVAL);
    }
    if (a->poisoned)
    {
        return ledger89_priv_error(EIO);
    }
    if (writable && a->writer == NULL)
    {
        return ledger89_priv_error(EBADF);
    }
    return 0;
}

static int ledger89_priv_finish(ledger89 *a, int rc)
{
    int saved;
    saved = errno;
    if (append89_end(a->writer) != 0)
    {
        a->poisoned = 1;
        return ledger89_priv_error(EIO);
    }
    errno = saved;
    return rc;
}

static int ledger89_priv_open(ledger89 **out, const char *path, size_t reserve,
                              int writable)
{
    ledger89 *a;
    int saved;
    if (out == NULL)
    {
        return ledger89_priv_error(EINVAL);
    }
    *out = NULL;
    if (path == NULL)
    {
        return ledger89_priv_error(EINVAL);
    }
    if (ledger89_priv_reserve(&reserve) != 0)
    {
        return -1;
    }
    a = (ledger89 *)malloc(sizeof(*a));
    if (a == NULL)
    {
        return ledger89_priv_error(ENOMEM);
    }
    memset(a, 0, sizeof(*a));
    a->reserve = reserve;
    if (append89_open_reader_reserve(&a->reader, path, reserve) != 0 ||
        ledger89_priv_validate(a->reader, reserve) != 0 ||
        (writable && append89_open_writer_reserve(&a->writer, path,
                                                 (mode_t)0, reserve) != 0))
    {
        saved = errno;
        ledger89_close(a);
        return ledger89_priv_error(saved);
    }
    *out = a;
    return 0;
}

int ledger89_open_reader(ledger89 **out, const char *path, size_t reserve)
{
    return ledger89_priv_open(out, path, reserve, 0);
}

int ledger89_open_writer(ledger89 **out, const char *path, size_t reserve)
{
    return ledger89_priv_open(out, path, reserve, 1);
}

void ledger89_close(ledger89 *a)
{
    if (a != NULL)
    {
        append89_close(a->writer);
        append89_close(a->reader);
        free(a);
    }
}

int ledger89_create(const char *path, mode_t mode, size_t reserve)
{
    char *directory;
    char *file;
    const char *slash;
    size_t len;
    size_t prefix;
    append89 *w;
    unsigned char header[16];
    unsigned long long value;
    int rc;
    int saved;
    if (path == NULL)
    {
        return ledger89_priv_error(EINVAL);
    }
    if (ledger89_priv_reserve(&reserve) != 0)
    {
        return -1;
    }
    len = strlen(path);
    if (len > (size_t)-1 - 40U)
    {
        return ledger89_priv_error(ENAMETOOLONG);
    }
    directory = (char *)malloc(len + 40U);
    file = (char *)malloc(len + 40U);
    if (directory == NULL || file == NULL)
    {
        free(directory);
        free(file);
        return ledger89_priv_error(ENOMEM);
    }
    slash = strrchr(path, '/');
    prefix = slash == NULL ? 0U : (size_t)(slash - path) + 1U;
    memcpy(directory, path, prefix);
    strcpy(directory + prefix, ".ledger89-XXXXXX");
    if (mkdtemp(directory) == NULL)
    {
        saved = errno;
        free(directory);
        free(file);
        return ledger89_priv_error(saved);
    }
    strcpy(file, directory);
    strcat(file, "/data");
    memcpy(header, "LEDG89F1", 8U);
    value = (unsigned long long)reserve;
    ledger89_u64_store_be(header + 8, value);
    w = NULL;
    rc = append89_open_writer_reserve(&w, file, mode, reserve);
    if (rc == 0)
    {
        rc = append89_append(w, header, sizeof(header), NULL);
    }
    if (rc == 0)
    {
        rc = append89_sync(w);
    }
    if (rc == 0)
    {
        rc = link(file, path);
    }
    saved = errno;
    append89_close(w);
    (void)unlink(file);
    (void)rmdir(directory);
    free(file);
    free(directory);
    errno = saved;
    return rc;
}

static int ledger89_priv_total(const struct iovec *iov, int count, size_t *total)
{
    int i;
    *total = 0U;
    if (count < 0 || count == INT_MAX || (count > 0 && iov == NULL))
    {
        return ledger89_priv_error(EINVAL);
    }
    for (i = 0; i < count; ++i)
    {
        if (iov[i].iov_len > 0U && iov[i].iov_base == NULL)
        {
            return ledger89_priv_error(EINVAL);
        }
        if (iov[i].iov_len > (size_t)-1 - *total)
        {
            return ledger89_priv_error(EOVERFLOW);
        }
        *total += iov[i].iov_len;
    }
    return 0;
}

static int ledger89_priv_room(off_t start, size_t total, size_t reserve)
{
    size_t fragments;
    off_t payload;
    off_t frames;
    off_t room;
    unsigned long long value;
    fragments = total == 0U ? 1U : 1U + (total - 1U) / (reserve - 24U);
    value = (unsigned long long)total;
    if (ledger89_priv_to_off(value, &payload) != 0)
    {
        return ledger89_priv_error(EOVERFLOW);
    }
    value = (unsigned long long)fragments;
    if (ledger89_priv_to_off(value, &frames) != 0)
    {
        return ledger89_priv_error(EOVERFLOW);
    }
    room = ledger89_priv_maxoff() - start - (off_t)reserve;
    if (payload > room || frames > (room - payload) / 24)
    {
        return ledger89_priv_error(EOVERFLOW);
    }
    return 0;
}

static int ledger89_priv_write(ledger89 *a, const struct iovec *iov,
                               struct iovec *parts, size_t total, off_t start)
{
    unsigned char header[24];
    unsigned long long value;
    size_t left;
    size_t fragment;
    size_t need;
    size_t used;
    size_t take;
    int source;
    int count;
    left = total;
    source = 0;
    used = 0U;
    ledger89_u64_store_be(header, ledger89_priv_from_off(start));
    value = (unsigned long long)total;
    ledger89_u64_store_be(header + 8, value);
    do
    {
        fragment = left < a->reserve - 24U ? left : a->reserve - 24U;
        value = (unsigned long long)fragment;
        ledger89_u64_store_be(header + 16, value);
        parts[0].iov_base = header;
        parts[0].iov_len = sizeof(header);
        need = fragment;
        count = 1;
        while (need > 0U)
        {
            if (used == iov[source].iov_len)
            {
                ++source;
                used = 0U;
                continue;
            }
            take = iov[source].iov_len - used;
            if (take > need)
            {
                take = need;
            }
            parts[count].iov_base = (unsigned char *)iov[source].iov_base + used;
            parts[count].iov_len = take;
            ++count;
            used += take;
            need -= take;
        }
        if (append89_appendv(a->writer, parts, count, NULL) != 0)
        {
            return -1;
        }
        left -= fragment;
    } while (left > 0U);
    return 0;
}

int ledger89_appendv(ledger89 *a, const struct iovec *iov, int iovcnt,
                     ledger89_offset *offset)
{
    struct iovec *parts;
    size_t total;
    off_t start;
    int rc;
    int saved;
    if (ledger89_priv_usable(a, 1) != 0 ||
        ledger89_priv_total(iov, iovcnt, &total) != 0)
    {
        return -1;
    }
    if ((size_t)iovcnt + 1U > (size_t)-1 / sizeof(*parts))
    {
        return ledger89_priv_error(EOVERFLOW);
    }
    parts = (struct iovec *)malloc(((size_t)iovcnt + 1U) * sizeof(*parts));
    if (parts == NULL)
    {
        return ledger89_priv_error(ENOMEM);
    }
    rc = append89_begin(a->writer, &start);
    if (rc == 0)
    {
        rc = ledger89_priv_room(start, total, a->reserve);
        if (rc == 0)
        {
            rc = ledger89_priv_write(a, iov, parts, total, start);
        }
        rc = ledger89_priv_finish(a, rc);
    }
    saved = errno;
    free(parts);
    errno = saved;
    if (rc == 0 && offset != NULL)
    {
        *offset = start;
    }
    return rc;
}

int ledger89_append(ledger89 *a, const void *data, size_t size,
                    ledger89_offset *offset)
{
    struct iovec iov;
    iov.iov_base = (void *)data;
    iov.iov_len = size;
    return ledger89_appendv(a, &iov, 1, offset);
}

int ledger89_sync(ledger89 *a)
{
    int rc;
    if (ledger89_priv_usable(a, 1) != 0 || append89_begin(a->writer, NULL) != 0)
    {
        return -1;
    }
    rc = append89_sync(a->writer);
    return ledger89_priv_finish(a, rc);
}

/* Scan just headers and the last byte of each payload. Retained bytes never
 * change and this format promises prefix recovery, not corruption detection. */
static int ledger89_priv_scan(ledger89 *a, off_t position,
                              ledger89_priv_record *record, off_t *retry)
{
    unsigned char header[24];
    unsigned char last;
    size_t got;
    off_t at;
    off_t start;
    off_t total;
    off_t fragment;
    off_t active;
    off_t length;
    off_t accumulated;
    off_t probe;
    ssize_t n;
    active = 0;
    length = 0;
    accumulated = 0;
    for (;;)
    {
        *retry = active != 0 ? active : position;
        at = position;
        if (ledger89_priv_exact(a->reader, &at, header, sizeof(header), &got) != 0)
        {
            return -1;
        }
        if (got != sizeof(header))
        {
            return got == 0U && active == 0 ? LEDGER89_END : LEDGER89_PARTIAL;
        }
        if (ledger89_priv_to_off(ledger89_u64_load_be(header), &start) != 0 ||
            ledger89_priv_to_off(ledger89_u64_load_be(header + 8), &total) != 0 ||
            ledger89_priv_to_off(ledger89_u64_load_be(header + 16), &fragment) != 0)
        {
            return ledger89_priv_error(EILSEQ);
        }
        if (start < LEDGER89_BEGIN || start > position ||
            fragment > (off_t)(a->reserve - 24U) || fragment > total ||
            (total != 0 && fragment == 0) ||
            fragment > ledger89_priv_maxoff() - at)
        {
            return ledger89_priv_error(EILSEQ);
        }
        if (start == position)
        {
            active = start;
            length = total;
            accumulated = 0;
        }
        else if (active == 0 || start != active || total != length)
        {
            return ledger89_priv_error(EILSEQ);
        }
        if (fragment > length - accumulated)
        {
            return ledger89_priv_error(EILSEQ);
        }
        *retry = active;
        if (fragment > 0)
        {
            probe = at + fragment - 1;
            n = append89_read(a->reader, &probe, &last, 1U);
            if (n < 0)
            {
                return -1;
            }
            if (n == 0)
            {
                return LEDGER89_PARTIAL;
            }
        }
        accumulated += fragment;
        position = at + fragment;
        if (accumulated == length)
        {
            record->start = active;
            record->length = length;
            record->end = position;
            return LEDGER89_OK;
        }
    }
}

int ledger89_next(ledger89 *a, ledger89_offset *cursor, ledger89_message **out)
{
    ledger89_priv_record record;
    ledger89_message *message;
    off_t retry;
    int rc;
    if (out == NULL)
    {
        return ledger89_priv_error(EINVAL);
    }
    *out = NULL;
    if (ledger89_priv_usable(a, 0) != 0)
    {
        return -1;
    }
    if (cursor == NULL || *cursor < LEDGER89_BEGIN)
    {
        return ledger89_priv_error(EINVAL);
    }
    rc = ledger89_priv_scan(a, *cursor, &record, &retry);
    if (rc == LEDGER89_END || rc == LEDGER89_PARTIAL)
    {
        *cursor = retry;
    }
    if (rc != LEDGER89_OK)
    {
        return rc;
    }
    message = (ledger89_message *)malloc(sizeof(*message));
    if (message == NULL)
    {
        return ledger89_priv_error(ENOMEM);
    }
    message->owner = a;
    message->start = record.start;
    message->length = record.length;
    message->left = record.length;
    message->next = record.start;
    message->fragment_left = 0;
    *out = message;
    *cursor = record.end;
    return LEDGER89_OK;
}

unsigned long long ledger89_message_length(const ledger89_message *message)
{
    return message == NULL ? 0ULL : ledger89_priv_from_off(message->length);
}

ledger89_offset ledger89_message_offset(const ledger89_message *message)
{
    return message == NULL ? (off_t)-1 : message->start;
}

ssize_t ledger89_message_read(ledger89_message *message, void *data, size_t size)
{
    unsigned char header[24];
    size_t got;
    size_t want;
    size_t remaining;
    off_t at;
    off_t fragment;
    unsigned long long value;
    ssize_t n;
    if (message == NULL || (size != 0U && data == NULL))
    {
        return (ssize_t)ledger89_priv_error(EINVAL);
    }
    if (ledger89_priv_usable(message->owner, 0) != 0)
    {
        return -1;
    }
    if (size == 0U || message->left == 0)
    {
        return 0;
    }
    at = message->next;
    fragment = message->fragment_left;
    if (fragment == 0)
    {
        if (ledger89_priv_exact(message->owner->reader, &at, header,
                                sizeof(header), &got) != 0)
        {
            return -1;
        }
        if (got != sizeof(header) ||
            ledger89_u64_load_be(header) != ledger89_priv_from_off(message->start) ||
            ledger89_u64_load_be(header + 8) != ledger89_priv_from_off(message->length) ||
            ledger89_priv_to_off(ledger89_u64_load_be(header + 16), &fragment) != 0 ||
            fragment <= 0 || fragment > message->left ||
            fragment > (off_t)(message->owner->reserve - 24U))
        {
            return (ssize_t)ledger89_priv_error(EILSEQ);
        }
    }
    value = ledger89_priv_from_off(fragment);
    if (ledger89_u64_to_size(value, &remaining) != 0)
    {
        remaining = (size_t)-1;
    }
    want = size < remaining ? size : remaining;
    n = append89_read(message->owner->reader, &at, data, want);
    if (n < 0)
    {
        return -1;
    }
    if (n == 0)
    {
        return (ssize_t)ledger89_priv_error(EILSEQ);
    }
    message->next = at;
    message->fragment_left = fragment - n;
    message->left -= n;
    return n;
}

void ledger89_message_close(ledger89_message *message)
{
    free(message);
}

int ledger89_recover(ledger89 *a, ledger89_offset *before, ledger89_offset *after)
{
    ledger89_priv_record record;
    off_t end;
    off_t position;
    off_t retry;
    off_t target;
    int rc;
    if (ledger89_priv_usable(a, 1) != 0 || append89_begin(a->writer, &end) != 0)
    {
        return -1;
    }
    position = LEDGER89_BEGIN;
    target = end;
    for (;;)
    {
        rc = ledger89_priv_scan(a, position, &record, &retry);
        if (rc != LEDGER89_OK)
        {
            break;
        }
        position = record.end;
    }
    if (rc == LEDGER89_PARTIAL)
    {
        target = retry;
        rc = append89_truncate(a->writer, target);
    }
    else if (rc == LEDGER89_END)
    {
        rc = 0;
    }
    if (rc == 0)
    {
        rc = append89_sync(a->writer);
    }
    rc = ledger89_priv_finish(a, rc);
    if (rc == 0)
    {
        if (before != NULL)
        {
            *before = end;
        }
        if (after != NULL)
        {
            *after = target;
        }
    }
    return rc;
}
