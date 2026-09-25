#include "support.h"

/* Framing boundaries: empty, sub-fragment, exact fragment, and the first
 * multi-fragment sizes around a small reserve (capacity 8). */
static void roundtrip_size(size_t size, int number)
{
    char path[160];
    unsigned char *payload;
    size_t i;
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    test_path(path, sizeof(path), "fmt", number);
    payload = (unsigned char *)malloc(size == 0U ? 1U : size);
    assert(payload != NULL);
    for (i = 0U; i < size; ++i)
    {
        payload[i] = (unsigned char)(i * 7U + 1U);
    }
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, size == 0U ? NULL : payload, size, NULL) == 0);
    assert(ledger89_sync(a) == 0);
    cursor = LEDGER89_BEGIN;
    test_expect(a, &cursor, payload, size);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);
    free(payload);
    assert(unlink(path) == 0);
}

int main(void)
{
    static const size_t sizes[] = {0U,  1U,  7U,  8U,  9U,  15U, 16U,
                                   17U, 24U, 25U, 31U, 32U, 33U, 64U};
    size_t i;
    for (i = 0U; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        roundtrip_size(sizes[i], (int)i);
    }
    return 0;
}
