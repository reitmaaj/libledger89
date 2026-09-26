/* Layer 10: corruption tests. Mutate persistent files directly and verify
 * deterministic longest-valid-prefix recovery. */
#include "support.h"

static void flip_byte(const char *file, off_t at)
{
    int fd;
    unsigned char byte;
    fd = open(file, O_RDWR);
    assert(fd >= 0);
    assert(lseek(fd, at, SEEK_SET) == at);
    assert(read(fd, &byte, 1U) == 1);
    byte ^= 0x80U;
    assert(lseek(fd, at, SEEK_SET) == at);
    assert(write(fd, &byte, 1U) == 1);
    assert(close(fd) == 0);
}

static void checksum_flip_middle(void)
{
    char path[160];
    char index[160];
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "corrupt-crc", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "B", 1U, NULL) == 0);
    assert(ledger89_append(l, "C", 1U, NULL) == 0);
    assert(ledger89_append(l, "D", 1U, NULL) == 0);
    ledger89_close(l);
    (void)snprintf(index, sizeof(index), "%s.index", path);
    flip_byte(index, 2 * LEDGER89_TEST_ENTRY_SIZE + 16); /* C's checksum */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 2ULL);
    test_expect(l, 0ULL, "A", 1U);
    test_expect(l, 1ULL, "B", 1U);
    ledger89_close(l);
    test_unlink(path);
}

static void offset_flip_first(void)
{
    char path[160];
    char index[160];
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "corrupt-off", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    ledger89_close(l);
    (void)snprintf(index, sizeof(index), "%s.index", path);
    flip_byte(index, 0); /* first entry's offset */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 0ULL);
    ledger89_close(l);
    test_unlink(path);
}

static void data_mutation(void)
{
    char path[160];
    char data[160];
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "corrupt-data", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "B", 1U, NULL) == 0);
    assert(ledger89_append(l, "C", 1U, NULL) == 0);
    ledger89_close(l);
    (void)snprintf(data, sizeof(data), "%s.data", path);
    flip_byte(data, 1); /* B's payload byte */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    test_expect(l, 0ULL, "A", 1U);
    ledger89_close(l);
    test_unlink(path);
}

static void magic_corrupt(void)
{
    char path[160];
    char index[160];
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "corrupt-magic", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "B", 1U, NULL) == 0);
    ledger89_close(l);
    (void)snprintf(index, sizeof(index), "%s.index", path);
    flip_byte(index, LEDGER89_TEST_ENTRY_SIZE + 24); /* B's magic */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    test_expect(l, 0ULL, "A", 1U);
    ledger89_close(l);
    test_unlink(path);
}

int main(void)
{
    checksum_flip_middle();
    offset_flip_first();
    data_mutation();
    magic_corrupt();
    return 0;
}
