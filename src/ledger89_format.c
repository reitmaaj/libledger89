/* ledger89_format.c - 16-byte preamble and record frame header codecs plus
 * CRC-64/NVME checksums. Pure: no allocation and no I/O. */

#include "ledger89_priv.h"

void ledger89_priv_preamble_encode(
    unsigned char out[LEDGER89_PRIV_PREAMBLE_SIZE], unsigned long long reserve)
{
    ledger89_u64_store_be(out, LEDGER89_PRIV_MAGIC);
    ledger89_u64_store_be(out + 8, reserve);
}

void ledger89_priv_preamble_decode(
    const unsigned char in[LEDGER89_PRIV_PREAMBLE_SIZE],
    unsigned long long *reserve, int *ok)
{
    unsigned long long magic;

    magic = ledger89_u64_load_be(in);
    *reserve = ledger89_u64_load_be(in + 8);
    *ok = 1;
    if (magic != LEDGER89_PRIV_MAGIC)
    {
        *ok = 0;
    }
}

void ledger89_priv_header_encode(unsigned char out[LEDGER89_PRIV_HEADER_SIZE],
                                 const struct ledger89_priv_header *h)
{
    ledger89_u64_store_be(out, h->size);
    ledger89_u64_store_be(out + 8, h->checksum);
}

void ledger89_priv_header_decode(
    const unsigned char in[LEDGER89_PRIV_HEADER_SIZE],
    struct ledger89_priv_header *h)
{
    h->size = ledger89_u64_load_be(in);
    h->checksum = ledger89_u64_load_be(in + 8);
}

unsigned long long ledger89_priv_crc_data(const void *data, size_t size)
{
    checksum89_crc64_nvme_ctx ctx;
    checksum89_u64 value;

    checksum89_crc64_nvme_init(&ctx);
    checksum89_crc64_nvme_update(&ctx, data, size);
    value = checksum89_crc64_nvme_final(&ctx);
    return ((unsigned long long)value.hi << 32) | (unsigned long long)value.lo;
}
