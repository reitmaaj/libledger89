#include "support.h"

/* Minimal end-to-end real-I/O path: create, append, commit, reopen as reader,
 * iterate, then reopen as writer and append once more. */
int main(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset off;
    static const char *const items[] = {"A", "B"};
    static const size_t sizes[] = {1U, 1U};
    test_path(path, sizeof(path), "smoke", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, &off) == 0);
    assert(off == (ledger89_offset)32);
    assert(ledger89_append(l, "B", 1U, NULL) == 0);
    ledger89_close(l);

    assert(ledger89_open_reader(&l, path) == 0);
    test_expect(l, 0ULL, "A", 1U);
    test_expect(l, 1ULL, "B", 1U);
    test_expect_all(l, items, sizes, 2U);
    ledger89_close(l);

    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "C", 1U, NULL) == 0);
    ledger89_close(l);
    test_unlink(path);
    return 0;
}
