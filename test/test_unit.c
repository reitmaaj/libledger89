/* Layer 1: deterministic unit tests for the INDEX codec, big-endian scalars,
 * and offset arithmetic. Includes the private header to test pure functions
 * directly. */
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "ledger89_priv.h"

static void codec_roundtrip(void)
{
    static const struct
    {
        unsigned long long offset;
        unsigned long long length;
        unsigned long long checksum;
    } cases[] = {
        {0ULL, 0ULL, 0ULL},
        {0x0102030405060708ULL, 0x1112131415161718ULL, 0x2122232425262728ULL},
        {0xffffffffffffffffULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL},
        {1ULL, 2ULL, 3ULL},
    };
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    struct ledger89_priv_entry e;
    int header_ok;
    size_t i;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        e.offset = cases[i].offset;
        e.length = cases[i].length;
        e.checksum = cases[i].checksum;
        ledger89_priv_entry_encode(raw, &e);
        ledger89_priv_entry_decode(raw, &e, &header_ok);
        assert(header_ok == 1);
        assert(e.offset == cases[i].offset);
        assert(e.length == cases[i].length);
        assert(e.checksum == cases[i].checksum);
    }
}

static void codec_exact_bytes(void)
{
    static const unsigned char expected[LEDGER89_PRIV_ENTRY_SIZE] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
        0x4c, 0x44, 0x38, 0x39, 0x00, 0x01, 0x00, 0x00};
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    struct ledger89_priv_entry e;
    e.offset = 0x0102030405060708ULL;
    e.length = 0x1112131415161718ULL;
    e.checksum = 0x2122232425262728ULL;
    ledger89_priv_entry_encode(raw, &e);
    assert(memcmp(raw, expected, LEDGER89_PRIV_ENTRY_SIZE) == 0);
}

static void codec_header_rejection(void)
{
    unsigned char raw[LEDGER89_PRIV_ENTRY_SIZE];
    struct ledger89_priv_entry e;
    int header_ok;
    memset(raw, 0, sizeof(raw));
    /* A valid entry, then corrupt each header field in turn. */
    e.offset = 0ULL;
    e.length = 0ULL;
    e.checksum = 0ULL;
    ledger89_priv_entry_encode(raw, &e);
    ledger89_priv_entry_decode(raw, &e, &header_ok);
    assert(header_ok == 1);

    raw[24] ^= 0x01U; /* magic */
    ledger89_priv_entry_decode(raw, &e, &header_ok);
    assert(header_ok == 0);
    raw[24] ^= 0x01U;

    raw[29] = 2U; /* version */
    ledger89_priv_entry_decode(raw, &e, &header_ok);
    assert(header_ok == 0);
    raw[29] = 1U;

    raw[31] = 1U; /* flags */
    ledger89_priv_entry_decode(raw, &e, &header_ok);
    assert(header_ok == 0);
}

static void u64_roundtrip(void)
{
    unsigned char raw[8];
    static const unsigned long long values[] = {
        0ULL, 1ULL, 0xffULL, 0xffffffffffffffffULL, 0x0102030405060708ULL};
    size_t i;
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        ledger89_u64_store_be(raw, values[i]);
        assert(ledger89_u64_load_be(raw) == values[i]);
    }
}

static void off_conversion(void)
{
    off_t out;
    assert(ledger89_u64_to_off(0ULL, &out) == 0 && out == 0);
    assert(ledger89_u64_to_off(ledger89_u64_off_max(), &out) == 0);
    assert(out == (off_t)ledger89_u64_off_max());
    assert(ledger89_u64_to_off(ledger89_u64_off_max() + 1ULL, &out) == -1);
    assert(ledger89_u64_from_off(0) == 0ULL);
    assert(ledger89_u64_from_off(123) == 123ULL);
}

static void crc_deterministic(void)
{
    unsigned long long crc;
    cksum89_u64 value;
    crc = ledger89_priv_crc_data("123456789", 9U);
    value = cksum89_crc64_nvme("123456789", 9U);
    assert(crc == (((unsigned long long)value.hi << 32) |
                   (unsigned long long)value.lo));
    assert(ledger89_priv_crc_data(NULL, 0U) ==
           ledger89_priv_crc_data(NULL, 0U));
}

int main(void)
{
    codec_roundtrip();
    codec_exact_bytes();
    codec_header_rejection();
    u64_roundtrip();
    off_conversion();
    crc_deterministic();
    return 0;
}
