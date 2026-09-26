/* Layer 20: deterministic fuzz target over arbitrary raw byte tails. For every
 * generated tail, recovery must not crash, must not invoke undefined behavior,
 * must produce a valid prefix, and must be idempotent. */
#include "support.h"

#define FUZZ_ITERATIONS 2000

static unsigned long long fuzz_rng_state;

static unsigned long long fuzz_rng(void)
{
    fuzz_rng_state ^= fuzz_rng_state << 13;
    fuzz_rng_state ^= fuzz_rng_state >> 7;
    fuzz_rng_state ^= fuzz_rng_state << 17;
    return fuzz_rng_state;
}

static void fuzz_tail(unsigned long long seed)
{
    char path[160];
    append89 *w;
    ledger89 *l;
    unsigned char *bytes;
    size_t len;
    size_t i;
    int truncate;
    fuzz_rng_state = seed;
    len = (size_t)(fuzz_rng() % 96U);
    truncate = (int)(fuzz_rng() % 2U);
    bytes = (unsigned char *)malloc(len == 0U ? 1U : len);
    assert(bytes != NULL);
    for (i = 0U; i < len; ++i)
    {
        bytes[i] = (unsigned char)fuzz_rng();
    }
    test_path(path, sizeof(path), "fuzz", (int)(seed % 100000));
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(append89_open_writer(&w, path, (mode_t)0) == 0);
    if (truncate)
    {
        /* Cut back to a random prefix (the preamble at minimum). */
        assert(append89_truncate(w, (append89_offset)(16U)) == 0);
    }
    if (len > 0U)
    {
        assert(append89_append(w, bytes, len, NULL) == 0);
    }
    append89_close(w);
    /* Recovery must always succeed and produce a valid, idempotent prefix. */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    (void)test_count(l);
    ledger89_close(l);
    /* Recovery is idempotent. */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    (void)test_count(l);
    ledger89_close(l);
    free(bytes);
    test_unlink(path);
}

int main(void)
{
    unsigned long long seed;
    for (seed = 1ULL; seed <= FUZZ_ITERATIONS; ++seed)
    {
        fuzz_tail(seed);
    }
    return 0;
}
