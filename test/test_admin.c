/* Administrative inspection and repair over the single-file ledger. */
#include "support.h"
#include "ledger89/admin.h"

static void clean(void)
{
    char path[160];
    ledger89 *l;
    ledger89_admin_report report;
    test_path(path, sizeof(path), "adm-clean", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "one", 3U, NULL) == 0);
    assert(ledger89_append(l, "two", 3U, NULL) == 0);
    ledger89_close(l);
    memset(&report, 0x7f, sizeof(report));
    assert(ledger89_admin_check(path, &report) == 0);
    assert(report.incomplete_tail == 0);
    assert(report.records == 2ULL);
    assert(report.valid_end == (ledger89_offset)(16 + 2 * 19));
    test_unlink(path);
}

static void incomplete(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    ledger89_admin_report report;
    unsigned char partial[5];
    int i;
    test_path(path, sizeof(path), "adm-partial", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    for (i = 0; i < 5; ++i)
    {
        partial[i] = (unsigned char)(i + 1);
    }
    assert(append89_append(w, partial, sizeof(partial), NULL) == 0);
    append89_close(w);
    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_check(path, &report) == 0);
    assert(report.incomplete_tail == 1);
    assert(report.records == 1ULL);
    assert(report.valid_end == (ledger89_offset)(16 + 20));
    test_unlink(path);
}

static void repair(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    ledger89_admin_report report;
    unsigned char partial[5] = {1U, 2U, 3U, 4U, 5U};
    test_path(path, sizeof(path), "adm-repair", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    assert(append89_append(w, partial, sizeof(partial), NULL) == 0);
    append89_close(w);
    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_repair(path, &report) == 0);
    assert(report.incomplete_tail == 1);
    assert(report.records == 1ULL);
    assert(report.valid_end == (ledger89_offset)(16 + 20));
    /* Idempotent. */
    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_repair(path, &report) == 0);
    assert(report.incomplete_tail == 0);
    assert(report.records == 1ULL);
    assert(report.valid_end == (ledger89_offset)(16 + 20));
    /* A later append begins exactly at the repaired boundary. */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "next", 4U, NULL) == 0);
    test_expect(l, 0ULL, "base", 4U);
    test_expect(l, 1ULL, "next", 4U);
    ledger89_close(l);
    test_unlink(path);
}

static void errors(void)
{
    char path[160];
    ledger89_admin_report report;
    test_path(path, sizeof(path), "adm-errors", 0);
    memset(&report, 0, sizeof(report));
    assert(ledger89_admin_check(path, &report) == -1);
    assert(errno == ENOENT);
    assert(ledger89_admin_repair(path, &report) == -1);
    assert(errno == ENOENT);
    assert(mkdir(path, 0700) == 0);
    assert(ledger89_admin_check(path, &report) == -1);
    assert(ledger89_admin_repair(path, &report) == -1);
    assert(rmdir(path) == 0);
}

int main(void)
{
    clean();
    incomplete();
    repair();
    errors();
    return 0;
}
