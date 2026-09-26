/* ledger89.c - single-file append-only ledger over libappend89 +
 * libchecksum89.
 *
 * One stream carries a 16-byte preamble followed by self-framed records
 * [size u64 BE][crc u64 BE][payload]. The stream's writer lock is the
 * ledger-wide writer lock. The reserve protocol publishes whole frames, so
 * the committed end is the logical size reported by append89_begin: O(1),
 * no scan. The public API is record-based and sequential.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ledger89_priv.h"

/* The empty GREEN_PURE annotation lets green treat a following genuinely pure
 * helper as pure, so call sites may use it in expression position. */
#define GREEN_PURE

int ledger89_priv_error(int err)
{
    errno = err;
    return -1;
}

static void ledger89_priv_poison(ledger89 *l)
{
    l->poisoned = 1;
    ledger89_priv_error(EIO);
}

static size_t ledger89_priv_want_size(off_t left, size_t bufsize)
{
    if (left > (off_t)bufsize)
    {
        return bufsize;
    }
    return (size_t)left;
}

static size_t ledger89_priv_take_size(size_t got, size_t room)
{
    if (got < room)
    {
        return got;
    }
    return room;
}

static unsigned long long
ledger89_priv_crc_final(checksum89_crc64_nvme_ctx *ctx)
{
    checksum89_u64 value;

    value = checksum89_crc64_nvme_final(ctx);
    return ((unsigned long long)value.hi << 32) | (unsigned long long)value.lo;
}

int ledger89_priv_usable(const ledger89 *l, int writable)
{
    if (l == NULL)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    if (l->poisoned)
    {
        ledger89_priv_error(EIO);
        return -1;
    }
    if (writable)
    {
        if (l->w == NULL)
        {
            ledger89_priv_error(EBADF);
            return -1;
        }
    }
    return 0;
}

/* ---- open / close ---- */

static int ledger89_priv_fail_open(ledger89 *l)
{
    int saved;

    saved = errno;
    ledger89_close(l);
    ledger89_priv_error(saved);
    return -1;
}

/* Validate the 16-byte preamble of an open stream. Fails EINVAL on a short,
 * mismatched, or wrong-reserve preamble without modifying the file. */
static int ledger89_priv_check_preamble(append89 *r)
{
    unsigned char raw[LEDGER89_PRIV_PREAMBLE_SIZE];
    unsigned long long reserve;
    size_t capacity;
    off_t pos;
    int ok;
    int got;

    pos = 0;
    got = ledger89_priv_read_exact(r, &pos, raw, LEDGER89_PRIV_PREAMBLE_SIZE);
    if (got != 1)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    ledger89_priv_preamble_decode(raw, &reserve, &ok);
    if (!ok)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    capacity = append89_capacity(r);
    if (reserve != (unsigned long long)capacity)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    return 0;
}

static int ledger89_priv_open(ledger89 **out, const char *name, int writable)
{
    ledger89 *l;
    int rc;

    if (out == NULL)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    *out = NULL;
    if (name == NULL)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    if (CHAR_BIT != 8)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    if (sizeof(off_t) > 8U)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    l = (ledger89 *)malloc(sizeof(*l));
    if (l == NULL)
    {
        ledger89_priv_error(ENOMEM);
        return -1;
    }
    memset(l, 0, sizeof(*l));
    rc = append89_open_reader(&l->r, name);
    if (rc == 0)
    {
        rc = ledger89_priv_check_preamble(l->r);
    }
    if (rc == 0)
    {
        if (writable)
        {
            rc = append89_open_writer(&l->w, name, (mode_t)0);
        }
    }
    if (rc != 0)
    {
        rc = ledger89_priv_fail_open(l);
        return rc;
    }
    *out = l;
    return 0;
}

int ledger89_open_reader(ledger89 **out, const char *name)
{
    int rc;

    rc = ledger89_priv_open(out, name, 0);
    return rc;
}

int ledger89_open_writer(ledger89 **out, const char *name)
{
    int rc;

    rc = ledger89_priv_open(out, name, 1);
    return rc;
}

static void ledger89_priv_release(ledger89 *l)
{
    append89_close(l->w);
    append89_close(l->r);
    free(l);
}

void ledger89_close(ledger89 *l)
{
    if (l != NULL)
    {
        ledger89_priv_release(l);
    }
}

/* ---- create ---- */

static int ledger89_priv_fail_create(char *path, char *dir)
{
    int saved;

    saved = errno;
    free(path);
    free(dir);
    ledger89_priv_error(saved);
    return -1;
}

