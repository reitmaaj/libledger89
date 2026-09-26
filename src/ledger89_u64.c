/* ledger89_u64.c - native unsigned long long big-endian codec and checked
 * conversions. */

#include <limits.h>

#include "ledger89_u64.h"

static unsigned long long ledger89_u64_push_byte(unsigned long long value,
                                                 unsigned char byte)
{
    return (value << 8) | (unsigned long long)byte;
}

static unsigned char ledger89_u64_shift_out(unsigned long long *value)
{
    unsigned char byte;

    byte = (unsigned char)(*value & 0xffULL);
    *value >>= 8;
    return byte;
}

unsigned long long ledger89_u64_load_be(const unsigned char in[8])
{
    unsigned long long value;
    int i;

    value = 0ULL;
    for (i = 0; i < 8; ++i)
    {
        value = ledger89_u64_push_byte(value, in[i]);
    }
    return value;
}

void ledger89_u64_store_be(unsigned char out[8], unsigned long long value)
{
    int i;

    for (i = 7; i >= 0; --i)
    {
        out[i] = ledger89_u64_shift_out(&value);
    }
}

unsigned long long ledger89_u64_from_off(off_t value)
{
    return (unsigned long long)value;
}

unsigned long long ledger89_u64_off_max(void)
{
    size_t bits;

    bits = sizeof(off_t) * CHAR_BIT;
    if (bits > 63U)
    {
        bits = 63U;
    }
    return (1ULL << (bits - 1U)) - 1ULL;
}

int ledger89_u64_to_off(unsigned long long value, off_t *out)
{
    if (value > ledger89_u64_off_max())
    {
        return -1;
    }
    *out = (off_t)value;
    return 0;
}
