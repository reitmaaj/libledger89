#ifndef LEDGER89_U64_H
#define LEDGER89_U64_H

/* Native unsigned long long scalars and the canonical 8-byte big-endian codec.
 * The library requires CHAR_BIT == 8 and sizeof(off_t) <= 8, enforced at open
 * time. */

#include <sys/types.h>

unsigned long long ledger89_u64_load_be(const unsigned char in[8]);
void ledger89_u64_store_be(unsigned char out[8], unsigned long long value);
unsigned long long ledger89_u64_from_off(off_t value);
unsigned long long ledger89_u64_off_max(void);

/* 0 on success; -1 when value exceeds the positive off_t range. */
int ledger89_u64_to_off(unsigned long long value, off_t *out);

#endif /* LEDGER89_U64_H */
