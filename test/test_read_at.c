/* Layer: direct record access by payload offset. Verifies framing, bounds,
 * capacity handling, checksum verification, and failure atomicity. */
#include "support.h"

static void read_basic(void)
{
    char path[160];
    ledger89 *l;
    ledger89_iter *it;
    ledger89_offset first;
    ledger89_offset second;
    ledger89_offset empty;
    unsigned char buf[16];
    test_path(path, sizeof(path), "readat-basic", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "alpha", 5U, &first) == 0);
    assert(ledger89_append(l, "beta", 4U, &second) == 0);
    assert(ledger89_append(l, NULL, 0U, &empty) == 0);
    assert(first == (ledger89_offset)32);
    assert(second == (ledger89_offset)53);
    assert(empty == (ledger89_offset)73);
    memset(buf, 0, sizeof(buf));
    assert(ledger89_read_at(l, first, buf, sizeof(buf)) == 5);
    assert(memcmp(buf, "alpha", 5U) == 0);
    memset(buf, 0, sizeof(buf));
    assert(ledger89_read_at(l, second, buf, sizeof(buf)) == 4);
    assert(memcmp(buf, "beta", 4U) == 0);
    assert(ledger89_read_at(l, empty, NULL, 0U) == 0);
    ledger89_close(l);

    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    assert(ledger89_iter_next(it) == LEDGER89_OK);
    assert(ledger89_iter_offset(it) == first);
    assert(ledger89_read_at(l, ledger89_iter_offset(it), buf, sizeof(buf)) ==
           5);
    assert(memcmp(buf, "alpha", 5U) == 0);
    assert(ledger89_iter_next(it) == LEDGER89_OK);
    assert(ledger89_iter_offset(it) == second);
    memset(buf, 0, sizeof(buf));
    assert(ledger89_read_at(l, ledger89_iter_offset(it), buf, sizeof(buf)) ==
           4);
    assert(memcmp(buf, "beta", 4U) == 0);
    ledger89_iter_close(it);
    ledger89_close(l);
    test_unlink(path);
}

static void consecutive_empty(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset a;
    ledger89_offset b;
    test_path(path, sizeof(path), "readat-empty", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, NULL, 0U, &a) == 0);
    assert(ledger89_append(l, NULL, 0U, &b) == 0);
    assert(a == (ledger89_offset)32);
    assert(b == (ledger89_offset)48);
    assert(ledger89_read_at(l, a, NULL, 0U) == 0);
    assert(ledger89_read_at(l, b, NULL, 0U) == 0);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_read_at(l, a, NULL, 0U) == 0);
    assert(ledger89_read_at(l, b, NULL, 0U) == 0);
    ledger89_close(l);
    test_unlink(path);
}

static void maximum_payload(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset offset;
    unsigned char *payload;
    unsigned char *buf;
    size_t max;
    size_t i;
    test_path(path, sizeof(path), "readat-max", 0);
    max = (size_t)APPEND89_RESERVE - LEDGER89_TEST_HEADER_SIZE;
    payload = (unsigned char *)malloc(max);
    buf = (unsigned char *)malloc(max);
    assert(payload != NULL && buf != NULL);
    for (i = 0U; i < max; ++i)
    {
        payload[i] = (unsigned char)(i * 29U + 11U);
    }
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, payload, max, &offset) == 0);
    assert(ledger89_read_at(l, offset, buf, max) == (ssize_t)max);
    assert(memcmp(buf, payload, max) == 0);
    ledger89_close(l);
    free(payload);
    free(buf);
    test_unlink(path);
}

static void argument_errors(void)
{
    char path[160];
    ledger89 *l;
    unsigned char buf[8];
    test_path(path, sizeof(path), "readat-args", 0);
    assert(ledger89_read_at(NULL, (ledger89_offset)32, buf, sizeof(buf)) == -1);
    assert(errno == EINVAL);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_read_at(l, (ledger89_offset)32, NULL, 1U) == -1);
    assert(errno == EINVAL);
    assert(ledger89_read_at(l, (ledger89_offset)-1, buf, sizeof(buf)) == -1);
    assert(errno == EINVAL);
    assert(ledger89_read_at(l, (ledger89_offset)0, buf, sizeof(buf)) == -1);
    assert(errno == EINVAL);
    assert(ledger89_read_at(l, (ledger89_offset)16, buf, sizeof(buf)) == -1);
    assert(errno == EINVAL);
    assert(ledger89_read_at(l, (ledger89_offset)31, buf, sizeof(buf)) == -1);
    assert(errno == EINVAL);
    /* The empty ledger's first frame header would start at byte 16, which is
     * the logical end. */
    assert(ledger89_read_at(l, (ledger89_offset)32, buf, sizeof(buf)) == -1);
    assert(errno == EINVAL);
    assert(ledger89_read_at(l, (ledger89_offset)1000, buf, sizeof(buf)) == -1);
    assert(errno == EINVAL);
    ledger89_close(l);
    test_unlink(path);
}

static void capacity_errors(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset offset;
    unsigned char buf[4];
    test_path(path, sizeof(path), "readat-cap", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "hello", 5U, &offset) == 0);
    memset(buf, 0x5a, sizeof(buf));
    assert(ledger89_read_at(l, offset, buf, sizeof(buf)) == -1);
    assert(errno == E2BIG);
    assert(buf[0] == 0x5aU && buf[1] == 0x5aU && buf[2] == 0x5aU &&
           buf[3] == 0x5aU);
    assert(ledger89_read_at(l, offset, NULL, 0U) == -1);
    assert(errno == E2BIG);
    ledger89_close(l);
    test_unlink(path);
}

