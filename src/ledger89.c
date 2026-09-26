/* ledger89.c - two-file append-only ledger over libappend89 + libchecksum89.
 *
 * DATA carries raw payload; INDEX carries fixed 32-byte descriptors. The INDEX
 * writer lock is the ledger-wide writer lock. The public API is record-based.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ledger89_priv.h"

#define LEDGER89_PRIV_CHUNK 4096U

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

static size_t ledger89_priv_chunk_size(size_t left)
{
    if (left > (size_t)APPEND89_RESERVE)
    {
        return (size_t)APPEND89_RESERVE;
    }
    return left;
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
        if (l->index_w == NULL)
        {
            ledger89_priv_error(EBADF);
            return -1;
        }
    }
    return 0;
}

/* ---- path construction ---- */

static char *ledger89_priv_suffix(const char *name, const char *suffix)
{
    size_t nl;
    size_t sl;
    char *path;

    nl = strlen(name);
    sl = strlen(suffix);
    if (nl > (size_t)-1 - sl - 1U)
    {
        return NULL;
    }
    path = (char *)malloc(nl + sl + 1U);
    if (path == NULL)
    {
        return NULL;
    }
    memcpy(path, name, nl);
    memcpy(path + nl, suffix, sl + 1U);
    return path;
}

static void ledger89_priv_fail_free_two(char *a, char *b)
{
    free(a);
    free(b);
    ledger89_priv_error(ENOMEM);
}

static int ledger89_priv_fail_open(ledger89 *l, char *data_path,
                                   char *index_path)
{
    int saved;

    saved = errno;
    ledger89_close(l);
    free(data_path);
    free(index_path);
    ledger89_priv_error(saved);
    return -1;
}

static int ledger89_priv_open_handles(ledger89 *l, const char *data_path,
                                      const char *index_path, int writable)
{
    int rc;

    rc = append89_open_reader(&l->data_r, data_path);
    if (rc != 0)
    {
        return -1;
    }
    rc = append89_open_reader_reserve(&l->index_r, index_path,
                                      LEDGER89_PRIV_INDEX_RESERVE);
    if (rc != 0)
    {
        return -1;
    }
    if (writable)
    {
        rc = append89_open_writer(&l->data_w, data_path, (mode_t)0);
        if (rc != 0)
        {
            return -1;
        }
        rc = append89_open_writer_reserve(&l->index_w, index_path, (mode_t)0,
                                          LEDGER89_PRIV_INDEX_RESERVE);
        if (rc != 0)
        {
            return -1;
        }
    }
    return 0;
}

/* ---- open / close ---- */

