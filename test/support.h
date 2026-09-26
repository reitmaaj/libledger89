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
#include "checksum89.h"
#include "fault.h"

#define LEDGER89_TEST_HEADER_SIZE 16U
#define LEDGER89_TEST_PREAMBLE_SIZE 16U
#define LEDGER89_TEST_MAGIC 0x4c45444738395331ULL

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
    checksum89_u64 value;
    value = checksum89_crc64_nvme(data, size);
    return ((unsigned long long)value.hi << 32) | (unsigned long long)value.lo;
}

/* Encode the 16-byte preamble exactly as the library does. */
static void test_preamble(unsigned char out[LEDGER89_TEST_PREAMBLE_SIZE])
{
    test_u64_store_be(out, LEDGER89_TEST_MAGIC);
    test_u64_store_be(out + 8, (unsigned long long)APPEND89_RESERVE);
}

/* Encode one record frame (header || payload) exactly as the library does.
 * Returns the frame length. */
static size_t test_frame(unsigned char *out, size_t cap, const void *data,
                         size_t size)
{
    assert(cap >= LEDGER89_TEST_HEADER_SIZE + size);
    test_u64_store_be(out, (unsigned long long)size);
    test_u64_store_be(out + 8, test_crc(data, size));
    if (size > 0U)
    {
        memcpy(out + LEDGER89_TEST_HEADER_SIZE, data, size);
    }
    return LEDGER89_TEST_HEADER_SIZE + size;
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

/* Remove the ledger file at path. */
static void test_unlink(const char *path)
{
    assert(unlink(path) == 0);
}

/* Logical size of the ledger stream, excluding its append reserve. */
static off_t test_logical_size(const char *path)
{
    return test_size(path) - (off_t)APPEND89_RESERVE;
}

/* Read the ledger's logical bytes directly through libappend89. */
static size_t test_read_file(const char *path, unsigned char *buf, size_t cap)
{
    append89 *a;
    append89_offset pos;
    size_t got;
    ssize_t n;
    assert(append89_open_reader(&a, path) == 0);
    pos = 0;
    got = 0U;
    while ((n = append89_read(a, &pos, buf + got, cap - got)) > 0)
    {
        got += (size_t)n;
    }
    append89_close(a);
    return got;
}

/* Verify record n by iterating from the first record. */
static void test_expect(ledger89 *l, unsigned long long n, const void *data,
                        size_t size)
{
    ledger89_iter *it;
    unsigned long long i;
    unsigned char buf[4096];
    size_t done;
    ssize_t got;
    int rc;
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    for (i = 0U; i <= n; ++i)
    {
        rc = ledger89_iter_next(it);
        assert(rc == LEDGER89_OK);
    }
    assert(ledger89_iter_index(it) == n);
    assert(ledger89_iter_length(it) == (unsigned long long)size);
    done = 0U;
    while ((got = ledger89_iter_read(it, buf, sizeof(buf))) > 0)
    {
        assert((size_t)got <= size - done);
        assert(memcmp(buf, (const unsigned char *)data + done,
                      (size_t)got) == 0);
        done += (size_t)got;
    }
    assert(got == 0 && done == size);
    ledger89_iter_close(it);
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

/* Count committed records by iteration. */
static unsigned long long test_count(ledger89 *l)
{
    ledger89_iter *it;
    unsigned long long n;
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    n = 0ULL;
    while (ledger89_iter_next(it) == LEDGER89_OK)
    {
        ++n;
    }
    ledger89_iter_close(it);
    return n;
}

/* Raw helpers: append bytes via append89 directly, for crafting on-disk states
 * outside the public append path. */
static void test_append_bytes(append89 *w, const void *data, size_t size)
{
    assert(append89_append(w, data, size, NULL) == 0);
}

static void test_append_preamble(append89 *w)
{
    unsigned char p[LEDGER89_TEST_PREAMBLE_SIZE];
    test_preamble(p);
    test_append_bytes(w, p, sizeof(p));
}

static void test_append_frame_bytes(append89 *w, const void *data, size_t size)
{
    unsigned char frame[LEDGER89_TEST_HEADER_SIZE + 70000U];
    size_t len;
    assert(size <= 70000U);
    len = test_frame(frame, sizeof(frame), data, size);
    test_append_bytes(w, frame, len);
}

/* Append just a 16-byte frame header, for crafting states whose payload does
 * not exist or does not match. */
static void test_append_header(append89 *w, unsigned long long size,
                               unsigned long long checksum)
{
    unsigned char raw[LEDGER89_TEST_HEADER_SIZE];
    test_u64_store_be(raw, size);
    test_u64_store_be(raw + 8, checksum);
    test_append_bytes(w, raw, sizeof(raw));
}
#endif
