/* Layer 3: storage-format tests that inspect DATA and INDEX directly. */
#include "support.h"

static void data_purity(void)
{
    char path[160];
    ledger89 *l;
    unsigned char buf[64];
    size_t got;
    test_path(path, sizeof(path), "fmt-purity", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "BC", 2U, NULL) == 0);
    assert(ledger89_append(l, "DEF", 3U, NULL) == 0);
    ledger89_close(l);
    got = test_read_data(path, buf, sizeof(buf));
    assert(got == 6U);
    assert(memcmp(buf, "ABCDEF", 6U) == 0);
    test_unlink(path);
}

static void index_authority(void)
{
    char path[160];
    char data[160];
    char index[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "fmt-auth", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    (void)snprintf(data, sizeof(data), "%s.data", path);
    (void)snprintf(index, sizeof(index), "%s.index", path);
    /* DATA = A || B || C, but INDEX describes only A and B. */
    assert(append89_open_writer(&w, data, (mode_t)0) == 0);
    test_append_data(w, "ABC", 3U);
    append89_close(w);
    assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                        LEDGER89_TEST_INDEX_RESERVE) == 0);
    test_append_index(w, 0ULL, 1ULL, test_crc("A", 1U));
    test_append_index(w, 1ULL, 1ULL, test_crc("B", 1U));
    append89_close(w);
    /* C disappears on recovery; A and B survive. */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 2ULL);
    test_expect(l, 0ULL, "A", 1U);
    test_expect(l, 1ULL, "B", 1U);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 2);
    test_unlink(path);
}

static void data_cannot_imply_records(void)
{
    char path[160];
    char data[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "fmt-dataonly", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    (void)snprintf(data, sizeof(data), "%s.data", path);
    assert(append89_open_writer(&w, data, (mode_t)0) == 0);
    test_append_data(w, "garbage", 7U);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 0ULL);
    ledger89_close(l);
    assert(test_data_logical_size(path) == 0);
    test_unlink(path);
}

static void index_defines_framing(void)
{
    char path[160];
    ledger89 *l;
    unsigned char buf[4 * LEDGER89_TEST_ENTRY_SIZE];
    size_t got;
    static const char *const items[] = {"abc", "", "defgh", "i"};
    static const size_t sizes[] = {3U, 0U, 5U, 1U};
    static const unsigned long long offsets[] = {0ULL, 3ULL, 3ULL, 8ULL};
    static const unsigned long long lengths[] = {3ULL, 0ULL, 5ULL, 1ULL};
    size_t i;
    test_path(path, sizeof(path), "fmt-framing", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    for (i = 0U; i < 4U; ++i)
    {
        assert(ledger89_append(l, items[i], sizes[i], NULL) == 0);
    }
    ledger89_close(l);
    got = test_read_index(path, buf, sizeof(buf));
    assert(got == 4U * LEDGER89_TEST_ENTRY_SIZE);
    for (i = 0U; i < 4U; ++i)
    {
        assert(test_u64_load_be(buf + i * LEDGER89_TEST_ENTRY_SIZE) ==
               offsets[i]);
        assert(test_u64_load_be(buf + i * LEDGER89_TEST_ENTRY_SIZE + 8) ==
               lengths[i]);
    }
    test_unlink(path);
}

static void empty_record_checksum(void)
{
    char path[160];
    ledger89 *l;
    unsigned char buf[LEDGER89_TEST_ENTRY_SIZE];
    size_t got;
    test_path(path, sizeof(path), "fmt-emptycrc", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, NULL, 0U, NULL) == 0);
    ledger89_close(l);
    got = test_read_index(path, buf, sizeof(buf));
    assert(got == LEDGER89_TEST_ENTRY_SIZE);
    /* Zero-length record: offset 0, length 0, checksum of the empty input. */
    assert(test_u64_load_be(buf) == 0ULL);
    assert(test_u64_load_be(buf + 8) == 0ULL);
    assert(test_u64_load_be(buf + 16) == test_crc(NULL, 0U));
    test_unlink(path);
}

int main(void)
{
    data_purity();
    index_authority();
    data_cannot_imply_records();
    index_defines_framing();
    empty_record_checksum();
    return 0;
}
