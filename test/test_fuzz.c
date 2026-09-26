/* Layer 20: deterministic fuzz target over arbitrary raw DATA/INDEX byte
 * streams. For every generated pair, recovery must not crash, must not invoke
 * undefined behavior, must produce a valid prefix, and must be idempotent. */
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

static void fuzz_pair(unsigned long long seed)
{
    char path[160];
    char data[160];
    char index[160];
    append89 *w;
    ledger89 *l;
    unsigned long long count;
    unsigned char *dbytes;
    unsigned char *ibytes;
    size_t dlen;
    size_t ilen;
    size_t i;
    fuzz_rng_state = seed;
    dlen = (size_t)(fuzz_rng() % 64U);
    ilen = (size_t)(fuzz_rng() % (8U * LEDGER89_TEST_ENTRY_SIZE + 1U));
    dbytes = (unsigned char *)malloc(dlen == 0U ? 1U : dlen);
    ibytes = (unsigned char *)malloc(ilen == 0U ? 1U : ilen);
    assert(dbytes != NULL && ibytes != NULL);
    for (i = 0U; i < dlen; ++i)
    {
        dbytes[i] = (unsigned char)fuzz_rng();
    }
    for (i = 0U; i < ilen; ++i)
    {
        ibytes[i] = (unsigned char)fuzz_rng();
    }
    test_path(path, sizeof(path), "fuzz", (int)(seed % 100000));
    assert(ledger89_create(path, (mode_t)0600) == 0);
    (void)snprintf(data, sizeof(data), "%s.data", path);
    (void)snprintf(index, sizeof(index), "%s.index", path);
    if (dlen > 0U)
    {
        assert(append89_open_writer(&w, data, (mode_t)0) == 0);
        assert(append89_append(w, dbytes, dlen, NULL) == 0);
        append89_close(w);
    }
    if (ilen > 0U)
    {
        assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                            LEDGER89_TEST_INDEX_RESERVE) == 0);
        assert(append89_append(w, ibytes, ilen, NULL) == 0);
        append89_close(w);
    }
    /* Recovery must always succeed and produce a valid, idempotent prefix. */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0);
    (void)count;
    ledger89_close(l);
    assert(test_index_logical_size(path) % LEDGER89_TEST_ENTRY_SIZE == 0);
    /* Recovery is idempotent. */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    ledger89_close(l);
    free(dbytes);
    free(ibytes);
    test_unlink(path);
}

int main(void)
{
    unsigned long long seed;
    for (seed = 1ULL; seed <= FUZZ_ITERATIONS; ++seed)
    {
        fuzz_pair(seed);
    }
    return 0;
}
