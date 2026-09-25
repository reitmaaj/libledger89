#include "support.h"

static void fail_fragment(const char *call, int occurrence, int number)
{
    char path[160];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    ledger89_offset output;
    test_path(path, sizeof(path), "fault", number);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    a89_fault_reset();
    a89_fault_errno(call, occurrence, ENOSPC);
    output = -77;
    assert(ledger89_append(a, "abcdefghijklmno", 15U, &output) == -1);
    assert(errno == ENOSPC && output == -77);
    a89_fault_reset();
    assert(ledger89_append(a, "replacement", 11U, NULL) == 0);
    cursor = LEDGER89_BEGIN;
    test_expect(a, &cursor, "replacement", 11U);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void retry_short(void)
{
    char path[160];
    ledger89 *a;
    ledger89_offset cursor;
    struct iovec iov[4];
    int i;
    test_path(path, sizeof(path), "short", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    for (i = 0; i < 4; ++i)
    {
        iov[i].iov_base = (void *)"abcde";
        iov[i].iov_len = i == 1 ? 0U : 5U;
    }
    a89_fault_reset();
    a89_fault_short_writes(2U);
    a89_fault_eintr("pwrite", 1, 3);
    a89_fault_eintr("fdatasync", 1, 2);
    a89_fault_eintr("ftruncate", 1, 2);
    assert(ledger89_appendv(a, iov, 4, NULL) == 0);
    a89_fault_reset();
    a89_fault_eintr("pread", 1, 3);
    cursor = LEDGER89_BEGIN;
    test_expect(a, &cursor, "abcdeabcdeabcde", 15U);
    a89_fault_reset();
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void uncertain(void)
{
    char path[160];
    ledger89 *a;
    ledger89_offset cursor;
    test_path(path, sizeof(path), "uncertain", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    a89_fault_reset();
    a89_fault_errno("fcntl", 2, EIO);
    assert(ledger89_append(a, "hello", 5U, NULL) == -1);
    assert(errno == EIO);
    a89_fault_reset();
    assert(ledger89_append(a, "bad", 3U, NULL) == -1 && errno == EIO);
    ledger89_close(a);
    assert(ledger89_open_reader(&a, path, 32U) == 0);
    cursor = LEDGER89_BEGIN;
    test_expect(a, &cursor, "hello", 5U);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void recovery_retry(void)
{
    char path[160];
    ledger89 *a;
    append89 *w;
    ledger89_offset after;
    ledger89_offset cursor;
    test_path(path, sizeof(path), "recovery-sync", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(append89_open_writer_reserve(&w, path, (mode_t)0, 32U) == 0);
    test_fragment(w, LEDGER89_BEGIN, 100UL, "abc", 3U);
    append89_close(w);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    a89_fault_reset();
    a89_fault_errno("fdatasync", 1, EIO);
    after = -77;
    assert(ledger89_recover(a, NULL, &after) == -1 && errno == EIO);
    assert(after == -77);
    a89_fault_reset();
    /* Stay in maintenance: complete the barrier before allowing new writes. */
    assert(ledger89_recover(a, NULL, &after) == 0);
    assert(after == LEDGER89_BEGIN);
    assert(ledger89_append(a, "safe", 4U, NULL) == 0);
    cursor = LEDGER89_BEGIN;
    test_expect(a, &cursor, "safe", 4U);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

int main(void)
{
    fail_fragment("pwrite", 1, 0);
    fail_fragment("pwrite", 3, 1);
    fail_fragment("fdatasync", 1, 2);
    fail_fragment("fdatasync", 2, 3);
    fail_fragment("ftruncate", 1, 4);
    fail_fragment("ftruncate", 2, 5);
    retry_short();
    uncertain();
    recovery_retry();
    return 0;
}