static void corrupt_checksum(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    unsigned char buf[5];
    test_path(path, sizeof(path), "readat-crc", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_header(w, 5ULL, test_crc("hello", 5U) ^ 1ULL);
    test_append_bytes(w, "hello", 5U);
    append89_close(w);
    assert(ledger89_open_reader(&l, path) == 0);
    memset(buf, 0x5a, sizeof(buf));
    assert(ledger89_read_at(l, (ledger89_offset)32, buf, sizeof(buf)) == -1);
    assert(errno == EILSEQ);
    assert(buf[0] == 0x5aU && buf[1] == 0x5aU && buf[2] == 0x5aU &&
           buf[3] == 0x5aU && buf[4] == 0x5aU);
    ledger89_close(l);
    test_unlink(path);
}

static void corrupt_oversize(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    unsigned char buf[8];
    test_path(path, sizeof(path), "readat-big", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_header(w, (unsigned long long)APPEND89_RESERVE, 0ULL);
    append89_close(w);
    assert(ledger89_open_reader(&l, path) == 0);
    memset(buf, 0x5a, sizeof(buf));
    assert(ledger89_read_at(l, (ledger89_offset)32, buf, sizeof(buf)) == -1);
    assert(errno == EILSEQ);
    assert(buf[0] == 0x5aU);
    ledger89_close(l);
    test_unlink(path);
}

static void truncated_payload(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    unsigned char buf[5];
    test_path(path, sizeof(path), "readat-trunc", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_header(w, 5ULL, test_crc("hello", 5U));
    test_append_bytes(w, "he", 2U);
    append89_close(w);
    assert(ledger89_open_reader(&l, path) == 0);
    memset(buf, 0x5a, sizeof(buf));
    assert(ledger89_read_at(l, (ledger89_offset)32, buf, sizeof(buf)) == -1);
    assert(errno == EILSEQ);
    assert(buf[0] == 0x5aU && buf[1] == 0x5aU);
    ledger89_close(l);
    test_unlink(path);
}

static void partial_header(void)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    unsigned char buf[8];
    test_path(path, sizeof(path), "readat-partial", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    test_append_bytes(w, "1234567", 7U);
    append89_close(w);
    assert(ledger89_open_reader(&l, path) == 0);
    memset(buf, 0x5a, sizeof(buf));
    assert(ledger89_read_at(l, (ledger89_offset)32, buf, sizeof(buf)) == -1);
    assert(errno == EILSEQ);
    assert(buf[0] == 0x5aU);
    ledger89_close(l);
    test_unlink(path);
}

static void read_io_error(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset offset;
    unsigned char buf[8];
    test_path(path, sizeof(path), "readat-eio", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "hello", 5U, &offset) == 0);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    a89_fault_reset();
    a89_fault_errno("pread", 0, EIO);
    memset(buf, 0x5a, sizeof(buf));
    assert(ledger89_read_at(l, offset, buf, sizeof(buf)) == -1);
    assert(errno == EIO);
    assert(buf[0] == 0x5aU);
    a89_fault_reset();
    assert(ledger89_read_at(l, offset, buf, sizeof(buf)) == 5);
    assert(memcmp(buf, "hello", 5U) == 0);
    ledger89_close(l);
    test_unlink(path);
}

static void payload_io_error(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset offset;
    unsigned char buf[8];
    test_path(path, sizeof(path), "readat-eio-payload", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "hello", 5U, &offset) == 0);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    a89_fault_reset();
    a89_fault_errno("pread", 2, EIO);
    memset(buf, 0x5a, sizeof(buf));
    assert(ledger89_read_at(l, offset, buf, sizeof(buf)) == -1);
    assert(errno == EIO);
    assert(buf[0] == 0x5aU);
    a89_fault_reset();
    assert(ledger89_read_at(l, offset, buf, sizeof(buf)) == 5);
    assert(memcmp(buf, "hello", 5U) == 0);
    ledger89_close(l);
    test_unlink(path);
}

static void allocation_failure(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset offset;
    unsigned char buf[8];
    test_path(path, sizeof(path), "readat-nomem", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "hello", 5U, &offset) == 0);
    ledger89_close(l);
    assert(ledger89_open_reader(&l, path) == 0);
    a89_fault_reset();
    a89_fault_errno("malloc", 0, ENOMEM);
    memset(buf, 0x5a, sizeof(buf));
    assert(ledger89_read_at(l, offset, buf, sizeof(buf)) == -1);
    assert(errno == ENOMEM);
    assert(buf[0] == 0x5aU);
    a89_fault_reset();
    assert(ledger89_read_at(l, offset, buf, sizeof(buf)) == 5);
    assert(memcmp(buf, "hello", 5U) == 0);
    ledger89_close(l);
    test_unlink(path);
}

static void stale_errno_success(void)
{
    char path[160];
    ledger89 *l;
    ledger89_offset offset;
    unsigned char buf[1];
    test_path(path, sizeof(path), "readat-errno", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "Z", 1U, &offset) == 0);
    errno = EINVAL;
    assert(ledger89_read_at(l, offset, buf, sizeof(buf)) == 1);
    assert(buf[0] == 'Z');
    ledger89_close(l);
    test_unlink(path);
}

int main(void)
{
    read_basic();
    consecutive_empty();
    maximum_payload();
    argument_errors();
    capacity_errors();
    corrupt_checksum();
    corrupt_oversize();
    truncated_payload();
    partial_header();
    read_io_error();
    payload_io_error();
    allocation_failure();
    stale_errno_success();
    return 0;
}
