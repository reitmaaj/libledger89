/* Layer 12: stress tests - high record count and maximum-size records. */
#include "support.h"

#define STRESS_RECORDS 20000
#define STRESS_MAX ((size_t)APPEND89_RESERVE - LEDGER89_TEST_HEADER_SIZE)

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
    assert(test_count(l) == STRESS_RECORDS);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    count = 0ULL;
    while (ledger89_iter_next(it) == LEDGER89_OK)
    {
        assert(ledger89_iter_offset(it) ==
               (ledger89_offset)(32 + count * 24U));
        assert(ledger89_iter_length(it) == 8ULL);
        ++count;
    }
    assert(count == STRESS_RECORDS);
    ledger89_iter_close(it);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(test_count(l) == STRESS_RECORDS);
    ledger89_close(l);
    test_unlink(path);
}

static void max_record(void)
{
    char path[160];
    ledger89 *l;
    ledger89_iter *it;
    unsigned char *payload;
    unsigned char *actual;
    size_t i;
    size_t done;
    ssize_t n;
    payload = (unsigned char *)malloc(STRESS_MAX);
    actual = (unsigned char *)malloc(STRESS_MAX);
    assert(payload != NULL && actual != NULL);
    for (i = 0U; i < STRESS_MAX; ++i)
    {
        payload[i] = (unsigned char)(i * 13U);
    }
    test_path(path, sizeof(path), "stress-max", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, payload, STRESS_MAX, NULL) == 0);
    assert(test_count(l) == 1ULL);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    assert(ledger89_iter_next(it) == LEDGER89_OK);
    assert(ledger89_iter_length(it) == (unsigned long long)STRESS_MAX);
    done = 0U;
    while ((n = ledger89_iter_read(it, actual + done, STRESS_MAX - done)) > 0)
    {
        done += (size_t)n;
    }
    assert(n == 0 && done == STRESS_MAX);
    assert(memcmp(payload, actual, STRESS_MAX) == 0);
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
    int i;
    test_path(path, sizeof(path), "stress-empty", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    for (i = 0; i < 5000; ++i)
    {
        assert(ledger89_append(l, NULL, 0U, NULL) == 0);
    }
    assert(test_count(l) == 5000ULL);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(test_count(l) == 5000ULL);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)(16 + 16 * 5000));
    test_unlink(path);
}

int main(void)
{
    many_small_records();
    max_record();
    many_empty_records();
    return 0;
}