static int ledger89_priv_open(ledger89 **out, const char *name, int writable)
{
    ledger89 *l;
    char *data_path;
    char *index_path;
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
    data_path = ledger89_priv_suffix(name, ".data");
    index_path = ledger89_priv_suffix(name, ".index");
    if (data_path == NULL)
    {
        ledger89_priv_fail_free_two(data_path, index_path);
        return -1;
    }
    if (index_path == NULL)
    {
        ledger89_priv_fail_free_two(data_path, index_path);
        return -1;
    }
    l = (ledger89 *)malloc(sizeof(*l));
    if (l == NULL)
    {
        ledger89_priv_fail_free_two(data_path, index_path);
        return -1;
    }
    memset(l, 0, sizeof(*l));
    rc = ledger89_priv_open_handles(l, data_path, index_path, writable);
    if (rc != 0)
    {
        rc = ledger89_priv_fail_open(l, data_path, index_path);
        return rc;
    }
    free(data_path);
    free(index_path);
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
    append89_close(l->data_w);
    append89_close(l->data_r);
    append89_close(l->index_w);
    append89_close(l->index_r);
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

static int ledger89_priv_fail_create(char *data_path, char *index_path,
                                     char *dir)
{
    int saved;

    saved = errno;
    free(data_path);
    free(index_path);
    free(dir);
    ledger89_priv_error(saved);
    return -1;
}

static int ledger89_priv_fail_create_dir(char *tmp_data, char *tmp_index,
                                         char *data_path, char *index_path,
                                         char *dir)
{
    int saved;

    saved = errno;
    (void)rmdir(dir);
    free(tmp_data);
    free(tmp_index);
    free(data_path);
    free(index_path);
    free(dir);
    ledger89_priv_error(saved);
    return -1;
}

static void ledger89_priv_fail_free_three(char *a, char *b, char *c)
{
    free(a);
    free(b);
    free(c);
    ledger89_priv_error(ENOMEM);
}

static size_t ledger89_priv_prefix_len(const char *slash, const char *name)
{
    if (slash == NULL)
    {
        return 0U;
    }
    return (size_t)(slash - name) + 1U;
}

static int ledger89_priv_create_file(const char *path, mode_t mode,
                                     size_t reserve, int use_reserve)
{
    append89 *w;
    int rc;

    w = NULL;
    if (use_reserve)
    {
        rc = append89_open_writer_reserve(&w, path, mode, reserve);
    }
    else
    {
        rc = append89_open_writer(&w, path, mode);
    }
    if (rc == 0)
    {
        append89_close(w);
    }
    return rc;
}

static int ledger89_priv_create_files(mode_t mode, const char *tmp_data,
                                      const char *tmp_index,
                                      const char *data_path,
                                      const char *index_path)
{
    int rc;

    rc = ledger89_priv_create_file(tmp_data, mode, 0U, 0);
    if (rc == 0)
    {
        rc = ledger89_priv_create_file(tmp_index, mode,
                                       LEDGER89_PRIV_INDEX_RESERVE, 1);
    }
    if (rc == 0)
    {
        rc = link(tmp_data, data_path);
    }
    if (rc == 0)
    {
        rc = link(tmp_index, index_path);
        if (rc != 0)
        {
            unlink(data_path);
        }
    }
    return rc;
}

int ledger89_create(const char *name, mode_t mode)
{
    char *data_path;
    char *index_path;
    char *dir;
    char *tmp_data;
    char *tmp_index;
    char *made;
    const char *slash;
    size_t nl;
    size_t prefix;
    size_t dirlen;
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
    data_path = ledger89_priv_suffix(name, ".data");
    index_path = ledger89_priv_suffix(name, ".index");
    dir = (char *)malloc(nl + 40U);
    if (data_path == NULL)
    {
        ledger89_priv_fail_free_three(data_path, index_path, dir);
        return -1;
    }
    if (index_path == NULL)
    {
        ledger89_priv_fail_free_three(data_path, index_path, dir);
        return -1;
    }
    if (dir == NULL)
    {
        ledger89_priv_fail_free_three(data_path, index_path, dir);
        return -1;
    }
    slash = strrchr(name, '/');
    prefix = ledger89_priv_prefix_len(slash, name);
    memcpy(dir, name, prefix);
    strcpy(dir + prefix, ".ledger89-XXXXXX");
    made = mkdtemp(dir);
    if (made == NULL)
    {
        rc = ledger89_priv_fail_create(data_path, index_path, dir);
        return rc;
    }
    dirlen = strlen(dir);
    tmp_data = (char *)malloc(dirlen + 16U);
    tmp_index = (char *)malloc(dirlen + 16U);
    if (tmp_data == NULL)
    {
        rc = ledger89_priv_fail_create_dir(tmp_data, tmp_index, data_path,
                                           index_path, dir);
        return rc;
    }
    if (tmp_index == NULL)
    {
        rc = ledger89_priv_fail_create_dir(tmp_data, tmp_index, data_path,
                                           index_path, dir);
        return rc;
    }
    strcpy(tmp_data, dir);
    strcat(tmp_data, "/data");
    strcpy(tmp_index, dir);
    strcat(tmp_index, "/index");
    rc = ledger89_priv_create_files(mode, tmp_data, tmp_index, data_path,
                                    index_path);
    saved = errno;
    (void)unlink(tmp_data);
    (void)unlink(tmp_index);
    (void)rmdir(dir);
    free(tmp_data);
    free(tmp_index);
    free(data_path);
    free(index_path);
    free(dir);
    errno = saved;
    return rc;
}

/* ---- shared I/O helpers ---- */

static int
ledger89_priv_read_entry_chunk(append89 *index_r, off_t *pos,
                               unsigned char out[LEDGER89_PRIV_ENTRY_SIZE],
                               size_t *got)
{
    ssize_t n;

    n = append89_read(index_r, pos, out + *got,
                      LEDGER89_PRIV_ENTRY_SIZE - *got);
    if (n < 0)
    {
        return -1;
    }
    if (n == 0)
    {
        return 0;
    }
    *got = *got + (size_t)n;
    return 1;
}

int ledger89_priv_read_entry(append89 *index_r, off_t *pos,
                             unsigned char out[LEDGER89_PRIV_ENTRY_SIZE],
                             int *complete)
{
    size_t got;
    int rc;

    got = 0U;
    *complete = 0;
    while (got < LEDGER89_PRIV_ENTRY_SIZE)
    {
        rc = ledger89_priv_read_entry_chunk(index_r, pos, out, &got);
        if (rc <= 0)
        {
            return rc;
        }
    }
    *complete = 1;
    return 0;
}

static int ledger89_priv_crc_step(append89 *data_r, off_t *pos, off_t *left,
                                  checksum89_crc64_nvme_ctx *ctx)
{
    unsigned char buf[LEDGER89_PRIV_CHUNK];
    size_t want;
    ssize_t n;

    want = ledger89_priv_want_size(*left, sizeof(buf));
    n = append89_read(data_r, pos, buf, want);
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

int ledger89_priv_extent_crc(append89 *data_r, off_t offset, off_t length,
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
        rc = ledger89_priv_crc_step(data_r, &pos, &left, &ctx);
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
    end_rc = append89_end(l->index_w);
    if (end_rc != 0)
    {
        ledger89_priv_poison(l);
        return -1;
    }
    errno = saved;
    return rc;
}

/* Committed end derived from the last complete INDEX entry. */
static int ledger89_priv_frontier(ledger89 *l, off_t index_size,
                                  off_t *committed)
{
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    struct ledger89_priv_entry e;
    unsigned long long end;
    off_t pos;
    int header_ok;
    int complete;
    int rc;

    if (index_size < (off_t)LEDGER89_PRIV_ENTRY_SIZE)
    {
        *committed = 0;
        return 0;
    }
    pos = (index_size / (off_t)LEDGER89_PRIV_ENTRY_SIZE - 1) *
          (off_t)LEDGER89_PRIV_ENTRY_SIZE;
    rc = ledger89_priv_read_entry(l->index_r, &pos, raw, &complete);
    if (rc != 0)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    if (!complete)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    ledger89_priv_entry_decode(raw, &e, &header_ok);
    if (!header_ok)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    end = e.offset + e.length;
    rc = ledger89_u64_to_off(end, committed);
    if (rc != 0)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    return 0;
}

static int ledger89_priv_append_chunk(append89 *data_w, const unsigned char **p,
                                      size_t *left)
{
    size_t chunk;
    int rc;

    chunk = ledger89_priv_chunk_size(*left);
    rc = append89_append(data_w, *p, chunk, NULL);
    if (rc != 0)
    {
        return -1;
    }
    *p = *p + chunk;
    *left = *left - chunk;
    return 0;
}

static int ledger89_priv_append_all(append89 *data_w, const void *data,
                                    size_t size)
{
    const unsigned char *p;
    size_t left;
    int rc;

    p = (const unsigned char *)data;
    left = size;
    while (left > 0U)
    {
        rc = ledger89_priv_append_chunk(data_w, &p, &left);
        if (rc != 0)
        {
            return -1;
        }
    }
    return 0;
}

/* Append payload, sync DATA, publish the INDEX entry, sync INDEX. Assumes the
 * INDEX lock is held and committed == size(DATA). */
static int ledger89_priv_append_payload(ledger89 *l, const void *data,
                                        size_t size, off_t committed)
{
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    struct ledger89_priv_entry entry;
    int rc;

    rc = ledger89_priv_append_all(l->data_w, data, size);
    if (rc != 0)
    {
        return -1;
    }
    rc = append89_sync(l->data_w);
    if (rc != 0)
    {
        return -1;
    }
    entry.offset = ledger89_u64_from_off(committed);
    entry.length = (unsigned long long)size;
    entry.checksum = ledger89_priv_crc_data(data, size);
    ledger89_priv_entry_encode(raw, &entry);
    rc = append89_append(l->index_w, raw, LEDGER89_PRIV_ENTRY_SIZE, NULL);
    if (rc != 0)
    {
        return -1;
    }
    rc = append89_sync(l->index_w);
    if (rc != 0)
    {
        return -1;
    }
    return 0;
}

/* Ensure DATA is aligned with the INDEX frontier, recovering when needed. */
static int ledger89_priv_align_data(ledger89 *l, off_t index_size,
                                    off_t *committed)
{
    off_t data_size;
    off_t before;
    off_t after;
    int rc;

    rc = append89_begin(l->data_w, &data_size);
    if (rc != 0)
    {
        return rc;
    }
    if (data_size != *committed)
    {
        rc = append89_end(l->data_w);
        if (rc != 0)
        {
            ledger89_priv_poison(l);
            return -1;
        }
        rc = ledger89_priv_recover_core(l, index_size, &before, &after);
        if (rc != 0)
        {
            return -1;
        }
        *committed = after;
        return 0;
    }
    rc = append89_end(l->data_w);
    if (rc != 0)
    {
        ledger89_priv_poison(l);
        return -1;
    }
    return 0;
}

static int ledger89_priv_check_room(off_t committed, off_t len)
{
    unsigned long long room;

    room = ledger89_u64_off_max() - (unsigned long long)len;
    if (committed > (off_t)room)
    {
        ledger89_priv_error(EOVERFLOW);
        return -1;
    }
    return 0;
}

int ledger89_append(ledger89 *l, const void *data, size_t size,
                    ledger89_offset *offset)
{
    off_t index_size;
    off_t committed;
    off_t len;
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
    rc = ledger89_u64_to_off((unsigned long long)size, &len);
    if (rc != 0)
    {
        ledger89_priv_error(EOVERFLOW);
        return -1;
    }
    rc = append89_begin(l->index_w, &index_size);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_priv_frontier(l, index_size, &committed);
    if (rc == 0)
    {
        rc = ledger89_priv_align_data(l, index_size, &committed);
    }
    if (rc == 0)
    {
        rc = ledger89_priv_check_room(committed, len);
    }
    if (rc == 0)
    {
        rc = ledger89_priv_append_payload(l, data, size, committed);
    }
    rc = ledger89_priv_finish(l, rc);
    if (rc == 0)
    {
        if (offset != NULL)
        {
            *offset = committed;
        }
    }
    return rc;
}

/* ---- reader: count / length / offset / read ---- */

int ledger89_count(ledger89 *l, unsigned long long *count)
{
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    off_t pos;
    int complete;
    int rc;

    rc = ledger89_priv_usable(l, 0);
    if (rc != 0)
    {
        return -1;
    }
    if (count == NULL)
    {
        ledger89_priv_error(EINVAL);
        return -1;
    }
    *count = 0ULL;
    pos = 0;
    for (;;)
    {
        rc = ledger89_priv_read_entry(l->index_r, &pos, raw, &complete);
        if (rc != 0)
        {
            return -1;
        }
        if (!complete)
        {
            break;
        }
        ++*count;
    }
    return 0;
}

/* Read and header-validate entry n. Returns 1 with e filled when present and
 * header-valid, 0 when out of range or header-invalid, -1 on I/O error. */
static int ledger89_priv_entry_at(ledger89 *l, unsigned long long n,
                                  struct ledger89_priv_entry *e)
{
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    off_t pos;
    int header_ok;
    int complete;
    int rc;

    if (n > ledger89_u64_off_max() / LEDGER89_PRIV_ENTRY_SIZE)
    {
        return 0;
    }
    pos = (off_t)n * (off_t)LEDGER89_PRIV_ENTRY_SIZE;
    rc = ledger89_priv_read_entry(l->index_r, &pos, raw, &complete);
    if (rc != 0)
    {
        return -1;
    }
    if (!complete)
    {
        return 0;
    }
    ledger89_priv_entry_decode(raw, e, &header_ok);
    if (header_ok)
    {
        return 1;
    }
    return 0;
}

unsigned long long ledger89_length(ledger89 *l, unsigned long long n)
{
    struct ledger89_priv_entry e;
    int rc;

    if (l == NULL)
    {
        return 0ULL;
    }
    rc = ledger89_priv_entry_at(l, n, &e);
    if (rc <= 0)
    {
        return 0ULL;
    }
    return e.length;
}

ledger89_offset ledger89_offset_of(ledger89 *l, unsigned long long n)
{
    struct ledger89_priv_entry e;
    off_t off;
    int rc;

    if (l == NULL)
    {
        return (ledger89_offset)-1;
    }
    rc = ledger89_priv_entry_at(l, n, &e);
    if (rc <= 0)
    {
        return (ledger89_offset)-1;
    }
    rc = ledger89_u64_to_off(e.offset, &off);
    if (rc != 0)
    {
        return (ledger89_offset)-1;
    }
    return off;
}

static void ledger89_priv_copy_out(unsigned char *data, size_t copied,
                                   size_t size, const unsigned char *buf,
                                   size_t got, size_t *copied_out)
{
    size_t room;
    size_t take;

    room = size - copied;
    take = ledger89_priv_take_size(got, room);
    memcpy(data + copied, buf, take);
    *copied_out = copied + take;
}

static int ledger89_priv_read_step(ledger89 *l, off_t *pos, off_t *left,
                                   unsigned char *data, size_t size,
                                   size_t *copied,
                                   checksum89_crc64_nvme_ctx *ctx)
{
    unsigned char buf[LEDGER89_PRIV_CHUNK];
    size_t want;
    ssize_t got;

    want = ledger89_priv_want_size(*left, sizeof(buf));
    got = append89_read(l->data_r, pos, buf, want);
    if (got < 0)
    {
        return -1;
    }
    if (got == 0)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    checksum89_crc64_nvme_update(ctx, buf, (size_t)got);
    if (*copied < size)
    {
        ledger89_priv_copy_out(data, *copied, size, buf, (size_t)got, copied);
    }
    *left = *left - got;
    return 0;
}

ssize_t ledger89_read(ledger89 *l, unsigned long long n, void *data,
                      size_t size)
{
    struct ledger89_priv_entry e;
    off_t pos;
    off_t left;
    off_t off;
    off_t len;
    size_t copied;
    checksum89_crc64_nvme_ctx ctx;
    unsigned long long crc;
    int rc;

    rc = ledger89_priv_usable(l, 0);
    if (rc != 0)
    {
        return -1;
    }
    if (size > 0U)
    {
        if (data == NULL)
        {
            ledger89_priv_error(EINVAL);
            return (ssize_t)-1;
        }
    }
    rc = ledger89_priv_entry_at(l, n, &e);
    if (rc <= 0)
    {
        return 0;
    }
    rc = ledger89_u64_to_off(e.offset, &off);
    if (rc != 0)
    {
        ledger89_priv_error(EILSEQ);
        return (ssize_t)-1;
    }
    rc = ledger89_u64_to_off(e.length, &len);
    if (rc != 0)
    {
        ledger89_priv_error(EILSEQ);
        return (ssize_t)-1;
    }
    if (len == 0)
    {
        return 0;
    }
    if (off > (off_t)(ledger89_u64_off_max() - (unsigned long long)len))
    {
        ledger89_priv_error(EILSEQ);
        return (ssize_t)-1;
    }
    checksum89_crc64_nvme_init(&ctx);
    pos = off;
    left = len;
    copied = 0U;
    while (left > 0)
    {
        rc = ledger89_priv_read_step(l, &pos, &left, (unsigned char *)data,
                                     size, &copied, &ctx);
        if (rc != 0)
        {
            return -1;
        }
    }
    crc = ledger89_priv_crc_final(&ctx);
    if (crc != e.checksum)
    {
        ledger89_priv_error(EILSEQ);
        return (ssize_t)-1;
    }
    return (ssize_t)copied;
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
    *out = it;
    return LEDGER89_OK;
}

int ledger89_iter_next(ledger89_iter *it)
{
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    struct ledger89_priv_entry e;
    off_t pos;
    off_t off;
    off_t len;
    int header_ok;
    int complete;
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
    if (it->next_index > ledger89_u64_off_max() / LEDGER89_PRIV_ENTRY_SIZE)
    {
        return LEDGER89_END;
    }
    pos = (off_t)it->next_index * (off_t)LEDGER89_PRIV_ENTRY_SIZE;
    rc = ledger89_priv_read_entry(it->owner->index_r, &pos, raw, &complete);
    if (rc != 0)
    {
        return -1;
    }
    if (!complete)
    {
        return LEDGER89_END;
    }
    ledger89_priv_entry_decode(raw, &e, &header_ok);
    if (!header_ok)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    rc = ledger89_u64_to_off(e.offset, &off);
    if (rc != 0)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    rc = ledger89_u64_to_off(e.length, &len);
    if (rc != 0)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    if (off != it->expected)
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    if (off > (off_t)(ledger89_u64_off_max() - (unsigned long long)len))
    {
        ledger89_priv_error(EILSEQ);
        return -1;
    }
    it->offset = off;
    it->length = len;
    it->read_pos = off;
    it->read_left = len;
    it->checksum = e.checksum;
    checksum89_crc64_nvme_init(&it->crc);
    it->expected = off + len;
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
    n = append89_read(it->owner->data_r, &pos, data, want);
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
    off_t index_size;
    off_t b;
    off_t a;
    int rc;

    rc = ledger89_priv_usable(l, 1);
    if (rc != 0)
    {
        return -1;
    }
    rc = append89_begin(l->index_w, &index_size);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_priv_recover_core(l, index_size, &b, &a);
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
