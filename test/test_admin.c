#include "support.h"
#include "ledger89/admin.h"

static void clean(void)
{
    char path[160];
    ledger89 *a;
    ledger89_admin_report report;
    test_path(path, sizeof(path), "adm-clean", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, "one", 3U, NULL) == 0);
    assert(ledger89_append(a, "two", 3U, NULL) == 0);
    assert(ledger89_sync(a) == 0);
    ledger89_close(a);

    memset(&report, 0x7f, sizeof(report));
    assert(ledger89_admin_check(path, &report) == 0);
    assert(report.incomplete_tail == 0);
    assert(report.records == 2UL);
    assert(report.valid_end == 16 + 27 + 27);
    assert(unlink(path) == 0);
}

static void incomplete(void)
{
    char path[160];
    append89 *w;
    ledger89 *a;
    ledger89_admin_report report;
    ledger89_offset end;
    off_t size;
    test_path(path, sizeof(path), "adm-partial", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, "base", 4U, NULL) == 0);
    assert(ledger89_sync(a) == 0);
    ledger89_close(a);
    assert(append89_open_writer_reserve(&w, path, (mode_t)0, 32U) == 0);
    assert(append89_begin(w, &end) == 0);
    test_fragment(w, end, 100UL, "abcdefgh", 8U);
    assert(append89_end(w) == 0);
    append89_close(w);
    size = test_size(path);

    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_check(path, &report) == 0);
    assert(report.incomplete_tail == 1);
    assert(report.records == 1UL);
    assert(report.valid_end == 16 + 24 + 4);
    assert(test_size(path) == size); /* check never mutates */
    assert(unlink(path) == 0);
}

static void repair(void)
{
    char path[160];
    append89 *w;
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset end;
    ledger89_offset cursor;
    ledger89_admin_report report;
    test_path(path, sizeof(path), "adm-repair", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, "base", 4U, NULL) == 0);
    assert(ledger89_sync(a) == 0);
    ledger89_close(a);
    assert(append89_open_writer_reserve(&w, path, (mode_t)0, 32U) == 0);
    assert(append89_begin(w, &end) == 0);
    test_fragment(w, end, 100UL, "abcdefgh", 8U);
    assert(append89_end(w) == 0);
    append89_close(w);

    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_repair(path, &report) == 0);
    assert(report.incomplete_tail == 1);
    assert(report.records == 1UL);
    assert(report.valid_end == 16 + 24 + 4);

    /* Idempotent: a second repair sees no incomplete tail. */
    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_repair(path, &report) == 0);
    assert(report.incomplete_tail == 0);
    assert(report.records == 1UL);
    assert(report.valid_end == 16 + 24 + 4);

    /* A later append begins exactly at the repaired boundary. */
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, "next", 4U, NULL) == 0);
    cursor = LEDGER89_BEGIN;
    test_expect(a, &cursor, "base", 4U);
    test_expect(a, &cursor, "next", 4U);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

static void interior_corrupt(void)
{
    char path[160];
    ledger89 *a;
    ledger89_admin_report report;
    unsigned char corrupt;
    off_t size;
    int fd;
    test_path(path, sizeof(path), "adm-corrupt", 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, "A", 1U, NULL) == 0);
    assert(ledger89_append(a, "B", 1U, NULL) == 0);
    assert(ledger89_append(a, "C", 1U, NULL) == 0);
    assert(ledger89_sync(a) == 0);
    ledger89_close(a);
    size = test_size(path);
    /* B's header starts at 16 + 25 = 41; its fragment-length LSB is at
     * 41 + 23 = 64. 9 exceeds the 8-byte fragment capacity (reserve 32). */
    fd = open(path, O_WRONLY);
    assert(fd >= 0);
    assert(lseek(fd, 64, SEEK_SET) == 64);
    corrupt = 9U;
    assert(write(fd, &corrupt, 1U) == 1);
    assert(close(fd) == 0);

    memset(&report, 0xAB, sizeof(report));
    assert(ledger89_admin_check(path, &report) == -1);
    assert(errno == EILSEQ);
    assert(ledger89_admin_repair(path, &report) == -1);
    assert(errno == EILSEQ);
    assert(test_size(path) == size); /* never truncate valid history */
    assert(unlink(path) == 0);
}

static void reserve_discovery(void)
{
    char path[160];
    ledger89 *a;
    ledger89_admin_report report;
    test_path(path, sizeof(path), "adm-reserve", 0);
    assert(ledger89_create(path, (mode_t)0600, 64U) == 0);
    assert(ledger89_open_writer(&a, path, 64U) == 0);
    assert(ledger89_append(a, "hello", 5U, NULL) == 0);
    assert(ledger89_sync(a) == 0);
    ledger89_close(a);

    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_check(path, &report) == 0);
    assert(report.records == 1UL);
    assert(report.incomplete_tail == 0);
    assert(report.valid_end == 16 + 24 + 5);
    assert(unlink(path) == 0);
}

static void errors(void)
{
    char path[160];
    unsigned char header[16];
    ledger89_admin_report report;
    int fd;
    test_path(path, sizeof(path), "adm-errors", 0);

    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_check(path, &report) == -1);
    assert(errno == ENOENT);
    assert(ledger89_admin_repair(path, &report) == -1);
    assert(errno == ENOENT);

    assert(mkdir(path, 0700) == 0);
    assert(ledger89_admin_check(path, &report) == -1);
    assert(errno == EINVAL);
    assert(ledger89_admin_repair(path, &report) == -1);
    assert(errno == EINVAL);
    assert(rmdir(path) == 0);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, "LEDG", 4U) == 4);
    assert(close(fd) == 0);
    assert(ledger89_admin_check(path, &report) == -1);
    assert(errno == EINVAL);
    assert(unlink(path) == 0);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    memset(header, 0, sizeof(header));
    memcpy(header, "WRONG", 5U);
    assert(write(fd, header, sizeof(header)) == (ssize_t)sizeof(header));
    assert(close(fd) == 0);
    assert(ledger89_admin_check(path, &report) == -1);
    assert(errno == EINVAL);
    assert(unlink(path) == 0);
}

int main(void)
{
    clean();
    incomplete();
    repair();
    interior_corrupt();
    reserve_discovery();
    errors();
    return 0;
}
