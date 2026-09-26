/* Layer 4: deterministic recovery tests. Each test crafts an exact on-disk
 * state, runs recovery, and verifies the canonical longest-valid-prefix
 * result, then verifies recovery is idempotent. */
#include "support.h"

/* Recover once, capture the logical size, recover again, and compare. */
static void recover_twice(const char *path)
{
    ledger89 *l;
    off_t s1;
    off_t s2;
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    ledger89_close(l);
    s1 = test_logical_size(path);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    ledger89_close(l);
    s2 = test_logical_size(path);
    assert(s1 == s2);
}

static void clean_state(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "rec-clean", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "AAAA", 4U, NULL) == 0);
    assert(ledger89_append(l, "BBBB", 4U, NULL) == 0);
    ledger89_close(l);
    recover_twice(path);
    assert(ledger89_open_reader(&l, path) == 0);
    test_expect(l, 0ULL, "AAAA", 4U);
    test_expect(l, 1ULL, "BBBB", 4U);
    ledger89_close(l);
    test_unlink(path);
}

static void garbage_tail(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    test_path(path, sizeof(path), "rec-garbage", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_bytes(w, "garbage!", 8U);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 1ULL);
    test_expect(l, 0ULL, "base", 4U);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)(16 + 16 + 4));
    recover_twice(path);
    test_unlink(path);
}

static void partial_header(int partial)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    unsigned char garbage[15];
    int i;
    test_path(path, sizeof(path), "rec-partial", partial);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    for (i = 0; i < partial; ++i)
    {
        garbage[i] = (unsigned char)(i + 1);
    }
    assert(append89_append(w, garbage, (size_t)partial, NULL) == 0);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 1ULL);
    test_expect(l, 0ULL, "base", 4U);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)(16 + 16 + 4));
    test_unlink(path);
}

static void beyond_eof(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    test_path(path, sizeof(path), "rec-beyondeof", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "AB", 2U, NULL) == 0);
    ledger89_close(l);
    /* A frame whose header claims 100 payload bytes, with none present. */
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_header(w, 100ULL, 0ULL);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 1ULL);
    test_expect(l, 0ULL, "AB", 2U);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)(16 + 16 + 2));
    test_unlink(path);
}

static void oversize_frame(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    test_path(path, sizeof(path), "rec-oversize", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "AAAAAA", 6U, NULL) == 0);
    ledger89_close(l);
    /* A frame whose size violates the reserve cap. */
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_header(w, (unsigned long long)APPEND89_RESERVE, 0ULL);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 1ULL);
    test_expect(l, 0ULL, "AAAAAA", 6U);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)(16 + 16 + 6));
    test_unlink(path);
}

static void checksum_mismatch(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    test_path(path, sizeof(path), "rec-crc", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "keep", 4U, NULL) == 0);
    ledger89_close(l);
    /* A frame with a wrong checksum ends the prefix. */
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_header(w, 3ULL, test_crc("bad", 3U) ^ 1ULL);
    assert(append89_append(w, "bad", 3U, NULL) == 0);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 1ULL);
    test_expect(l, 0ULL, "keep", 4U);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)(16 + 16 + 4));
    test_unlink(path);
}

static void invalid_first(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    test_path(path, sizeof(path), "rec-first", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_header(w, (unsigned long long)APPEND89_RESERVE, 0ULL);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 0ULL);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)16);
    test_unlink(path);
}

static void middle_corruption(void)
{
    char path[160];
    ledger89 *l;
    int fd;
    unsigned char byte;
    test_path(path, sizeof(path), "rec-middle", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "B", 1U, NULL) == 0);
    assert(ledger89_append(l, "C", 1U, NULL) == 0);
    assert(ledger89_append(l, "D", 1U, NULL) == 0);
    assert(ledger89_append(l, "E", 1U, NULL) == 0);
    ledger89_close(l);
    /* C's payload byte sits at 16 + 2*17 + 16 = 66. */
    fd = open(path, O_RDWR);
    assert(fd >= 0);
    assert(lseek(fd, 66, SEEK_SET) == 66);
    assert(read(fd, &byte, 1U) == 1);
    byte ^= 0x80U;
    assert(lseek(fd, 66, SEEK_SET) == 66);
    assert(write(fd, &byte, 1U) == 1);
    assert(close(fd) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 2ULL);
    test_expect(l, 0ULL, "A", 1U);
    test_expect(l, 1ULL, "B", 1U);
    ledger89_close(l);
    test_unlink(path);
}

int main(void)
{
    int i;
    clean_state();
    garbage_tail();
    for (i = 1; i < (int)LEDGER89_TEST_HEADER_SIZE; ++i)
    {
        partial_header(i);
    }
    beyond_eof();
    oversize_frame();
    checksum_mismatch();
    invalid_first();
    middle_corruption();
    return 0;
}
