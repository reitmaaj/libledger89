#include "support.h"

static void malformed(int which)
{
    char path[160];
    unsigned char header[24];
    unsigned long long value;
    append89 *w;
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    off_t size;
    test_path(path, sizeof(path), "malformed", which);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    memset(header, 0, sizeof(header));
    value = 16ULL;
    u64_store_be(header, value);
    if (which == 0)
    {
        header[0] = 255U; /* Impossible offset. */
    }
    else if (which == 1)
    {
        header[23] = 1U; /* Payload larger than zero total. */
    }
    else if (which == 2)
    {
        header[15] = 9U;
        header[23] = 9U; /* Fragment exceeds capacity (8). */
    }
    else if (which == 3)
    {
        header[15] = 1U; /* Nonempty message, empty fragment. */
    }
    else
    {
        header[7] = 15U; /* Offset before first possible fragment. */
    }
    assert(append89_open_writer_reserve(&w, path, (mode_t)0, 32U) == 0);
    assert(append89_append(w, header, sizeof(header), NULL) == 0);
    append89_close(w);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    size = test_size(path);
    cursor = LEDGER89_BEGIN;
    assert(ledger89_next(a, &cursor, &m) == -1);
    assert(errno == EILSEQ && cursor == LEDGER89_BEGIN && m == NULL);
    assert(ledger89_recover(a, NULL, NULL) == -1);
    assert(errno == EILSEQ && test_size(path) == size);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void polling(void)
{
    char path[160];
    append89 *w;
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset start;
    ledger89_offset cursor;
    test_path(path, sizeof(path), "poll", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_reader(&a, path, 32U) == 0);
    assert(append89_open_writer_reserve(&w, path, (mode_t)0, 32U) == 0);
    cursor = LEDGER89_BEGIN;
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    assert(append89_begin(w, &start) == 0);
    test_fragment(w, start, 12UL, "abcdefgh", 8U);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_PARTIAL);
    assert(cursor == start);
    test_fragment(w, start, 12UL, "ijkl", 4U);
    test_expect(a, &cursor, "abcdefghijkl", 12U);
    assert(append89_end(w) == 0);
    append89_close(w);
    assert(ledger89_append(a, "x", 1U, NULL) == -1 && errno == EBADF);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void arguments(void)
{
    char path[160];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    struct iovec iov[2];
    off_t before;
    test_path(path, sizeof(path), "args", 0);
    assert(ledger89_create(path, (mode_t)0600, 24U) == -1 && errno == EINVAL);
    assert(access(path, F_OK) == -1);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    before = test_size(path);
    assert(ledger89_open_writer(&a, path, 31U) == -1 && errno == EINVAL);
    assert(test_size(path) == before);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, NULL, 1U, NULL) == -1 && errno == EINVAL);
    assert(ledger89_appendv(a, NULL, 1, NULL) == -1 && errno == EINVAL);
    assert(ledger89_appendv(a, NULL, -1, NULL) == -1 && errno == EINVAL);
    iov[0].iov_base = (void *)"x";
    iov[0].iov_len = (size_t)-1;
    iov[1].iov_base = (void *)"x";
    iov[1].iov_len = 1U;
    assert(ledger89_appendv(a, iov, 2, NULL) == -1 && errno == EOVERFLOW);
    assert(test_size(path) == before);
    cursor = 0;
    m = (ledger89_message *)1;
    assert(ledger89_next(a, &cursor, &m) == -1 && errno == EINVAL && m == NULL);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

int main(void)
{
    int i;
    for (i = 0; i < 5; ++i)
    {
        malformed(i);
    }
    polling();
    arguments();
    return 0;
}
