/* Layers 6/9: crash consistency. A writer is SIGKILLed at controlled points
 * and the stream is cut at every byte boundary; recovery must always yield a
 * valid ledger prefix, never a torn frame. */
#include "support.h"

static void kill_child(const char *path, const char *kind, int occurrence)
{
    char payload[20];
    ledger89 *l;
    pid_t child;
    memset(payload, 'A', sizeof(payload));
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        assert(ledger89_open_writer(&l, path) == 0);
        a89_fault_reset();
        if (strcmp(kind, "before") == 0)
        {
            a89_fault_kill_before("ftruncate", occurrence);
        }
        else
        {
            a89_fault_kill_after("ftruncate", occurrence);
        }
        (void)ledger89_append(l, payload, sizeof(payload), NULL);
        _exit(3);
    }
    test_wait(child, 1);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 1ULL);
    test_expect(l, 0ULL, "base", 4U);
    ledger89_close(l);
}

static void process_death(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "kill-before", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    kill_child(path, "before", 1);
    test_unlink(path);

    test_path(path, sizeof(path), "kill-after", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    {
        char payload[20];
        pid_t child;
        memset(payload, 'B', sizeof(payload));
        child = fork();
        assert(child >= 0);
        if (child == 0)
        {
            assert(ledger89_open_writer(&l, path) == 0);
            a89_fault_reset();
            a89_fault_kill_after("ftruncate", 1);
            (void)ledger89_append(l, payload, sizeof(payload), NULL);
            _exit(3);
        }
        test_wait(child, 1);
    }
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    /* Published before the kill: the whole frame survives. */
    assert(test_count(l) == 2ULL);
    test_expect(l, 0ULL, "base", 4U);
    ledger89_close(l);
    test_unlink(path);
}

/* Kill mid-candidate: the partially written frame stays in the reserve and is
 * never published. */
static void mid_candidate_death(void)
{
    char path[160];
    char payload[20];
    ledger89 *l;
    pid_t child;
    memset(payload, 'C', sizeof(payload));
    test_path(path, sizeof(path), "kill-mid", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        assert(ledger89_open_writer(&l, path) == 0);
        a89_fault_reset();
        /* Header (16) plus 5 payload bytes are written, then killed. */
        a89_fault_kill_after_write_bytes((off_t)(16 + 5));
        (void)ledger89_append(l, payload, sizeof(payload), NULL);
        _exit(3);
    }
    test_wait(child, 1);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(test_count(l) == 1ULL);
    test_expect(l, 0ULL, "base", 4U);
    ledger89_close(l);
    assert(test_logical_size(path) == (off_t)(16 + 16 + 4));
    test_unlink(path);
}

static void prefix_cuts(void)
{
    char path[160];
    char payload[20];
    ledger89 *l;
    off_t full;
    int cut;
    int fd;
    memset(payload, 'q', sizeof(payload));
    for (cut = 0; cut <= 72; ++cut)
    {
        test_path(path, sizeof(path), "prefix", cut);
        assert(ledger89_create(path, (mode_t)0600) == 0);
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_append(l, "base", 4U, NULL) == 0);
        assert(ledger89_append(l, payload, sizeof(payload), NULL) == 0);
        ledger89_close(l);
        full = test_logical_size(path);
        assert(full == 72);
        fd = open(path, O_WRONLY);
        assert(fd >= 0);
        assert(ftruncate(fd, (off_t)APPEND89_RESERVE + cut) == 0);
        assert(close(fd) == 0);
        if (cut < 16)
        {
            /* The preamble is torn; open fails without modifying anything. */
            assert(ledger89_open_writer(&l, path) == -1);
            assert(errno == EINVAL);
            test_unlink(path);
            continue;
        }
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_recover(l, NULL, NULL) == 0);
        if (cut >= 72)
        {
            assert(test_count(l) == 2ULL);
            test_expect(l, 0ULL, "base", 4U);
            test_expect(l, 1ULL, payload, sizeof(payload));
        }
        else if (cut >= 36)
        {
            assert(test_count(l) == 1ULL);
            test_expect(l, 0ULL, "base", 4U);
        }
        else
        {
            assert(test_count(l) == 0ULL);
        }
        ledger89_close(l);
        test_unlink(path);
    }
}

static void repeated_kill_cycles(void)
{
    char path[160];
    ledger89 *l;
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
            a89_fault_reset();
            /* Alternate: killed before or after publication. */
            if (i % 2 == 0)
            {
                a89_fault_kill_before("ftruncate", 1);
            }
            else
            {
                a89_fault_kill_after("ftruncate", 1);
            }
            (void)ledger89_append(l, "XX", 2U, NULL);
            _exit(3);
        }
        test_wait(child, 1);
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_recover(l, NULL, NULL) == 0);
        assert(test_count(l) >= (unsigned long long)(i + 1));
        ledger89_close(l);
    }
    test_unlink(path);
}

int main(void)
{
    process_death();
    mid_candidate_death();
    prefix_cuts();
    repeated_kill_cycles();
    return 0;
}
