#include "support.h"

int main(void)
{
    char path[160];
    unsigned char *payload;
    size_t size;
    size_t i;
    ledger89 *a;
    ledger89_offset cursor;
    int option;
    size_t reserve;
    size = (size_t)APPEND89_RESERVE + 37U;
    payload = (unsigned char *)malloc(size);
    assert(payload != NULL);
    for (i = 0U; i < size; ++i)
    {
        payload[i] = (unsigned char)(i % 251U);
    }
    for (option = 0; option < 2; ++option)
    {
        reserve = option == 0 ? 0U : (size_t)APPEND89_RESERVE * 2U;
        test_path(path, sizeof(path), "large", option);
        assert(ledger89_create(path, (mode_t)0600, reserve) == 0);
        assert(ledger89_open_writer(&a, path, reserve) == 0);
        assert(ledger89_append(a, payload, size, NULL) == 0);
        assert(ledger89_sync(a) == 0);
        cursor = LEDGER89_BEGIN;
        test_expect(a, &cursor, payload, size);
        ledger89_close(a);
        assert(unlink(path) == 0);
    }
    free(payload);
    return 0;
}
