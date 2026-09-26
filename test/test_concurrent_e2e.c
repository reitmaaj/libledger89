/* Layer 8 (end-to-end): multiple writer processes append with per-writer,
 * per-record varying delays while several reader processes run simultaneously,
 * re-validating the committed prefix on every poll. The parent verifies the
 * final total order. */
#include <time.h>

#include "support.h"

#define WRITERS 4
#define RECORDS 20
#define READERS 3
#define PAYLOAD 96
#define TOTAL (WRITERS * RECORDS)
#define MAX_POLLS 20000

/* Validate the record an iterator is positioned on: fixed length, full
 * checksum-verified payload, and a decodable writer id / sequence with a
 * consistent fill pattern. */
static void validate_record(ledger89_iter *it, int *wid, int *seq)
{
    unsigned char buf[PAYLOAD];
    size_t done;
    ssize_t n;
    int k;
    assert(ledger89_iter_length(it) == (unsigned long long)PAYLOAD);
    done = 0U;
    while ((n = ledger89_iter_read(it, buf + done, (size_t)PAYLOAD - done)) > 0)
    {
        done += (size_t)n;
    }
    assert(n == 0 && done == (size_t)PAYLOAD);
    *wid = buf[0] - 'A';
    *seq = buf[1] - 'a';
    assert(*wid >= 0 && *wid < WRITERS);
    assert(*seq >= 0 && *seq < RECORDS);
    for (k = 2; k < PAYLOAD; ++k)
    {
        assert(buf[k] == 'A' + *wid);
    }
}

static void writer_run(const char *path, int wid)
{
    ledger89 *l;
    char payload[PAYLOAD];
    int seq;
    assert(ledger89_open_writer(&l, path) == 0);
    for (seq = 0; seq < RECORDS; ++seq)
    {
        struct timespec d;
        long us;
        memset(payload, 'A' + wid, sizeof(payload));
        payload[0] = (char)('A' + wid);
        payload[1] = (char)('a' + seq);
        us = 1000L + (long)(((unsigned long)wid * 523UL +
                             (unsigned long)seq * 97UL) % 2500UL);
        d.tv_sec = 0;
        d.tv_nsec = us * 1000L;
        (void)nanosleep(&d, NULL);
        assert(ledger89_append(l, payload, sizeof(payload), NULL) == 0);
    }
    ledger89_close(l);
    _exit(0);
}

static void reader_run(const char *path)
{
    ledger89 *l;
    unsigned long long polls;
    assert(ledger89_open_reader(&l, path) == 0);
    polls = 0ULL;
    for (;;)
    {
        ledger89_iter *it;
        ledger89_offset expected;
        unsigned long long idx;
        assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
        expected = 0;
        idx = 0ULL;
        for (;;)
        {
            int rc;
            rc = ledger89_iter_next(it);
            if (rc == LEDGER89_END)
            {
                break;
            }
            assert(rc == LEDGER89_OK);
            assert(ledger89_iter_index(it) == idx);
            assert(ledger89_iter_offset(it) == expected);
            {
                int wid;
                int seq;
                validate_record(it, &wid, &seq);
                (void)wid;
                (void)seq;
            }
            expected += (ledger89_offset)ledger89_iter_length(it);
            ++idx;
        }
        ledger89_iter_close(it);
        if (idx == (unsigned long long)TOTAL)
        {
            break;
        }
        {
            struct timespec pause;
            pause.tv_sec = 0;
            pause.tv_nsec = 1000000L;
            (void)nanosleep(&pause, NULL);
        }
        ++polls;
        if (polls > (unsigned long long)MAX_POLLS)
        {
            _exit(1);
        }
    }
    ledger89_close(l);
    _exit(0);
}

static void verify_final(const char *path)
{
    ledger89 *l;
    ledger89_iter *it;
    int seen[WRITERS][RECORDS];
    unsigned long long count;
    ledger89_offset expected;
    unsigned long long idx;
    memset(seen, 0, sizeof(seen));
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_count(l, &count) == 0);
    assert(count == (unsigned long long)TOTAL);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    expected = 0;
    idx = 0ULL;
    for (;;)
    {
        int rc;
        rc = ledger89_iter_next(it);
        if (rc == LEDGER89_END)
        {
            break;
        }
        assert(rc == LEDGER89_OK);
        assert(ledger89_iter_index(it) == idx);
        assert(ledger89_iter_offset(it) == expected);
        {
            int wid;
            int seq;
            validate_record(it, &wid, &seq);
            assert(seen[wid][seq] == 0);
            seen[wid][seq] = 1;
        }
        expected += (ledger89_offset)ledger89_iter_length(it);
        ++idx;
    }
    assert(idx == (unsigned long long)TOTAL);
    ledger89_iter_close(it);
    ledger89_close(l);
}

int main(void)
{
    char path[160];
    pid_t writers[WRITERS];
    pid_t readers[READERS];
    int i;
    test_path(path, sizeof(path), "e2e", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    for (i = 0; i < WRITERS; ++i)
    {
        writers[i] = fork();
        assert(writers[i] >= 0);
        if (writers[i] == 0)
        {
            writer_run(path, i);
        }
    }
    for (i = 0; i < READERS; ++i)
    {
        readers[i] = fork();
        assert(readers[i] >= 0);
        if (readers[i] == 0)
        {
            reader_run(path);
        }
    }
    for (i = 0; i < WRITERS; ++i)
    {
        test_wait(writers[i], 0);
    }
    for (i = 0; i < READERS; ++i)
    {
        test_wait(readers[i], 0);
    }
    verify_final(path);
    test_unlink(path);
    return 0;
}
