#include "support.h"

static void open_errors(void)
{
    ledger89 *a;

    a = (ledger89 *)1;
    assert(ledger89_open_reader(NULL, "x", 0U) == -1 && errno == EINVAL);
    assert(ledger89_open_writer(NULL, "x", 0U) == -1 && errno == EINVAL);
    assert(ledger89_open_reader(&a, NULL, 0U) == -1 && errno == EINVAL);
    assert(a == NULL);
    assert(ledger89_open_writer(&a, NULL, 0U) == -1 && errno == EINVAL);
    assert(a == NULL);
    assert(ledger89_open_reader(&a, "/nonexistent-ledger89-api", 0U) == -1);
    assert(errno == ENOENT && a == NULL);
}

static void writer_only_errors(void)
{
    char path[160];
    ledger89 *a;
    test_path(path, sizeof(path), "api-ro", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_reader(&a, path, 32U) == 0);
    assert(ledger89_append(a, "x", 1U, NULL) == -1 && errno == EBADF);
    assert(ledger89_sync(a) == -1 && errno == EBADF);
    assert(ledger89_recover(a, NULL, NULL) == -1 && errno == EBADF);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void message_view_errors(void)
{
    char path[160];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    size_t length;
    char buf[4];
    test_path(path, sizeof(path), "api-msg", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, "data", 4U, NULL) == 0);
    ledger89_close(a);

    assert(ledger89_open_reader(&a, path, 32U) == 0);
    assert(ledger89_message_length(NULL) == 0ULL);
    assert(ledger89_message_offset(NULL) == (off_t)-1);
    assert(ledger89_message_read(NULL, buf, sizeof(buf)) == -1);
    assert(errno == EINVAL);

    cursor = LEDGER89_BEGIN;
    assert(ledger89_next(a, &cursor, NULL) == -1 && errno == EINVAL);

    cursor = LEDGER89_BEGIN;
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_OK);
    assert(ledger89_message_offset(m) == LEDGER89_BEGIN);
    assert(u64_to_size(ledger89_message_length(m), &length) == 0);
    assert(length == 4U);
    assert(ledger89_message_read(m, NULL, 4U) == -1 && errno == EINVAL);
    assert(ledger89_message_read(m, buf, 0U) == 0);
    ledger89_message_close(m);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void stale_errno(void)
{
    char path[160];
    ledger89 *a;
    ledger89_offset cursor;
    ledger89_offset off;
    test_path(path, sizeof(path), "api-errno", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    errno = EINVAL;
    assert(ledger89_append(a, "ok", 2U, &off) == 0);
    assert(off == LEDGER89_BEGIN);
    errno = ENOSPC;
    assert(ledger89_sync(a) == 0);
    ledger89_close(a);

    assert(ledger89_open_reader(&a, path, 32U) == 0);
    errno = EINVAL;
    cursor = LEDGER89_BEGIN;
    test_expect(a, &cursor, "ok", 2U);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

int main(void)
{
    open_errors();
    writer_only_errors();
    message_view_errors();
    stale_errno();
    return 0;
}
