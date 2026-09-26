/* Layer 12: stress tests - high record count and large DATA. */
#include "support.h"

#define STRESS_RECORDS 20000
#define STRESS_LARGE (2U * 1024U * 1024U + 512U * 1024U)

static void many_small_records(void)
{
    char path[160];
    ledger89 *l;
    ledger89_iter *it;
    unsigned long long count;
    unsigned char payload[8];
    int i;
    test_path(path, sizeof(path), "stress-many", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    for (i = 0; i < STRESS_RECORDS; ++i)
    {
        int j;
        for (j = 0; j < 8; ++j)
        {
            payload[j] = (unsigned char)(i + j);
        }
        assert(ledger89_append(l, payload, sizeof(payload), NULL) == 0);
    }
    assert(ledger89_count(l, &count) == 0);
    assert(count == STRESS_RECORDS);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    count = 0ULL;
    while (ledger89_iter_next(it) == LEDGER89_OK)
    {
        assert(ledger89_iter_offset(it) == (ledger89_offset)(count * 8U));
        assert(ledger89_iter_length(it) == 8ULL);
        ++count;
    }
    assert(count == STRESS_RECORDS);
    ledger89_iter_close(it);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_count(l, &count) == 0 && count == STRESS_RECORDS);
    ledger89_close(l);
    test_unlink(path);
}

static void large_record(void)
{
    char path[160];
    ledger89 *l;
    ledger89_iter *it;
    unsigned long long count;
    unsigned char *payload;
    unsigned char *actual;
    size_t i;
    size_t done;
    ssize_t n;
    payload = (unsigned char *)malloc(STRESS_LARGE);
    actual = (unsigned char *)malloc(STRESS_LARGE);
    assert(payload != NULL && actual != NULL);
    for (i = 0U; i < STRESS_LARGE; ++i)
    {
        payload[i] = (unsigned char)(i * 13U);
    }
    test_path(path, sizeof(path), "stress-large", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, payload, STRESS_LARGE, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    assert(ledger89_length(l, 0ULL) == STRESS_LARGE);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    assert(ledger89_iter_next(it) == LEDGER89_OK);
    done = 0U;
    while ((n = ledger89_iter_read(it, actual + done, STRESS_LARGE - done)) > 0)
    {
        done += (size_t)n;
    }
    assert(n == 0 && done == STRESS_LARGE);
    assert(memcmp(payload, actual, STRESS_LARGE) == 0);
    ledger89_iter_close(it);
    ledger89_close(l);
    free(payload);
    free(actual);
    test_unlink(path);
}

static void many_empty_records(void)
{
    char path[160];
    ledger89 *l;
    unsigned long long count;
    int i;
    test_path(path, sizeof(path), "stress-empty", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    for (i = 0; i < 5000; ++i)
    {
        assert(ledger89_append(l, NULL, 0U, NULL) == 0);
    }
    assert(ledger89_count(l, &count) == 0 && count == 5000ULL);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 5000ULL);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 0);
    test_unlink(path);
}

int main(void)
{
    many_small_records();
    large_record();
    many_empty_records();
    return 0;
}
