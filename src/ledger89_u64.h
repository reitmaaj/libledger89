#ifndef LEDGER89_U64_H
#define LEDGER89_U64_H

/* Native unsigned long long scalars. libll89's two-limb ll89_u64 abstraction
 * is gone; these two helpers are all the native type still needs here: the
 * canonical 8-byte big-endian load and a checked conversion to size_t. */

#include <stddef.h>

static unsigned long long ledger89_u64_load_be(const unsigned char in[8])
{
    unsigned long long value;
    int i;
    value = 0ULL;
    for (i = 0; i < 8; ++i)
    {
        value = (value << 8) | (unsigned long long)in[i];
    }
    return value;
}

/* 0 on success; -1 when value exceeds size_t. */
static int ledger89_u64_to_size(unsigned long long value, size_t *out)
{
    if (value > (unsigned long long)(size_t)-1)
    {
        return -1;
    }
    *out = (size_t)value;
    return 0;
}

#endif /* LEDGER89_U64_H */
