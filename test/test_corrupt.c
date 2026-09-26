/* Layer 10: corruption tests. Mutate the persistent file directly and verify
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
    ledger89 *l;
    test_path(path, sizeof(path), "corrupt-crc", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "B", 1U, NULL) == 0);
    assert(ledger89_append(l, "C", 1U, NULL) == 0);
    assert(ledger89_append(l, "D", 1U, NULL) == 0);
    ledger89_close(l);
    /* C's frame header starts at 16 + 2*17 = 50; its checksum at 58. */
    flip_byte(path, 58);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 2ULL);
    test_expect(l, 0ULL, "A", 1U);
    test_expect(l, 1ULL, "B", 1U);
    ledger89_close(l);
    test_unlink(path);
}

static void size_flip_first(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "corrupt-size", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    ledger89_close(l);
    /* Flip a high byte of the first frame's size: cap violation. */
    flip_byte(path, 16);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 0ULL);
    ledger89_close(l);
    test_unlink(path);
}

static void data_mutation(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "corrupt-data", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "B", 1U, NULL) == 0);
    assert(ledger89_append(l, "C", 1U, NULL) == 0);
    ledger89_close(l);
    /* B's payload byte sits at 16 + 17 + 16 = 49. */
    flip_byte(path, 49);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 1ULL);
    test_expect(l, 0ULL, "A", 1U);
    ledger89_close(l);
    test_unlink(path);
}

static void torn_final_header(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    unsigned char partial[7] = {1U, 2U, 3U, 4U, 5U, 6U, 7U};
    test_path(path, sizeof(path), "corrupt-torn", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "A", 1U, NULL) == 0);
    assert(ledger89_append(l, "B", 1U, NULL) == 0);
    ledger89_close(l);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    assert(append89_append(w, partial, sizeof(partial), NULL) == 0);
    append89_close(w);
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
    checksum_flip_middle();
    size_flip_first();
    data_mutation();
    torn_final_header();
    return 0;
}
