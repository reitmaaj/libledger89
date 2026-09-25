#include "support.h"

/* Minimal end-to-end real-I/O path: create, append, sync, reopen as reader,
 * scan, then reopen as writer and append once more. No mocks. */
int main(void)
{
    char path[160];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    ledger89_offset start;
    test_path(path, sizeof(path), "smoke", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, "A", 1U, &start) == 0);
    assert(start == LEDGER89_BEGIN);
    assert(ledger89_append(a, "B", 1U, NULL) == 0);
    assert(ledger89_sync(a) == 0);
    ledger89_close(a);

    assert(ledger89_open_reader(&a, path, 32U) == 0);
    cursor = LEDGER89_BEGIN;
    test_expect(a, &cursor, "A", 1U);
    test_expect(a, &cursor, "B", 1U);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);

    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, "C", 1U, NULL) == 0);
    ledger89_close(a);
    assert(unlink(path) == 0);
    return 0;
}
