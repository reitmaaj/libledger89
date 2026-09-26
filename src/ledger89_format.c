/* ledger89_format.c - fixed 32-byte INDEX entry codec and CRC-64/NVME
 * checksums. Pure: no allocation and no I/O. */

#include "ledger89_priv.h"

static int ledger89_priv_header_ok(unsigned long long magic,
                                   unsigned int version, unsigned int flags)
{
    if (magic != LEDGER89_PRIV_MAGIC)
    {
        return 0;
    }
    if (version != LEDGER89_PRIV_VERSION)
    {
        return 0;
    }
    if (flags != LEDGER89_PRIV_FLAGS)
    {
        return 0;
    }
    return 1;
}

void ledger89_priv_entry_encode(unsigned char out[LEDGER89_PRIV_ENTRY_SIZE],
                                const struct ledger89_priv_entry *e)
{
    unsigned long long magic;
    magic = LEDGER89_PRIV_MAGIC;
    ledger89_u64_store_be(out, e->offset);
    ledger89_u64_store_be(out + 8, e->length);
    ledger89_u64_store_be(out + 16, e->checksum);
    out[24] = (unsigned char)((magic >> 24) & 0xffULL);
    out[25] = (unsigned char)((magic >> 16) & 0xffULL);
    out[26] = (unsigned char)((magic >> 8) & 0xffULL);
    out[27] = (unsigned char)(magic & 0xffULL);
    out[28] = (unsigned char)((LEDGER89_PRIV_VERSION >> 8) & 0xffU);
    out[29] = (unsigned char)(LEDGER89_PRIV_VERSION & 0xffU);
    out[30] = (unsigned char)((LEDGER89_PRIV_FLAGS >> 8) & 0xffU);
    out[31] = (unsigned char)(LEDGER89_PRIV_FLAGS & 0xffU);
}

void ledger89_priv_entry_decode(
    const unsigned char in[LEDGER89_PRIV_ENTRY_SIZE],
    struct ledger89_priv_entry *e, int *header_ok)
{
    unsigned long long magic;
    unsigned int version;
    unsigned int flags;
    e->offset = ledger89_u64_load_be(in);
    e->length = ledger89_u64_load_be(in + 8);
    e->checksum = ledger89_u64_load_be(in + 16);
    magic = ((unsigned long long)in[24] << 24) |
            ((unsigned long long)in[25] << 16) |
            ((unsigned long long)in[26] << 8) | (unsigned long long)in[27];
    version = ((unsigned int)in[28] << 8) | (unsigned int)in[29];
    flags = ((unsigned int)in[30] << 8) | (unsigned int)in[31];
    *header_ok = ledger89_priv_header_ok(magic, version, flags);
}

unsigned long long ledger89_priv_crc_data(const void *data, size_t size)
{
    cksum89_crc64_nvme_ctx ctx;
    cksum89_u64 value;
    cksum89_crc64_nvme_init(&ctx);
    cksum89_crc64_nvme_update(&ctx, data, size);
    value = cksum89_crc64_nvme_final(&ctx);
    return ((unsigned long long)value.hi << 32) | (unsigned long long)value.lo;
}
