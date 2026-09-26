/* Layers 6/9: crash consistency. A writer is SIGKILLed at controlled points
 * and the DATA stream is cut at every byte boundary; recovery must always
 * yield a valid ledger prefix. */
#include "support.h"

static void process_death(int boundary)
{
    char path[160];
    char payload[20];
    ledger89 *l;
    unsigned long long count;
    pid_t child;
    test_path(path, sizeof(path), "kill", boundary);
    memset(payload, 'A', sizeof(payload));
    assert(ledger89_create(path, (mode_t)0600) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_append(l, "base", 4U, NULL) == 0);
        a89_fault_reset();
        a89_fault_kill_after("ftruncate", boundary);
        (void)ledger89_append(l, payload, sizeof(payload), NULL);
        _exit(3);
    }
    test_wait(child, 1);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0);
    test_expect(l, 0ULL, "base", 4U);
    if (boundary >= 2)
    {
        assert(count == 2ULL);
        test_expect(l, 1ULL, payload, sizeof(payload));
    }
    else
    {
        assert(count == 1ULL);
    }
    ledger89_close(l);
    test_unlink(path);
}

static void prefix_cuts(void)
{
    char path[160];
    char data[160];
    char payload[20];
    ledger89 *l;
    unsigned long long count;
    off_t full;
    int cut;
    int fd;
    memset(payload, 'q', sizeof(payload));
    for (cut = 0; cut <= 24; ++cut)
    {
        test_path(path, sizeof(path), "prefix", cut);
        assert(ledger89_create(path, (mode_t)0600) == 0);
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_append(l, "base", 4U, NULL) == 0);
        assert(ledger89_append(l, payload, sizeof(payload), NULL) == 0);
        ledger89_close(l);
        full = test_data_logical_size(path);
        assert(full == 24);
        (void)snprintf(data, sizeof(data), "%s.data", path);
        fd = open(data, O_WRONLY);
        assert(fd >= 0);
        assert(ftruncate(fd, (off_t)APPEND89_RESERVE + cut) == 0);
        assert(close(fd) == 0);
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_recover(l, NULL, NULL) == 0);
        assert(ledger89_count(l, &count) == 0);
        if (cut >= 24)
        {
            assert(count == 2ULL);
            test_expect(l, 0ULL, "base", 4U);
            test_expect(l, 1ULL, payload, sizeof(payload));
        }
        else if (cut >= 4)
        {
            assert(count == 1ULL);
            test_expect(l, 0ULL, "base", 4U);
        }
        else
        {
            assert(count == 0ULL);
        }
        ledger89_close(l);
        test_unlink(path);
    }
}

static void repeated_kill_cycles(void)
{
    char path[160];
    ledger89 *l;
    unsigned long long count;
    pid_t child;
    int i;
    test_path(path, sizeof(path), "kill-cycles", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    for (i = 0; i < 10; ++i)
    {
        child = fork();
        assert(child >= 0);
        if (child == 0)
        {
            char p[8];
            assert(ledger89_open_writer(&l, path) == 0);
            (void)snprintf(p, sizeof(p), "r%d", i);
            (void)ledger89_append(l, p, 2U, NULL);
            /* Kill at a varying point each cycle. */
            a89_fault_reset();
            a89_fault_kill_after("ftruncate", 1 + (i % 2));
            (void)ledger89_append(l, "XX", 2U, NULL);
            _exit(3);
        }
        test_wait(child, 1);
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_recover(l, NULL, NULL) == 0);
        assert(ledger89_count(l, &count) == 0);
        ledger89_close(l);
    }
    test_unlink(path);
}

int main(void)
{
    process_death(1);
    process_death(2);
    prefix_cuts();
    repeated_kill_cycles();
    return 0;
}
