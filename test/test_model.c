/* Layer 6: bounded exhaustive crash model. Enumerate every (DATA cut, INDEX
 * cut) pair for a short append history and prove that recovery always yields a
 * valid prefix of the append history: never a torn record, never a record
 * whose earlier neighbor is missing, never fabricated bytes. */
#include "support.h"

#define MODEL_RECORDS 3

static const size_t model_sizes[MODEL_RECORDS] = {0U, 1U, 2U};
static const char *const model_items[MODEL_RECORDS] = {"", "X", "YZ"};
static off_t model_offsets[MODEL_RECORDS];

static off_t model_data_end(void)
{
    off_t end;
    int i;
    end = 0;
    for (i = 0; i < MODEL_RECORDS; ++i)
    {
        end += (off_t)model_sizes[i];
    }
    return end;
}

static off_t model_index_end(void)
{
    return (off_t)(MODEL_RECORDS * LEDGER89_TEST_ENTRY_SIZE);
}

/* Number of records fully committed when DATA is cut at data_cut and INDEX is
 * cut at index_cut. */
static int model_expected(off_t data_cut, off_t index_cut)
{
    int i;
    int count;
    count = 0;
    for (i = 0; i < MODEL_RECORDS; ++i)
    {
        if ((off_t)((size_t)(i + 1) * LEDGER89_TEST_ENTRY_SIZE) > index_cut)
        {
            break;
        }
        if (model_offsets[i] + (off_t)model_sizes[i] > data_cut)
        {
            break;
        }
        ++count;
    }
    return count;
}

static void check_cut(off_t data_cut, off_t index_cut)
{
    char path[160];
    char data[160];
    char index[160];
    ledger89 *l;
    unsigned long long count;
    int expected;
    int i;
    int fd;
    test_path(path, sizeof(path), "model", (int)(data_cut * 100 + index_cut));
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    for (i = 0; i < MODEL_RECORDS; ++i)
    {
        assert(ledger89_append(l, model_items[i], model_sizes[i], NULL) == 0);
    }
    ledger89_close(l);
    (void)snprintf(data, sizeof(data), "%s.data", path);
    (void)snprintf(index, sizeof(index), "%s.index", path);
    fd = open(data, O_WRONLY);
    assert(fd >= 0);
    assert(ftruncate(fd, (off_t)APPEND89_RESERVE + data_cut) == 0);
    assert(close(fd) == 0);
    fd = open(index, O_WRONLY);
    assert(fd >= 0);
    assert(ftruncate(fd, (off_t)LEDGER89_TEST_INDEX_RESERVE + index_cut) == 0);
    assert(close(fd) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0);
    expected = model_expected(data_cut, index_cut);
    assert(count == (unsigned long long)expected);
    for (i = 0; i < expected; ++i)
    {
        test_expect(l, (unsigned long long)i, model_items[i], model_sizes[i]);
    }
    ledger89_close(l);
    test_unlink(path);
}

int main(void)
{
    off_t dmax;
    off_t imax;
    off_t dc;
    off_t ic;
    int i;
    off_t end;
    end = 0;
    for (i = 0; i < MODEL_RECORDS; ++i)
    {
        model_offsets[i] = end;
        end += (off_t)model_sizes[i];
    }
    dmax = model_data_end();
    imax = model_index_end();
    for (dc = 0; dc <= dmax; ++dc)
    {
        for (ic = 0; ic <= imax; ++ic)
        {
            check_cut(dc, ic);
        }
    }
    return 0;
}
