/* Layer 8: multiprocess writer serialization and total-order verification. */
#include "support.h"

int main(void)
{
    char path[160];
    char payload[83];
    ledger89 *l;
    ledger89_iter *it;
    pid_t children[4];
    int seen[4][12];
    unsigned long long count;
    ledger89_offset previous;
    int i;
    int j;
    int id;
    int sequence;
    test_path(path, sizeof(path), "concurrent", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < 4; ++i)
    {
        children[i] = fork();
        assert(children[i] >= 0);
        if (children[i] == 0)
        {
            assert(ledger89_open_writer(&l, path) == 0);
            for (j = 0; j < 12; ++j)
            {
                memset(payload, 'A' + i, sizeof(payload));
                payload[1] = (char)('a' + j);
                assert(ledger89_append(l, payload, sizeof(payload), NULL) == 0);
            }
            ledger89_close(l);
            _exit(0);
        }
    }
    for (i = 0; i < 4; ++i)
    {
        test_wait(children[i], 0);
    }
    assert(ledger89_open_reader(&l, path) == 0);
    assert(test_count(l) == 48ULL);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    previous = -1;
    count = 0ULL;
    while (ledger89_iter_next(it) == LEDGER89_OK)
    {
        unsigned char buf[83];
        size_t done;
        ssize_t n;
        assert(ledger89_iter_offset(it) > previous);
        previous = ledger89_iter_offset(it);
        assert(ledger89_iter_length(it) == sizeof(payload));
        done = 0U;
        while ((n = ledger89_iter_read(it, buf + done, sizeof(buf) - done)) > 0)
        {
            done += (size_t)n;
        }
        assert(n == 0 && done == sizeof(payload));
        id = buf[0] - 'A';
        sequence = buf[1] - 'a';
        assert(id >= 0 && id < 4 && sequence >= 0 && sequence < 12);
        assert(seen[id][sequence] == 0);
        seen[id][sequence] = 1;
        for (i = 2; i < (int)sizeof(payload); ++i)
        {
            assert(buf[i] == 'A' + id);
        }
        ++count;
    }
    assert(count == 48ULL);
    ledger89_iter_close(it);
    ledger89_close(l);
    test_unlink(path);
    return 0;
}