static int ledger89_priv_fail_create_dir(char *tmp, char *path, char *dir)
{
    int saved;

    saved = errno;
    (void)rmdir(dir);
    free(tmp);
    free(path);
    free(dir);
    ledger89_priv_error(saved);
    return -1;
}

static size_t ledger89_priv_prefix_len(const char *slash, const char *name)
{
    if (slash == NULL)
    {
        return 0U;
    }
    return (size_t)(slash - name) + 1U;
}

/* Write and synchronize the preamble into a freshly created stream. */
static int ledger89_priv_write_preamble(append89 *w)
{
    unsigned char raw[LEDGER89_PRIV_PREAMBLE_SIZE];
    size_t capacity;
    int rc;

    capacity = append89_capacity(w);
    ledger89_priv_preamble_encode(raw, (unsigned long long)capacity);
    rc = append89_append(w, raw, LEDGER89_PRIV_PREAMBLE_SIZE, NULL);
    if (rc != 0)
    {
        return -1;
    }
    rc = append89_sync(w);
    if (rc != 0)
    {
        return -1;
    }
    return 0;
}

int ledger89_create(const char *name, mode_t mode)
{
    char *path;
    char *dir;
    char *tmp;
    char *made;
    const char *slash;
    size_t nl;
    size_t prefix;
    size_t dirlen;
    append89 *w;
    int rc;
    int saved;

    if (name == NULL)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    if (CHAR_BIT != 8)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    if (sizeof(off_t) > 8U)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    nl = strlen(name);
    if (nl > (size_t)-1 - 40U)
    {
        ledger89_priv_error(ENAMETOOLONG);
        return -1;
    }
    path = (char *)malloc(nl + 1U);
    dir = (char *)malloc(nl + 40U);
    if (path == NULL)
    {
        ledger89_priv_fail_create(path, dir);
        return -1;
    }
    if (dir == NULL)
    {
        ledger89_priv_fail_create(path, dir);
        return -1;
    }
    memcpy(path, name, nl + 1U);
    slash = strrchr(name, '/');
    prefix = ledger89_priv_prefix_len(slash, name);
    memcpy(dir, name, prefix);
    strcpy(dir + prefix, ".ledger89-XXXXXX");
    made = mkdtemp(dir);
    if (made == NULL)
    {
        rc = ledger89_priv_fail_create(path, dir);
        return rc;
    }
    dirlen = strlen(dir);
    tmp = (char *)malloc(dirlen + 16U);
    if (tmp == NULL)
    {
        rc = ledger89_priv_fail_create_dir(tmp, path, dir);
        return rc;
    }
    strcpy(tmp, dir);
    strcat(tmp, "/ledger");
    w = NULL;
    rc = append89_open_writer(&w, tmp, mode);
    if (rc == 0)
    {
        rc = ledger89_priv_write_preamble(w);
    }
    append89_close(w);
    if (rc == 0)
    {
        rc = link(tmp, path);
    }
    saved = errno;
    (void)unlink(tmp);
    (void)rmdir(dir);
    free(tmp);
    free(path);
    free(dir);
    errno = saved;
    return rc;
}

/* ---- shared I/O helpers ---- */

GREEN_PURE
static size_t ledger89_priv_grow_got(size_t got, ssize_t n)
{
    return got + (size_t)n;
}

int ledger89_priv_read_exact(append89 *r, off_t *pos, unsigned char *out,
                             size_t want)
{
    size_t got;
    ssize_t n;

    got = 0U;
    while (got < want)
    {
        n = append89_read(r, pos, out + got, want - got);
        if (n < 0)
        {
            return -1;
        }
        if (n == 0)
        {
            return 0;
        }
        got = ledger89_priv_grow_got(got, n);
    }
    return 1;
}

static int ledger89_priv_crc_step(append89 *r, off_t *pos, off_t *left,
                                  checksum89_crc64_nvme_ctx *ctx)
{
    unsigned char buf[LEDGER89_PRIV_CHUNK];
    size_t want;
    ssize_t n;

    want = ledger89_priv_want_size(*left, sizeof(buf));
    n = append89_read(r, pos, buf, want);
    if (n < 0)
    {
        return -1;
    }
    if (n == 0)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    checksum89_crc64_nvme_update(ctx, buf, (size_t)n);
    *left = *left - n;
    return 0;
}

int ledger89_priv_extent_crc(append89 *r, off_t offset, off_t length,
                             unsigned long long *crc)
{
    off_t pos;
    off_t left;
    checksum89_crc64_nvme_ctx ctx;
    int rc;

    checksum89_crc64_nvme_init(&ctx);
    pos = offset;
    left = length;
    while (left > 0)
    {
        rc = ledger89_priv_crc_step(r, &pos, &left, &ctx);
        if (rc != 0)
        {
            return -1;
        }
    }
    *crc = ledger89_priv_crc_final(&ctx);
    return 0;
}

