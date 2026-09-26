/* Layer 7: property-based / generative model testing. A deterministic PRNG
 * drives append/reopen/recover operations against an in-memory reference
 * model; every stable state is compared to the model. */
#include "support.h"

#define PROP_MAX_RECORDS 512
#define PROP_MAX_SIZE 64
#define PROP_OPS 400
#define PROP_SEEDS 25

static unsigned long long rng_state;

static unsigned long long rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static size_t random_size(void)
{
    unsigned long long r;
    r = rng() % 8ULL;
    switch (r)
    {
    case 0:
        return 0U;
    case 1:
        return 1U;
    case 2:
        return 2U;
    case 3:
        return 3U;
    default:
        return (size_t)(rng() % PROP_MAX_SIZE);
    }
}

static void fill(unsigned char *buf, size_t size, unsigned long long tag)
{
    size_t i;
    for (i = 0U; i < size; ++i)
    {
        buf[i] = (unsigned char)(tag + i * 31U);
    }
}

static void model_check(ledger89 *l, unsigned char model[PROP_MAX_RECORDS]
                                                    [PROP_MAX_SIZE],
                        const size_t *sizes, size_t count)
{
    unsigned long long n;
    size_t i;
    assert(ledger89_count(l, &n) == 0);
    assert(n == (unsigned long long)count);
    for (i = 0U; i < count; ++i)
    {
        test_expect(l, (unsigned long long)i, model[i], sizes[i]);
    }
}

int main(void)
{
    char path[160];
    ledger89 *l;
    unsigned char model[PROP_MAX_RECORDS][PROP_MAX_SIZE];
    size_t sizes[PROP_MAX_RECORDS];
    size_t count;
    unsigned char payload[PROP_MAX_SIZE];
    int seed;
    int op;
    for (seed = 1; seed <= PROP_SEEDS; ++seed)
    {
        rng_state = (unsigned long long)seed * 0x9e3779b97f4a7c15ULL + 1ULL;
        test_path(path, sizeof(path), "prop", seed);
        assert(ledger89_create(path, (mode_t)0600) == 0);
        assert(ledger89_open_writer(&l, path) == 0);
        count = 0U;
        for (op = 0; op < PROP_OPS; ++op)
        {
            unsigned long long choice;
            choice = rng() % 10ULL;
            if (choice < 6U && count < PROP_MAX_RECORDS)
            {
                size_t size;
                size = random_size();
                if (size > PROP_MAX_SIZE)
                {
                    size = PROP_MAX_SIZE;
                }
                fill(payload, size, (unsigned long long)op);
                assert(ledger89_append(l, size == 0U ? NULL : payload, size,
                                       NULL) == 0);
                memcpy(model[count], payload, size);
                sizes[count] = size;
                ++count;
                model_check(l, model, sizes, count);
            }
            else if (choice == 6U)
            {
                /* Close and reopen as a reader, verify, then reopen writer. */
                ledger89_close(l);
                assert(ledger89_open_reader(&l, path) == 0);
                model_check(l, model, sizes, count);
                ledger89_close(l);
                assert(ledger89_open_writer(&l, path) == 0);
            }
            else if (choice == 7U)
            {
                assert(ledger89_recover(l, NULL, NULL) == 0);
                model_check(l, model, sizes, count);
            }
            else
            {
                model_check(l, model, sizes, count);
            }
        }
        ledger89_close(l);
        /* Final reopen verifies durability across the whole sequence. */
        assert(ledger89_open_reader(&l, path) == 0);
        model_check(l, model, sizes, count);
        ledger89_close(l);
        test_unlink(path);
    }
    return 0;
}
