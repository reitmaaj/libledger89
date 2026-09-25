#include "support.h"

int main(void)
{
    char path[160];
    char payload[83];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    ledger89_offset previous;
    pid_t children[4];
    int seen[4][12];
    int i;
    int j;
    int id;
    int sequence;
    int count;
    size_t done;
    ssize_t n;
    test_path(path, sizeof(path), "concurrent", 0);
    assert(ledger89_create(path, (mode_t)0600, 40U) == 0);
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < 4; ++i)
    {
        children[i] = fork();
        assert(children[i] >= 0);
        if (children[i] == 0)
        {
            assert(ledger89_open_writer(&a, path, 40U) == 0);
            for (j = 0; j < 12; ++j)
            {
                memset(payload, 'A' + i, sizeof(payload));
                payload[1] = (char)('a' + j);
                assert(ledger89_append(a, payload, sizeof(payload), NULL) == 0);
            }
            ledger89_close(a);
            _exit(0);
        }
    }
    for (i = 0; i < 4; ++i)
    {
        test_wait(children[i], 0);
    }
    assert(ledger89_open_reader(&a, path, 40U) == 0);
    cursor = LEDGER89_BEGIN;
    previous = 0;
    count = 0;
    while (ledger89_next(a, &cursor, &m) == LEDGER89_OK)
    {
        assert(ledger89_message_offset(m) > previous);
        previous = ledger89_message_offset(m);
        done = 0U;
        while (done < sizeof(payload))
        {
            n = ledger89_message_read(m, payload + done, sizeof(payload) - done);
            assert(n > 0);
            done += (size_t)n;
        }
        id = payload[0] - 'A';
        sequence = payload[1] - 'a';
        assert(id >= 0 && id < 4 && sequence >= 0 && sequence < 12);
        assert(seen[id][sequence] == 0);
        seen[id][sequence] = 1;
        for (i = 2; i < (int)sizeof(payload); ++i)
        {
            assert(payload[i] == 'A' + id);
        }
        ledger89_message_close(m);
        ++count;
    }
    assert(count == 48);
    ledger89_close(a);
    assert(unlink(path) == 0);
    return 0;
}
