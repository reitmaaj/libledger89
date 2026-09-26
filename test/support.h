#ifndef LEDGER89_TEST_SUPPORT_H
#define LEDGER89_TEST_SUPPORT_H
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ledger89.h"
#include "append89.h"
#include "cksum89.h"
#include "fault.h"

#define LEDGER89_TEST_ENTRY_SIZE 32U
#define LEDGER89_TEST_MAGIC 0x4c443839ULL
#define LEDGER89_TEST_INDEX_RESERVE 4096U

static void test_u64_store_be(unsigned char out[8], unsigned long long value)
{
    int i;
    for (i = 7; i >= 0; --i)
    {
        out[i] = (unsigned char)(value & 0xffULL);
        value >>= 8;
    }
}

static unsigned long long test_u64_load_be(const unsigned char in[8])
{
    unsigned long long value;
    int i;
    value = 0ULL;
    for (i = 0; i < 8; ++i)
    {
        value = (value << 8) | (unsigned long long)in[i];
    }
    return value;
}

static unsigned long long test_crc(const void *data, size_t size)
{
    cksum89_u64 value;
    value = cksum89_crc64_nvme(data, size);
    return ((unsigned long long)value.hi << 32) | (unsigned long long)value.lo;
}

/* Encode one 32-byte INDEX entry exactly as the library does. */
static void test_entry(unsigned char out[LEDGER89_TEST_ENTRY_SIZE],
                       unsigned long long offset, unsigned long long length,
                       unsigned long long checksum)
{
    test_u64_store_be(out, offset);
    test_u64_store_be(out + 8, length);
    test_u64_store_be(out + 16, checksum);
    out[24] = (unsigned char)((LEDGER89_TEST_MAGIC >> 24) & 0xffULL);
    out[25] = (unsigned char)((LEDGER89_TEST_MAGIC >> 16) & 0xffULL);
    out[26] = (unsigned char)((LEDGER89_TEST_MAGIC >> 8) & 0xffULL);
    out[27] = (unsigned char)(LEDGER89_TEST_MAGIC & 0xffULL);
    out[28] = 0U;
    out[29] = 1U;
    out[30] = 0U;
    out[31] = 0U;
}

static void test_path(char *path, size_t size, const char *tag, int number)
{
    (void)snprintf(path, size, "/tmp/ledger89-%s-%ld-%d", tag, (long)getpid(),
                   number);
}

static void test_wait(pid_t child, int killed)
{
    int status;
    assert(waitpid(child, &status, 0) == child);
    if (killed)
    {
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    }
    else
    {
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

static off_t test_size(const char *path)
{
    struct stat st;
    assert(stat(path, &st) == 0);
    return st.st_size;
}

/* Remove both files of a ledger created at path. */
static void test_unlink(const char *path)
{
    char buf[160];
    (void)snprintf(buf, sizeof(buf), "%s.data", path);
    assert(unlink(buf) == 0);
    (void)snprintf(buf, sizeof(buf), "%s.index", path);
    assert(unlink(buf) == 0);
}

/* Size of the ledger's DATA file. */
static off_t test_data_size(const char *path)
{
    char buf[160];
    (void)snprintf(buf, sizeof(buf), "%s.data", path);
    return test_size(buf);
}

/* Size of the ledger's INDEX file. */
static off_t test_index_size(const char *path)
{
    char buf[160];
    (void)snprintf(buf, sizeof(buf), "%s.index", path);
    return test_size(buf);
}

/* Logical (payload) size of the DATA stream, excluding its append reserve. */
static off_t test_data_logical_size(const char *path)
{
    char buf[160];
    (void)snprintf(buf, sizeof(buf), "%s.data", path);
    return test_size(buf) - (off_t)APPEND89_RESERVE;
}

/* Logical (entry) size of the INDEX stream, excluding its append reserve. */
static off_t test_index_logical_size(const char *path)
{
    char buf[160];
    (void)snprintf(buf, sizeof(buf), "%s.index", path);
    return test_size(buf) - (off_t)LEDGER89_TEST_INDEX_RESERVE;
}

/* Read the DATA stream's logical bytes directly through libappend89. */
static size_t test_read_data(const char *path, unsigned char *buf, size_t cap)
{
    append89 *a;
    append89_offset pos;
    char p[160];
    size_t got;
    ssize_t n;
    (void)snprintf(p, sizeof(p), "%s.data", path);
    assert(append89_open_reader(&a, p) == 0);
    pos = 0;
    got = 0U;
    while ((n = append89_read(a, &pos, buf + got, cap - got)) > 0)
    {
        got += (size_t)n;
    }
    append89_close(a);
    return got;
}

/* Read the INDEX stream's logical bytes directly through libappend89. */
static size_t test_read_index(const char *path, unsigned char *buf, size_t cap)
{
    append89 *a;
    append89_offset pos;
    char p[160];
    size_t got;
    ssize_t n;
    (void)snprintf(p, sizeof(p), "%s.index", path);
    assert(append89_open_reader_reserve(&a, p, LEDGER89_TEST_INDEX_RESERVE) == 0);
    pos = 0;
    got = 0U;
    while ((n = append89_read(a, &pos, buf + got, cap - got)) > 0)
    {
        got += (size_t)n;
    }
    append89_close(a);
    return got;
}

/* Read record n through the random-access API and compare it byte-for-byte.
 * Records larger than the internal buffer are exercised through iteration. */
static void test_expect(ledger89 *l, unsigned long long n, const void *data,
                        size_t size)
{
    unsigned char buf[70000];
    ssize_t got;
    assert(size <= sizeof(buf));
    got = ledger89_read(l, n, buf, sizeof(buf));
    assert(got == (ssize_t)size);
    assert(memcmp(buf, data, size) == 0);
    assert(ledger89_length(l, n) == (unsigned long long)size);
    assert(ledger89_offset_of(l, n) >= 0);
}

/* Iterate and compare every record to the expected array of payloads. */
static void test_expect_all(ledger89 *l, const char *const *items,
                            const size_t *sizes, size_t count)
{
    ledger89_iter *it;
    unsigned char buf[4096];
    size_t i;
    size_t done;
    ssize_t got;
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    for (i = 0U; i < count; ++i)
    {
        assert(ledger89_iter_next(it) == LEDGER89_OK);
        assert(ledger89_iter_index(it) == (unsigned long long)i);
        assert(ledger89_iter_length(it) == (unsigned long long)sizes[i]);
        done = 0U;
        while ((got = ledger89_iter_read(it, buf, sizeof(buf))) > 0)
        {
            assert((size_t)got <= sizes[i] - done);
            assert(memcmp(buf, (const unsigned char *)items[i] + done,
                          (size_t)got) == 0);
            done += (size_t)got;
        }
        assert(got == 0 && done == sizes[i]);
    }
    assert(ledger89_iter_next(it) == LEDGER89_END);
    ledger89_iter_close(it);
}

/* Raw helpers: append DATA bytes and INDEX entries via append89 directly, for
 * crafting on-disk states outside the public append path. */
static void test_append_data(append89 *w, const void *data, size_t size)
{
    assert(append89_append(w, data, size, NULL) == 0);
}

static void test_append_index(append89 *w, unsigned long long offset,
                              unsigned long long length,
                              unsigned long long checksum)
{
    unsigned char entry[LEDGER89_TEST_ENTRY_SIZE];
    test_entry(entry, offset, length, checksum);
    assert(append89_append(w, entry, LEDGER89_TEST_ENTRY_SIZE, NULL) == 0);
}
#endif
