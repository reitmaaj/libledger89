/* Layer 4: deterministic recovery tests. Each test crafts an exact on-disk
 * state, runs recovery, and verifies the canonical longest-valid-prefix
 * result, then verifies recovery is idempotent. */
#include "support.h"

static void data_path(char *out, size_t size, const char *path)
{
    (void)snprintf(out, size, "%s.data", path);
}

static void index_path(char *out, size_t size, const char *path)
{
    (void)snprintf(out, size, "%s.index", path);
}

/* Recover once, capture file sizes, recover again, and compare sizes. */
static void recover_twice(const char *path)
{
    ledger89 *l;
    off_t d1;
    off_t i1;
    off_t d2;
    off_t i2;
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    ledger89_close(l);
    d1 = test_data_logical_size(path);
    i1 = test_index_logical_size(path);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    ledger89_close(l);
    d2 = test_data_logical_size(path);
    i2 = test_index_logical_size(path);
    assert(d1 == d2);
    assert(i1 == i2);
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

static void uncommitted_data_tail(void)
{
    char path[160];
    char data[160];
    char index[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "rec-tail", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    data_path(data, sizeof(data), path);
    index_path(index, sizeof(index), path);
    assert(append89_open_writer(&w, data, (mode_t)0) == 0);
    test_append_data(w, "ABgarbage", 10U);
    append89_close(w);
    assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                        LEDGER89_TEST_INDEX_RESERVE) == 0);
    test_append_index(w, 0ULL, 1ULL, test_crc("A", 1U));
    test_append_index(w, 1ULL, 1ULL, test_crc("B", 1U));
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 2ULL);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 2);
    test_unlink(path);
}

static void data_only(void)
{
    char path[160];
    char data[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "rec-dataonly", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    data_path(data, sizeof(data), path);
    assert(append89_open_writer(&w, data, (mode_t)0) == 0);
    test_append_data(w, "arbitrary", 9U);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 0ULL);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 0);
    test_unlink(path);
}

static void partial_index_entry(int partial)
{
    char path[160];
    char index[160];
    append89 *w;
    ledger89 *l;
    unsigned char garbage[31];
    unsigned long long count;
    int i;
    test_path(path, sizeof(path), "rec-partial", partial);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    index_path(index, sizeof(index), path);
    assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                        LEDGER89_TEST_INDEX_RESERVE) == 0);
    for (i = 0; i < partial; ++i)
    {
        garbage[i] = (unsigned char)(i + 1);
    }
    assert(append89_append(w, garbage, (size_t)partial, NULL) == 0);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    test_expect(l, 0ULL, "base", 4U);
    ledger89_close(l);
    assert(test_index_logical_size(path) == LEDGER89_TEST_ENTRY_SIZE);
    assert(test_data_logical_size(path) == 4);
    test_unlink(path);
}

static void beyond_eof(void)
{
    char path[160];
    char data[160];
    char index[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "rec-beyondeof", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    data_path(data, sizeof(data), path);
    index_path(index, sizeof(index), path);
    assert(append89_open_writer(&w, data, (mode_t)0) == 0);
    test_append_data(w, "AB", 2U);
    append89_close(w);
    assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                        LEDGER89_TEST_INDEX_RESERVE) == 0);
    test_append_index(w, 0ULL, 1ULL, test_crc("A", 1U));
    test_append_index(w, 1ULL, 1ULL, test_crc("B", 1U));
    test_append_index(w, 2ULL, 5ULL, test_crc("XXXXX", 5U)); /* extends beyond */
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 2ULL);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 2);
    assert(test_index_logical_size(path) == 2 * LEDGER89_TEST_ENTRY_SIZE);
    test_unlink(path);
}

static void gap(void)
{
    char path[160];
    char data[160];
    char index[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "rec-gap", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    data_path(data, sizeof(data), path);
    index_path(index, sizeof(index), path);
    assert(append89_open_writer(&w, data, (mode_t)0) == 0);
    test_append_data(w, "AAAAAAAAAABBBBB", 15U);
    append89_close(w);
    assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                        LEDGER89_TEST_INDEX_RESERVE) == 0);
    test_append_index(w, 0ULL, 10ULL, test_crc("AAAAAAAAAA", 10U));
    test_append_index(w, 20ULL, 5ULL, test_crc("BBBBB", 5U)); /* gap */
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 10);
    test_unlink(path);
}

static void overlap(void)
{
    char path[160];
    char data[160];
    char index[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "rec-overlap", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    data_path(data, sizeof(data), path);
    index_path(index, sizeof(index), path);
    assert(append89_open_writer(&w, data, (mode_t)0) == 0);
    test_append_data(w, "AAAAAAAAAA", 10U);
    append89_close(w);
    assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                        LEDGER89_TEST_INDEX_RESERVE) == 0);
    test_append_index(w, 0ULL, 10ULL, test_crc("AAAAAAAAAA", 10U));
    test_append_index(w, 5ULL, 5ULL, test_crc("AAAAA", 5U)); /* overlap */
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    ledger89_close(l);
    test_unlink(path);
}

static void invalid_first_entry(void)
{
    char path[160];
    char data[160];
    char index[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "rec-first", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    data_path(data, sizeof(data), path);
    index_path(index, sizeof(index), path);
    assert(append89_open_writer(&w, data, (mode_t)0) == 0);
    test_append_data(w, "AAAAA", 5U);
    append89_close(w);
    assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                        LEDGER89_TEST_INDEX_RESERVE) == 0);
    test_append_index(w, 5ULL, 1ULL, test_crc("A", 1U)); /* offset != 0 */
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 0ULL);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 0);
    assert(test_index_logical_size(path) == 0);
    test_unlink(path);
}

static void middle_corruption(void)
{
    char path[160];
    char index[160];
    ledger89 *l;
    unsigned long long count;
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
    index_path(index, sizeof(index), path);
    /* Corrupt C's checksum: entry 2, checksum field at 2*32 + 16 = 80. */
    fd = open(index, O_RDWR);
    assert(fd >= 0);
    assert(lseek(fd, 2 * LEDGER89_TEST_ENTRY_SIZE + 16, SEEK_SET) ==
           2 * LEDGER89_TEST_ENTRY_SIZE + 16);
    assert(read(fd, &byte, 1U) == 1);
    byte ^= 0x80U;
    assert(lseek(fd, 2 * LEDGER89_TEST_ENTRY_SIZE + 16, SEEK_SET) ==
           2 * LEDGER89_TEST_ENTRY_SIZE + 16);
    assert(write(fd, &byte, 1U) == 1);
    assert(close(fd) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 2ULL);
    test_expect(l, 0ULL, "A", 1U);
    test_expect(l, 1ULL, "B", 1U);
    ledger89_close(l);
    test_unlink(path);
}

int main(void)
{
    int i;
    clean_state();
    uncommitted_data_tail();
    data_only();
    for (i = 1; i < (int)LEDGER89_TEST_ENTRY_SIZE; ++i)
    {
        partial_index_entry(i);
    }
    beyond_eof();
    gap();
    overlap();
    invalid_first_entry();
    middle_corruption();
    return 0;
}
