/* Layer 3: storage-format tests that inspect the single file directly. */
#include "support.h"

static void preamble_bytes(void)
{
    char path[160];
    ledger89 *l;
    unsigned char buf[LEDGER89_TEST_PREAMBLE_SIZE];
    size_t got;
    test_path(path, sizeof(path), "fmt-preamble", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    ledger89_close(l);
    got = test_read_file(path, buf, sizeof(buf));
    assert(got == LEDGER89_TEST_PREAMBLE_SIZE);
    assert(test_u64_load_be(buf) == LEDGER89_TEST_MAGIC);
    assert(test_u64_load_be(buf + 8) == (unsigned long long)APPEND89_RESERVE);
    assert(test_logical_size(path) == (off_t)LEDGER89_TEST_PREAMBLE_SIZE);
    test_unlink(path);
}

static void record_layout(void)
{
    char path[160];
    ledger89 *l;
    unsigned char buf[160];
    size_t got;
    static const char *const items[] = {"abc", "", "defgh"};
    static const size_t sizes[] = {3U, 0U, 5U};
    test_path(path, sizeof(path), "fmt-layout", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, items[0], sizes[0], NULL) == 0);
    assert(ledger89_append(l, items[1], sizes[1], NULL) == 0);
    assert(ledger89_append(l, items[2], sizes[2], NULL) == 0);
    ledger89_close(l);
    got = test_read_file(path, buf, sizeof(buf));
    assert(got == 16U + 3U * 16U + 8U);
    /* Preamble. */
    assert(test_u64_load_be(buf) == LEDGER89_TEST_MAGIC);
    assert(test_u64_load_be(buf + 8) == (unsigned long long)APPEND89_RESERVE);
    /* Frame 0: size 3, crc, payload "abc". */
    assert(test_u64_load_be(buf + 16) == 3ULL);
    assert(test_u64_load_be(buf + 24) == test_crc("abc", 3U));
    assert(memcmp(buf + 32, "abc", 3U) == 0);
    /* Frame 1: size 0, crc of empty. */
    assert(test_u64_load_be(buf + 35) == 0ULL);
    assert(test_u64_load_be(buf + 43) == test_crc(NULL, 0U));
    /* Frame 2: size 5, payload "defgh". */
    assert(test_u64_load_be(buf + 51) == 5ULL);
    assert(test_u64_load_be(buf + 59) == test_crc("defgh", 5U));
    assert(memcmp(buf + 67, "defgh", 5U) == 0);
    test_unlink(path);
}

static void iter_offsets(void)
{
    char path[160];
    ledger89 *l;
    ledger89_iter *it;
    test_path(path, sizeof(path), "fmt-offsets", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "BB", 2U, NULL) == 0);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    assert(ledger89_iter_next(it) == LEDGER89_OK);
    assert(ledger89_iter_index(it) == 0ULL);
    assert(ledger89_iter_length(it) == 1ULL);
    assert(ledger89_iter_offset(it) == (ledger89_offset)32);
    assert(ledger89_iter_next(it) == LEDGER89_OK);
    assert(ledger89_iter_index(it) == 1ULL);
    assert(ledger89_iter_length(it) == 2ULL);
    assert(ledger89_iter_offset(it) == (ledger89_offset)49);
    assert(ledger89_iter_next(it) == LEDGER89_END);
    ledger89_iter_close(it);
    ledger89_close(l);
    test_unlink(path);
}

/* A frame whose header claims more payload than the reserve cap admits ends
 * the valid prefix; recovery keeps only the preamble. */
static void oversize_frame(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    ledger89_iter *it;
    test_path(path, sizeof(path), "fmt-oversize", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_header(w, (unsigned long long)APPEND89_RESERVE, 0ULL);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    assert(ledger89_iter_next(it) == LEDGER89_END);
    ledger89_iter_close(it);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)LEDGER89_TEST_PREAMBLE_SIZE);
    test_unlink(path);
}

/* Raw DATA bytes without a valid frame are not records. */
static void garbage_is_not_records(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    ledger89_iter *it;
    test_path(path, sizeof(path), "fmt-garbage", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_bytes(w, "garbage", 7U);
    append89_close(w);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    assert(ledger89_iter_next(it) == LEDGER89_END);
    ledger89_iter_close(it);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)LEDGER89_TEST_PREAMBLE_SIZE);
    test_unlink(path);
}

/* A wrong-magic preamble is rejected at open and never modified. */
static void bad_preamble(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    unsigned char p[LEDGER89_TEST_PREAMBLE_SIZE];
    test_path(path, sizeof(path), "fmt-badmagic", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_u64_store_be(p, 0xDEADBEEFDEADBEEFULL);
    test_u64_store_be(p + 8, (unsigned long long)APPEND89_RESERVE);
    /* Replace the preamble: truncate, rewrite, and republish. */
    assert(append89_truncate(w, 0) == 0);
    test_append_bytes(w, p, sizeof(p));
    append89_close(w);
    assert(ledger89_open_reader(&l, path) == -1 && errno == EINVAL);
    assert(ledger89_open_writer(&l, path) == -1 && errno == EINVAL);
    assert(test_logical_size(path) == (off_t)LEDGER89_TEST_PREAMBLE_SIZE);
    test_unlink(path);
}

int main(void)
{
    preamble_bytes();
    record_layout();
    iter_offsets();
    oversize_frame();
    garbage_is_not_records();
    bad_preamble();
    return 0;
}
