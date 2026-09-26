#include "support.h"

static void open_errors(void)
{
    ledger89 *l;
    l = (ledger89 *)1;
    assert(ledger89_open_reader(NULL, "x") == -1 && errno == EINVAL);
    assert(ledger89_open_writer(NULL, "x") == -1 && errno == EINVAL);
    assert(ledger89_open_reader(&l, NULL) == -1 && errno == EINVAL);
    assert(l == NULL);
    assert(ledger89_open_writer(&l, NULL) == -1 && errno == EINVAL);
    assert(l == NULL);
    assert(ledger89_open_reader(&l, "/nonexistent-ledger89-api") == -1);
    assert(errno == ENOENT && l == NULL);
}

static void create_errors(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "api-create", 0);
    assert(ledger89_create(NULL, (mode_t)0600) == -1 && errno == EINVAL);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_create(path, (mode_t)0600) == -1 && errno == EEXIST);
    assert(ledger89_open_reader(&l, path) == 0);
    ledger89_close(l);
    test_unlink(path);
}

static void writer_only_errors(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "api-ro", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_append(l, "x", 1U, NULL) == -1 && errno == EBADF);
    assert(ledger89_recover(l, NULL, NULL) == -1 && errno == EBADF);
    ledger89_close(l);
    test_unlink(path);
}

static void append_arguments(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "api-args", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, NULL, 1U, NULL) == -1 && errno == EINVAL);
    assert(ledger89_append(l, NULL, 0U, NULL) == 0); /* empty record is legal */
    ledger89_close(l);
    test_unlink(path);
}

static void read_arguments(void)
{
    char path[160];
    ledger89 *l;
    unsigned long long count;
    char buf[4];
    test_path(path, sizeof(path), "api-read", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "data", 4U, NULL) == 0);
    assert(ledger89_count(l, NULL) == -1 && errno == EINVAL);
    assert(ledger89_read(l, 0ULL, NULL, 4U) == -1 && errno == EINVAL);
    assert(ledger89_read(l, 0ULL, buf, 0U) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    /* Out-of-range record. */
    assert(ledger89_read(l, 1ULL, buf, sizeof(buf)) == 0);
    assert(ledger89_length(l, 1ULL) == 0ULL);
    assert(ledger89_offset_of(l, 1ULL) == (ledger89_offset)-1);
    ledger89_close(l);
    test_unlink(path);
}

static void iter_errors(void)
{
    char path[160];
    ledger89 *l;
    ledger89_iter *it;
    test_path(path, sizeof(path), "api-iter", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_iter_begin(l, NULL) == -1 && errno == EINVAL);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    assert(ledger89_iter_next(NULL) == -1 && errno == EINVAL);
    ledger89_iter_close(it);
    ledger89_close(l);
    test_unlink(path);
}

static void stale_errno(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset off;
    unsigned long long count;
    test_path(path, sizeof(path), "api-errno", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    errno = EINVAL;
    assert(ledger89_append(l, "ok", 2U, &off) == 0);
    assert(off == 0);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    errno = ENOSPC;
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    test_expect(l, 0ULL, "ok", 2U);
    ledger89_close(l);
    test_unlink(path);
}

static void empty_ledger(void)
{
    char path[160];
    ledger89 *l;
    ledger89_iter *it;
    unsigned long long count;
    test_path(path, sizeof(path), "api-empty", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 0ULL);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    assert(ledger89_iter_next(it) == LEDGER89_END);
    ledger89_iter_close(it);
    ledger89_close(l);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 0ULL);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 0);
    assert(test_index_logical_size(path) == 0);
    test_unlink(path);
}

static void single_append_sizes(void)
{
    static const size_t sizes[] = {0U, 1U, 2U, 3U, 7U, 8U, 15U, 16U,
                                   31U, 32U, 63U, 64U, 255U, 256U, 4095U,
                                   4096U, 4097U, 65537U};
    size_t i;
    unsigned char payload[70000];
    char path[160];
    ledger89 *l;
    size_t j;
    for (i = 0U; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        test_path(path, sizeof(path), "api-size", (int)i);
        assert(sizes[i] <= sizeof(payload));
        for (j = 0U; j < sizes[i]; ++j)
        {
            payload[j] = (unsigned char)(j * 7U + 1U);
        }
        assert(ledger89_create(path, (mode_t)0600) == 0);
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_append(l, sizes[i] == 0U ? NULL : payload, sizes[i],
                               NULL) == 0);
        test_expect(l, 0ULL, payload, sizes[i]);
        ledger89_close(l);
        assert(ledger89_open_reader(&l, path) == 0);
        test_expect(l, 0ULL, payload, sizes[i]);
        ledger89_close(l);
        test_unlink(path);
    }
}

static void multiple_appends(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset off;
    static const char *const items[] = {"", "a", "bc", "", "defgh", "i", ""};
    static const size_t sizes[] = {0U, 1U, 2U, 0U, 5U, 1U, 0U};
    size_t i;
    test_path(path, sizeof(path), "api-multi", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    for (i = 0U; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        assert(ledger89_append(l, items[i], sizes[i], &off) == 0);
        (void)off;
    }
    test_expect_all(l, items, sizes, sizeof(sizes) / sizeof(sizes[0]));
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    test_expect_all(l, items, sizes, sizeof(sizes) / sizeof(sizes[0]));
    ledger89_close(l);
    test_unlink(path);
}

static void binary_payloads(void)
{
    char path[160];
    unsigned char payload[256];
    ledger89 *l;
    int i;
    test_path(path, sizeof(path), "api-binary", 0);
    for (i = 0; i < 256; ++i)
    {
        payload[i] = (unsigned char)i;
    }
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, payload, sizeof(payload), NULL) == 0);
    test_expect(l, 0ULL, payload, sizeof(payload));
    ledger89_close(l);
    test_unlink(path);
}

static void reopen_cycles(void)
{
    char path[160];
    ledger89 *l;
    char payload[64];
    int i;
    test_path(path, sizeof(path), "api-reopen", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    for (i = 0; i < 20; ++i)
    {
        memset(payload, (int)('a' + i), sizeof(payload));
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_append(l, payload, sizeof(payload), NULL) == 0);
        ledger89_close(l);
        assert(ledger89_open_reader(&l, path) == 0);
        test_expect(l, (unsigned long long)i, payload, sizeof(payload));
        ledger89_close(l);
    }
    test_unlink(path);
}

int main(void)
{
    open_errors();
    create_errors();
    writer_only_errors();
    append_arguments();
    read_arguments();
    iter_errors();
    stale_errno();
    empty_ledger();
    single_append_sizes();
    multiple_appends();
    binary_payloads();
    reopen_cycles();
    return 0;
}