/* ---- append ---- */

static int ledger89_priv_finish(ledger89 *l, int rc)
{
    int saved;
    int end_rc;

    saved = errno;
    end_rc = append89_end(l->w);
    if (end_rc != 0)
    {
        ledger89_priv_poison(l);
        return -1;
    }
    errno = saved;
    return rc;
}

/* Build the frame header || payload in one contiguous buffer and publish it
 * as one candidate, then sync. The writer lock is already held and committed
 * equals the logical size. The buffer copy is the cast-free way to submit one
 * candidate without discarding the payload's const qualifier. */
static int ledger89_priv_append_frame(ledger89 *l, const void *data,
                                      size_t size)
{
    struct ledger89_priv_header h;
    unsigned char *frame;
    int rc;

    frame = (unsigned char *)malloc(LEDGER89_PRIV_HEADER_SIZE + size);
    if (frame == NULL)
    {
        ledger89_priv_error(ENOMEM);
        return -1;
    }
    h.size = (unsigned long long)size;
    h.checksum = ledger89_priv_crc_data(data, size);
    ledger89_priv_header_encode(frame, &h);
    if (size > 0U)
    {
        memcpy(frame + LEDGER89_PRIV_HEADER_SIZE, data, size);
    }
    rc = append89_append(l->w, frame, LEDGER89_PRIV_HEADER_SIZE + size, NULL);
    free(frame);
    if (rc != 0)
    {
        return -1;
    }
    rc = append89_sync(l->w);
    if (rc != 0)
    {
        return -1;
    }
    return 0;
}

/* The payload offset of a record whose frame starts at the committed end. */
GREEN_PURE
static ledger89_offset ledger89_priv_payload_off(off_t end)
{
    return end + (off_t)LEDGER89_PRIV_HEADER_SIZE;
}

/* Report EOVERFLOW and release the held writer session. */
static int ledger89_priv_end_overflow(ledger89 *l)
{
    int rc;

    rc = ledger89_priv_error(EOVERFLOW);
    rc = ledger89_priv_finish(l, rc);
    return rc;
}

int ledger89_append(ledger89 *l, const void *data, size_t size,
                    ledger89_offset *offset)
{
    off_t end;
    size_t capacity;
    int rc;

    rc = ledger89_priv_usable(l, 1);
    if (rc != 0)
    {
        return -1;
    }
    if (size > 0U)
    {
        if (data == NULL)
        {
            ledger89_priv_error(EINVAL);
            return -1;
        }
    }
    capacity = append89_capacity(l->w);
    if (capacity < LEDGER89_PRIV_HEADER_SIZE)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    if (size > capacity - LEDGER89_PRIV_HEADER_SIZE)
    {
        ledger89_priv_error(E2BIG);
        return -1;
    }
    rc = append89_begin(l->w, &end);
    if (rc != 0)
    {
        return -1;
    }
    if (end > (off_t)(ledger89_u64_off_max() -
                      (unsigned long long)LEDGER89_PRIV_HEADER_SIZE))
    {
        rc = ledger89_priv_end_overflow(l);
        return rc;
    }
    rc = ledger89_priv_append_frame(l, data, size);
    rc = ledger89_priv_finish(l, rc);
    if (rc == 0)
    {
        if (offset != NULL)
        {
            *offset = ledger89_priv_payload_off(end);
        }
    }
    return rc;
}

/* ---- iteration ---- */

int ledger89_iter_begin(ledger89 *l, ledger89_iter **out)
{
    ledger89_iter *it;
    int rc;

    if (out == NULL)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    *out = NULL;
    rc = ledger89_priv_usable(l, 0);
    if (rc != 0)
    {
        return -1;
    }
    it = (ledger89_iter *)malloc(sizeof(*it));
    if (it == NULL)
    {
        ledger89_priv_error(ENOMEM);
        return -1;
    }
    memset(it, 0, sizeof(*it));
    it->owner = l;
    it->pos = (off_t)LEDGER89_PRIV_PREAMBLE_SIZE;
    *out = it;
    return LEDGER89_OK;
}

static int ledger89_priv_frame_fits(off_t frame, off_t len)
{
    unsigned long long room;

    room = ledger89_u64_off_max();
    if ((unsigned long long)len >
        room - (unsigned long long)LEDGER89_PRIV_HEADER_SIZE)
    {
        return 0;
    }
    if ((unsigned long long)frame >
        room - (unsigned long long)len -
            (unsigned long long)LEDGER89_PRIV_HEADER_SIZE)
    {
        return 0;
    }
    return 1;
}

