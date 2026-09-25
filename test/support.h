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
#include "fault.h"

/* Native unsigned long long test helpers (libll89 is gone). */
static void u64_store_be(unsigned char out[8], unsigned long long value)
{
    int i;
    for (i = 7; i >= 0; --i)
    {
        out[i] = (unsigned char)(value & 0xffULL);
        value >>= 8;
    }
}

static int u64_to_size(unsigned long long value, size_t *out)
{
    if (value > (unsigned long long)(size_t)-1)
    {
        return -1;
    }
    *out = (size_t)value;
    return 0;
}

static void test_path(char *path, size_t size, const char *tag, int number)
{
    (void)snprintf(path, size, "/tmp/ledger89-%s-%ld-%d", tag, (long)getpid(), number);
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

static void test_expect(ledger89 *a, ledger89_offset *cursor,
                        const void *data, size_t size)
{
    ledger89_message *m;
    unsigned char buf[4096];
    size_t done;
    size_t length;
    ssize_t n;
    assert(ledger89_next(a, cursor, &m) == LEDGER89_OK);
    assert(u64_to_size(ledger89_message_length(m), &length) == 0);
    assert(length == size);
    done = 0U;
    while ((n = ledger89_message_read(m, buf, sizeof(buf))) > 0)
    {
        assert((size_t)n <= size - done);
        assert(memcmp(buf, (const unsigned char *)data + done, (size_t)n) == 0);
        done += (size_t)n;
    }
    assert(n == 0 && done == size);
    ledger89_message_close(m);
}

static off_t test_size(const char *path)
{
    struct stat st;
    assert(stat(path, &st) == 0);
    return st.st_size;
}

static void test_fragment(append89 *w, off_t start, unsigned long length,
                          const char *data, size_t size)
{
    unsigned char header[24];
    struct iovec iov[2];
    unsigned long long v;
    /* Test fixtures use small offsets. Production uses checked codecs. */
    v = (unsigned long long)start;
    u64_store_be(header, v);
    v = (unsigned long long)length;
    u64_store_be(header + 8, v);
    v = (unsigned long long)size;
    u64_store_be(header + 16, v);
    iov[0].iov_base = header;
    iov[0].iov_len = sizeof(header);
    iov[1].iov_base = (void *)data;
    iov[1].iov_len = size;
    assert(append89_appendv(w, iov, 2, NULL) == 0);
}
#endif
