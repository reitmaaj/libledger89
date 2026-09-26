/* Layer 6: bounded exhaustive crash model. Enumerate every stream cut for a
 * short append history and prove that recovery always yields a valid prefix
 * of the append history: never a torn record, never a record whose earlier
 * neighbor is missing, never fabricated bytes. */
#include "support.h"

#define MODEL_RECORDS 3

static const size_t model_sizes[MODEL_RECORDS] = {0U, 1U, 2U};
static const char *const model_items[MODEL_RECORDS] = {"", "X", "YZ"};
static off_t model_ends[MODEL_RECORDS];

static off_t model_end(void)
{
    off_t end;
    int i;
    end = (off_t)LEDGER89_TEST_PREAMBLE_SIZE;
    for (i = 0; i < MODEL_RECORDS; ++i)
    {
        end += (off_t)LEDGER89_TEST_HEADER_SIZE + (off_t)model_sizes[i];
        model_ends[i] = end;
    }
    return end;
}

/* Number of records fully committed when the stream is cut at cut. */
static int model_expected(off_t cut)
{
    int i;
    int count;
    count = 0;
    for (i = 0; i < MODEL_RECORDS; ++i)
    {
        if (model_ends[i] > cut)
        {
            break;
        }
        ++count;
    }
    return count;
}

static void check_cut(off_t cut)
{
    char path[160];
    ledger89 *l;
    int expected;
    int i;
    int fd;
    test_path(path, sizeof(path), "model", (int)cut);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    for (i = 0; i < MODEL_RECORDS; ++i)
    {
        assert(ledger89_append(l, model_items[i], model_sizes[i], NULL) == 0);
    }
    ledger89_close(l);
    fd = open(path, O_WRONLY);
    assert(fd >= 0);
    assert(ftruncate(fd, (off_t)APPEND89_RESERVE + cut) == 0);
    assert(close(fd) == 0);
    if (cut < (off_t)LEDGER89_TEST_PREAMBLE_SIZE)
    {
        assert(ledger89_open_writer(&l, path) == -1);
        assert(errno == EINVAL);
        test_unlink(path);
        return;
    }
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    expected = model_expected(cut);
    assert(test_count(l) == (unsigned long long)expected);
    for (i = 0; i < expected; ++i)
    {
        test_expect(l, (unsigned long long)i, model_items[i], model_sizes[i]);
    }
    ledger89_close(l);
    test_unlink(path);
}

int main(void)
{
    off_t full;
    off_t cut;
    full = model_end();
    for (cut = 0; cut <= full; ++cut)
    {
        check_cut(cut);
    }
    return 0;
}
