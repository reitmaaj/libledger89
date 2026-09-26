/* Layer 1: deterministic unit tests for the preamble and frame header codecs,
 * big-endian scalars, and offset arithmetic. Includes the private header to
 * test pure functions directly. */
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "ledger89_priv.h"

static void preamble_roundtrip(void)
{
    unsigned char raw[LEDGER89_PRIV_PREAMBLE_SIZE];
    unsigned long long reserve;
    int ok;
    ledger89_priv_preamble_encode(raw, (unsigned long long)APPEND89_RESERVE);
    ledger89_priv_preamble_decode(raw, &reserve, &ok);
    assert(ok == 1);
    assert(reserve == (unsigned long long)APPEND89_RESERVE);
}

static void preamble_exact_bytes(void)
{
    static const unsigned char expected[LEDGER89_PRIV_PREAMBLE_SIZE] = {
        0x4c, 0x45, 0x44, 0x47, 0x38, 0x39, 0x53, 0x31,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00};
    unsigned char raw[LEDGER89_PRIV_PREAMBLE_SIZE];
    ledger89_priv_preamble_encode(raw, (unsigned long long)APPEND89_RESERVE);
    assert(memcmp(raw, expected, LEDGER89_PRIV_PREAMBLE_SIZE) == 0);
}

static void preamble_magic_rejection(void)
{
    unsigned char raw[LEDGER89_PRIV_PREAMBLE_SIZE];
    unsigned long long reserve;
    int ok;
    ledger89_priv_preamble_encode(raw, (unsigned long long)APPEND89_RESERVE);
    raw[0] ^= 0x01U;
    ledger89_priv_preamble_decode(raw, &reserve, &ok);
    assert(ok == 0);
}

static void header_roundtrip(void)
{
    static const struct
    {
        unsigned long long size;
        unsigned long long checksum;
    } cases[] = {
        {0ULL, 0ULL},
        {0x0102030405060708ULL, 0x1112131415161718ULL},
        {0xffffffffffffffffULL, 0xffffffffffffffffULL},
        {1ULL, 2ULL},
    };
    unsigned char raw[LEDGER89_PRIV_HEADER_SIZE];
    struct ledger89_priv_header h;
    size_t i;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        h.size = cases[i].size;
        h.checksum = cases[i].checksum;
        ledger89_priv_header_encode(raw, &h);
        ledger89_priv_header_decode(raw, &h);
        assert(h.size == cases[i].size);
        assert(h.checksum == cases[i].checksum);
    }
}

static void header_exact_bytes(void)
{
    static const unsigned char expected[LEDGER89_PRIV_HEADER_SIZE] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
    unsigned char raw[LEDGER89_PRIV_HEADER_SIZE];
    struct ledger89_priv_header h;
    h.size = 0x0102030405060708ULL;
    h.checksum = 0x1112131415161718ULL;
    ledger89_priv_header_encode(raw, &h);
    assert(memcmp(raw, expected, LEDGER89_PRIV_HEADER_SIZE) == 0);
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
    checksum89_u64 value;
    crc = ledger89_priv_crc_data("123456789", 9U);
    value = checksum89_crc64_nvme("123456789", 9U);
    assert(crc == (((unsigned long long)value.hi << 32) |
                   (unsigned long long)value.lo));
    assert(ledger89_priv_crc_data(NULL, 0U) ==
           ledger89_priv_crc_data(NULL, 0U));
}

int main(void)
{
    preamble_roundtrip();
    preamble_exact_bytes();
    preamble_magic_rejection();
    header_roundtrip();
    header_exact_bytes();
    u64_roundtrip();
    off_conversion();
    crc_deterministic();
    return 0;
}