int ledger89_iter_next(ledger89_iter *it)
{
    unsigned char raw[LEDGER89_PRIV_HEADER_SIZE];
    struct ledger89_priv_header h;
    off_t pos;
    off_t len;
    size_t capacity;
    int got;
    int rc;

    if (it == NULL)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    rc = ledger89_priv_usable(it->owner, 0);
    if (rc != 0)
    {
        return -1;
    }
    pos = it->pos;
    got = ledger89_priv_read_exact(it->owner->r, &pos, raw,
                                   LEDGER89_PRIV_HEADER_SIZE);
    if (got != 1)
    {
        if (got == 0)
        {
            return LEDGER89_END;
        }
        return -1;
    }
    ledger89_priv_header_decode(raw, &h);
    rc = ledger89_u64_to_off(h.size, &len);
    if (rc != 0)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    capacity = append89_capacity(it->owner->r);
    if ((unsigned long long)len +
            (unsigned long long)LEDGER89_PRIV_HEADER_SIZE >
        (unsigned long long)capacity)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    got = ledger89_priv_frame_fits(it->pos, len);
    if (got == 0)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    it->offset = it->pos + (off_t)LEDGER89_PRIV_HEADER_SIZE;
    it->length = len;
    it->read_pos = it->offset;
    it->read_left = len;
    it->checksum = h.checksum;
    checksum89_crc64_nvme_init(&it->crc);
    it->pos = it->offset + len;
    it->positioned = 1;
    ++it->next_index;
    return LEDGER89_OK;
}

unsigned long long ledger89_iter_index(const ledger89_iter *it)
{
    if (it == NULL)
    {
        return 0ULL;
    }
    if (it->positioned)
    {
        return it->next_index - 1ULL;
    }
    return 0ULL;
}

ledger89_offset ledger89_iter_offset(const ledger89_iter *it)
{
    if (it == NULL)
    {
        return (ledger89_offset)-1;
    }
    if (it->positioned)
    {
        return it->offset;
    }
    return (ledger89_offset)-1;
}

unsigned long long ledger89_iter_length(const ledger89_iter *it)
{
    if (it == NULL)
    {
        return 0ULL;
    }
    if (it->positioned)
    {
        return (unsigned long long)it->length;
    }
    return 0ULL;
}

ssize_t ledger89_iter_read(ledger89_iter *it, void *data, size_t size)
{
    size_t want;
    ssize_t n;
    off_t pos;
    unsigned long long crc;
    int rc;

    if (it == NULL)
    {
        ledger89_priv_error(EINVAL);
        return (ssize_t)-1;
    }
    if (size > 0U)
    {
        if (data == NULL)
        {
            ledger89_priv_error(EINVAL);
            return (ssize_t)-1;
        }
    }
    rc = ledger89_priv_usable(it->owner, 0);
    if (rc != 0)
    {
        return -1;
    }
    if (!it->positioned)
    {
        return 0;
    }
    if (size == 0U)
    {
        return 0;
    }
    if (it->read_left == 0)
    {
        return 0;
    }
    want = ledger89_priv_take_size(size, (size_t)it->read_left);
    pos = it->read_pos;
    n = append89_read(it->owner->r, &pos, data, want);
    if (n < 0)
    {
        return -1;
    }
    if (n == 0)
    {
        ledger89_priv_error(EILSEQ);
        return (ssize_t)-1;
    }
    checksum89_crc64_nvme_update(&it->crc, data, (size_t)n);
    it->read_pos = pos;
    it->read_left -= n;
    if (it->read_left == 0)
    {
        crc = ledger89_priv_crc_final(&it->crc);
        if (crc != it->checksum)
        {
            ledger89_priv_error(EILSEQ);
            return (ssize_t)-1;
        }
    }
    return n;
}

void ledger89_iter_close(ledger89_iter *it)
{
    free(it);
}

/* ---- recovery ---- */

int ledger89_recover(ledger89 *l, ledger89_offset *before,
                     ledger89_offset *after)
{
    off_t size;
    off_t b;
    off_t a;
    int rc;

    rc = ledger89_priv_usable(l, 1);
    if (rc != 0)
    {
        return -1;
    }
    rc = append89_begin(l->w, &size);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_priv_recover_core(l, size, &b, &a);
    rc = ledger89_priv_finish(l, rc);
    if (rc == 0)
    {
        if (before != NULL)
        {
            *before = b;
        }
        if (after != NULL)
        {
            *after = a;
        }
    }
    return rc;
}
