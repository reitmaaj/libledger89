#include "support.h"

static void process_death(int boundary)
{
    char path[160];
    char payload[20];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    pid_t child;
    test_path(path, sizeof(path), "kill", boundary);
    memset(payload, 'A', sizeof(payload));
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        assert(ledger89_open_writer(&a, path, 32U) == 0);
        a89_fault_reset();
        a89_fault_kill_after("ftruncate", boundary);
        (void)ledger89_append(a, payload, sizeof(payload), NULL);
        _exit(3);
    }
    test_wait(child, 1);
    /* No recovery. The next writer resumes from file-size-derived EOF. */
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    payload[0] = 'B';
    assert(ledger89_append(a, payload, sizeof(payload), NULL) == 0);
    /* Recovery must retain complete messages after abandoned predecessors. */
    assert(ledger89_recover(a, NULL, NULL) == 0);
    cursor = LEDGER89_BEGIN;
    if (boundary == 3)
    {
        payload[0] = 'A';
        test_expect(a, &cursor, payload, sizeof(payload));
        payload[0] = 'B';
    }
    test_expect(a, &cursor, payload, sizeof(payload));
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void every_prefix(void)
{
    char path[160];
    char payload[20];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    ledger89_offset start;
    ledger89_offset before;
    ledger89_offset after;
    off_t full;
    int cut;
    int fd;
    memset(payload, 'q', sizeof(payload));
    /* 20 payload bytes + three 24-byte headers = 92 physical stream bytes. */
    for (cut = 0; cut <= 92; ++cut)
    {
        test_path(path, sizeof(path), "prefix", cut);
        assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
        assert(ledger89_open_writer(&a, path, 32U) == 0);
        assert(ledger89_append(a, "base", 4U, NULL) == 0);
        assert(ledger89_sync(a) == 0);
        assert(ledger89_append(a, payload, sizeof(payload), &start) == 0);
        full = test_size(path);
        assert(full == start + 92 + 32);
        ledger89_close(a);
        fd = open(path, O_WRONLY);
        assert(fd >= 0);
        assert(ftruncate(fd, start + cut + 32) == 0);
        assert(close(fd) == 0);
        assert(ledger89_open_writer(&a, path, 32U) == 0);
        cursor = LEDGER89_BEGIN;
        test_expect(a, &cursor, "base", 4U);
        if (cut > 0 && cut < 92)
        {
            assert(ledger89_next(a, &cursor, &m) == LEDGER89_PARTIAL);
            assert(cursor == start);
        }
        assert(ledger89_recover(a, &before, &after) == 0);
        assert(before == start + cut);
        assert(after == (cut == 92 ? before : start));
        assert(ledger89_append(a, "next", 4U, NULL) == 0);
        assert(ledger89_sync(a) == 0);
        cursor = LEDGER89_BEGIN;
        test_expect(a, &cursor, "base", 4U);
        if (cut == 92)
        {
            test_expect(a, &cursor, payload, sizeof(payload));
        }
        test_expect(a, &cursor, "next", 4U);
        assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
        ledger89_close(a);
        assert(unlink(path) == 0);
    }
}

static void torn_new_header(void)
{
    char path[160];
    append89 *w;
    ledger89 *a;
    ledger89_offset end;
    ledger89_offset after;
    int cut;
    for (cut = 1; cut < 24; ++cut)
    {
        test_path(path, sizeof(path), "torn-new", cut);
        assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
        assert(append89_open_writer_reserve(&w, path, (mode_t)0, 32U) == 0);
        assert(append89_begin(w, &end) == 0);
        test_fragment(w, end, 100UL, "abc", 3U);
        assert(append89_append(w, "012345678901234567890123", (size_t)cut, NULL) == 0);
        assert(append89_end(w) == 0);
        append89_close(w);
        assert(ledger89_open_writer(&a, path, 32U) == 0);
        assert(ledger89_recover(a, NULL, &after) == 0);
        assert(after == LEDGER89_BEGIN);
        ledger89_close(a);
        assert(unlink(path) == 0);
    }
}

int main(void)
{
    process_death(1);
    process_death(2);
    process_death(3);
    every_prefix();
    torn_new_header();
    return 0;
}
