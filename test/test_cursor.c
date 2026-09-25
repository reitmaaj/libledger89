#include "support.h"

/* END is an observation, not a permanent state: a separate process can append
 * and the same reader cursor then observes the new record. */
static void end_transient_across_process(void)
{
    char path[160];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    pid_t child;
    test_path(path, sizeof(path), "cursor-end", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_reader(&a, path, 32U) == 0);
    cursor = LEDGER89_BEGIN;
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);

    child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        ledger89 *w;
        assert(ledger89_open_writer(&w, path, 32U) == 0);
        assert(ledger89_append(w, "X", 1U, NULL) == 0);
        assert(ledger89_sync(w) == 0);
        ledger89_close(w);
        _exit(0);
    }
    test_wait(child, 0);

    test_expect(a, &cursor, "X", 1U);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

/* PARTIAL retains the incomplete start for retry, and the same cursor
 * advances once the message completes. */
static void partial_retry_then_complete(void)
{
    char path[160];
    append89 *w;
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset end;
    ledger89_offset cursor;
    test_path(path, sizeof(path), "cursor-partial", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_reader(&a, path, 32U) == 0);
    assert(append89_open_writer_reserve(&w, path, (mode_t)0, 32U) == 0);
    assert(append89_begin(w, &end) == 0);
    test_fragment(w, end, 6UL, "abc", 3U);

    cursor = LEDGER89_BEGIN;
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_PARTIAL);
    assert(cursor == end);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_PARTIAL);
    assert(cursor == end); /* cursor stays fixed while incomplete */

    test_fragment(w, end, 6UL, "def", 3U);
    assert(append89_end(w) == 0);
    append89_close(w);

    test_expect(a, &cursor, "abcdef", 6U);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

int main(void)
{
    end_transient_across_process();
    partial_retry_then_complete();
    return 0;
}
