/* e2e_flaky.c - long-running flaky-writer end-to-end scenario.
 *
 * Sixteen concurrent writer processes per generation append uniquely
 * identifiable records until the final ledger reaches at least 10 MiB of
 * logical bytes. Writers are flaky: each lifetime plans a few crash events
 * (before publication, after publication, or mid-candidate), injected sync
 * EIO failures, handle reopens, oversized append attempts that must fail
 * E2BIG, and a random clean-shutdown quota. The parent respawns crashed
 * writers, runs exclusive recovery plus full prefix verification at every
 * quiet point, and checks the final accounting: every acknowledged record is
 * present exactly once, every present record was attempted with its recorded
 * size, and no duplicate or fabricated record exists. Two reader processes
 * poll the committed prefix outside recovery windows.
 *
 * Deterministic for a given seed (E2E_SEED env var, default 1). Not part of
 * `just test`; run with `just e2e`.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "support.h"

#define E2E_TARGET (10U * 1024U * 1024U)
#define E2E_SLACK (3U * 1024U * 1024U)
#define E2E_WRITERS 16
#define E2E_GENERATIONS 20
#define E2E_READERS 2
#define E2E_GEN_QUOTA (1024U * 1024U)
#define E2E_MAX_SPAWNS 96
#define E2E_MAX_ITER 5000
#define E2E_MAX_POLLS 200000
#define E2E_WRITER_QUOTA (E2E_GEN_QUOTA / E2E_WRITERS)
#define E2E_MIN_SIZE 8U

struct e2e_rec
{
    unsigned long id;
    unsigned long seq;
    unsigned long long size;
};

struct e2e_set
{
    struct e2e_rec *v;
    size_t n;
    size_t cap;
};

static unsigned long long g_rng;
static char g_dir[256];
static int g_max_id;

static unsigned long long e2e_rng(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static void e2e_sleep_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000L;
    ts.tv_nsec = (ms % 1000L) * 1000000L;
    (void)nanosleep(&ts, NULL);
}

static unsigned long e2e_parse_ulong(const char *text)
{
    unsigned long value;
    size_t i;

    value = 0UL;
    for (i = 0U; text[i] != '\0'; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return 0UL;
        }
        value = value * 10UL + (unsigned long)(text[i] - '0');
    }
    return value;
}

static int e2e_write_all(int fd, const char *p, size_t size)
{
    size_t done;
    ssize_t n;

    done = 0U;
    while (done < size)
    {
        n = write(fd, p + done, size - done);
        if (n < 0)
        {
            return -1;
        }
        if (n == 0)
        {
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static unsigned long e2e_load_be4(const unsigned char in[4])
{
    unsigned long value;
    int i;

    value = 0UL;
    for (i = 0; i < 4; ++i)
    {
        value = (value << 8) | (unsigned long)in[i];
    }
    return value;
}

static void e2e_store_be4(unsigned char out[4], unsigned long value)
{
    int i;

    for (i = 3; i >= 0; --i)
    {
        out[i] = (unsigned char)(value & 0xffUL);
        value >>= 8;
    }
}

static void e2e_fill(unsigned char *p, unsigned long id, unsigned long seq,
                     size_t size)
{
    size_t j;

    if (size >= 4U)
    {
        e2e_store_be4(p, id);
    }
    if (size >= 8U)
    {
        e2e_store_be4(p + 4, seq);
    }
    for (j = 8U; j < size; ++j)
    {
        p[j] = (unsigned char)((id * 7U + seq * 13U + j * 3U) & 0xffU);
    }
}

static int e2e_pattern_ok(const unsigned char *p, size_t size, unsigned long id,
                          unsigned long seq)
{
    size_t j;

    for (j = 8U; j < size; ++j)
    {
        if (p[j] != (unsigned char)((id * 7U + seq * 13U + j * 3U) & 0xffU))
        {
            return 0;
        }
    }
    return 1;
}

static size_t e2e_pick_size(void)
{
    unsigned long long r;

    r = e2e_rng() % 100U;
    if (r < 20U)
    {
        return 0U; /* zero-length record */
    }
    if (r < 94U)
    {
        return 8U + (size_t)(e2e_rng() % 1017U); /* 8..1024 */
    }
    if (r < 99U)
    {
        return 1025U + (size_t)(e2e_rng() % 3072U); /* 1025..4096 */
    }
    return 4097U + (size_t)(e2e_rng() % 61440U); /* 4097..65536 */
}

static int e2e_is_event(int iter, const unsigned long *events, int n)
{
    int i;

    for (i = 0; i < n; ++i)
    {
        if (events[i] == (unsigned long)iter)
        {
            return 1;
        }
    }
    return 0;
}

static void e2e_account_line(char *buf, size_t cap, const char *kind,
                             unsigned long id, unsigned long seq, size_t size)
{
    (void)snprintf(buf, cap, "%s %lu %lu %lu\n", kind, id, seq,
                   (unsigned long)size);
}

static void writer_run(const char *path, const char *dir, int id,
                       unsigned long seed)
{
    ledger89 *l;
    unsigned char *payload;
    unsigned long events_crash[3];
    unsigned long events_eio[2];
    unsigned long events_reopen[3];
    unsigned long wid;
    char acct[256];
    char line[128];
    size_t quota;
    size_t stop_quota;
    size_t size;
    unsigned long seq;
    int n_crash;
    int n_eio;
    int n_reopen;
    int iter;
    int rc;
    int i;
    int fd;

    g_rng = (unsigned long long)seed * 0x9e3779b97f4a7c15ULL + 1ULL;
    wid = (unsigned long)id;
    payload = (unsigned char *)malloc((size_t)APPEND89_RESERVE);
    assert(payload != NULL);
    (void)snprintf(acct, sizeof(acct), "%s/acct-%d", dir, id);
    fd = open(acct, O_WRONLY | O_CREAT | O_APPEND, 0600);
    assert(fd >= 0);
    assert(ledger89_open_writer(&l, path) == 0);
    n_crash = (int)(e2e_rng() % 3U);
    n_eio = (int)(e2e_rng() % 2U);
    n_reopen = (int)(e2e_rng() % 3U);
    for (i = 0; i < n_crash; ++i)
    {
        events_crash[i] = (unsigned long)(e2e_rng() % 100U);
    }
    for (i = 0; i < n_eio; ++i)
    {
        events_eio[i] = (unsigned long)(e2e_rng() % 100U);
    }
    for (i = 0; i < n_reopen; ++i)
    {
        events_reopen[i] = (unsigned long)(e2e_rng() % 100U);
    }
    quota = 0U;
    stop_quota = E2E_WRITER_QUOTA / 2U +
                 (size_t)(e2e_rng() % (E2E_WRITER_QUOTA / 2U + 1U));
    seq = 0UL;
    iter = 0;
    for (;;)
    {
        if (quota >= stop_quota)
        {
            break;
        }
        if (iter >= E2E_MAX_ITER)
        {
            break;
        }
        if (e2e_is_event(iter, events_crash, n_crash))
        {
            unsigned long long kind;

            kind = e2e_rng() % 3U;
            size = 64U + (size_t)(e2e_rng() % 64U);
            e2e_fill(payload, wid, seq, size);
            e2e_account_line(line, sizeof(line), "attempt", wid, seq, size);
            assert(e2e_write_all(fd, line, strlen(line)) == 0);
            a89_fault_reset();
            if (kind == 0U)
            {
                a89_fault_kill_before("ftruncate", 1);
            }
            if (kind == 1U)
            {
                a89_fault_kill_after("ftruncate", 1);
            }
            if (kind == 2U)
            {
                a89_fault_kill_after_write_bytes(
                    (off_t)(16 + (int)(e2e_rng() % 32U)));
            }
            (void)ledger89_append(l, payload, size, NULL);
            _exit(3);
        }
        if (e2e_is_event(iter, events_eio, n_eio))
        {
            unsigned long long occ;

            occ = 1U + (e2e_rng() % 2U);
            size = 8U + (size_t)(e2e_rng() % 64U);
            e2e_fill(payload, wid, seq, size);
            e2e_account_line(line, sizeof(line), "attempt", wid, seq, size);
            assert(e2e_write_all(fd, line, strlen(line)) == 0);
            a89_fault_reset();
            a89_fault_errno("fdatasync", (int)occ, EIO);
            rc = ledger89_append(l, payload, size, NULL);
            assert(rc == -1);
            assert(errno == EIO);
            a89_fault_reset();
            /* The failed append may still have committed; seq identifies the
             * attempt, so the next attempt uses a fresh seq. */
            seq = seq + 1UL;
            ++iter;
            continue;
        }
        if (e2e_is_event(iter, events_reopen, n_reopen))
        {
            ledger89_close(l);
            assert(ledger89_open_writer(&l, path) == 0);
            ++iter;
            continue;
        }
        if (iter > 0 && (iter % 11) == 0)
        {
            size = (size_t)APPEND89_RESERVE + 1U + (size_t)(e2e_rng() % 1024U);
            (void)snprintf(line, sizeof(line), "reject %lu\n",
                           (unsigned long)size);
            assert(e2e_write_all(fd, line, strlen(line)) == 0);
            rc = ledger89_append(l, payload, size, NULL);
            assert(rc == -1);
            assert(errno == E2BIG);
            ++iter;
            continue;
        }
        size = e2e_pick_size();
        e2e_fill(payload, wid, seq, size);
        e2e_account_line(line, sizeof(line), "attempt", wid, seq, size);
        assert(e2e_write_all(fd, line, strlen(line)) == 0);
        rc = ledger89_append(l, size == 0U ? NULL : payload, size, NULL);
        if (rc == 0)
        {
            e2e_account_line(line, sizeof(line), "ack", wid, seq, size);
            assert(e2e_write_all(fd, line, strlen(line)) == 0);
            quota += size + LEDGER89_TEST_HEADER_SIZE;
        }
        /* seq identifies the attempt, acked or not. */
        seq = seq + 1UL;
        ++iter;
    }
    ledger89_close(l);
    close(fd);
    free(payload);
    _exit(0);
}

static void spawn_writer(const char *path, const char *dir, int id,
                         unsigned long seed, pid_t *pids, int index, int *live)
{
    pid_t child;

    child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        writer_run(path, dir, id, seed);
    }
    pids[index] = child;
    *live = *live + 1;
}

static void e2e_reap(pid_t *pids, int n, int *live)
{
    int i;

    for (i = 0; i < n; ++i)
    {
        pid_t w;
        int st;

        if (pids[i] <= 0)
        {
            continue;
        }
        w = waitpid(pids[i], &st, WNOHANG);
        if (w == 0)
        {
            continue;
        }
        assert(w == pids[i]);
        pids[i] = -1;
        *live = *live - 1;
        if (WIFSIGNALED(st))
        {
            if (WTERMSIG(st) != SIGKILL)
            {
                fprintf(stderr, "e2e: writer died with signal %d\n",
                        WTERMSIG(st));
                exit(1);
            }
        }
        else if (WIFEXITED(st))
        {
            if (WEXITSTATUS(st) != 0)
            {
                fprintf(stderr, "e2e: writer exited with status %d\n",
                        WEXITSTATUS(st));
                exit(1);
            }
        }
        else
        {
            fprintf(stderr, "e2e: writer vanished unexpectedly\n");
            exit(1);
        }
    }
}

static int e2e_read_file(const char *path, char **out, size_t *out_len)
{
    char *buf;
    size_t cap;
    size_t len;
    int fd;
    ssize_t n;

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        if (errno == ENOENT)
        {
            *out = NULL;
            *out_len = 0U;
            return 0;
        }
        return -1;
    }
    cap = 4096U;
    buf = (char *)malloc(cap);
    assert(buf != NULL);
    len = 0U;
    for (;;)
    {
        char *grown;

        if (len == cap)
        {
            cap = cap * 2U;
            grown = (char *)realloc(buf, cap);
            assert(grown != NULL);
            buf = grown;
        }
        n = read(fd, buf + len, cap - len);
        if (n < 0)
        {
            close(fd);
            free(buf);
            return -1;
        }
        if (n == 0)
        {
            break;
        }
        len += (size_t)n;
    }
    close(fd);
    *out = buf;
    *out_len = len;
    return 0;
}

/* Parse one accounting line into a kind and up to three numbers. Returns the
 * number of parsed numbers. */
static int e2e_parse_line(char *line, char *kind, size_t kind_cap,
                          unsigned long nums[3])
{
    size_t pos;
    size_t t;
    char tok[64];
    int n;

    pos = 0U;
    t = 0U;
    while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t')
    {
        if (t + 1U < kind_cap)
        {
            kind[t] = line[pos];
            t = t + 1U;
        }
        pos = pos + 1U;
    }
    kind[t] = '\0';
    n = 0;
    while (n < 3)
    {
        while (line[pos] == ' ' || line[pos] == '\t')
        {
            pos = pos + 1U;
        }
        if (line[pos] == '\0')
        {
            break;
        }
        t = 0U;
        while (line[pos] != '\0' && line[pos] != ' ' && line[pos] != '\t')
        {
            if (t + 1U < sizeof(tok))
            {
                tok[t] = line[pos];
                t = t + 1U;
            }
            pos = pos + 1U;
        }
        tok[t] = '\0';
        nums[n] = e2e_parse_ulong(tok);
        n = n + 1;
    }
    return n;
}

static void e2e_set_add(struct e2e_set *s, unsigned long id, unsigned long seq,
                        unsigned long long size)
{
    struct e2e_rec *grown;

    if (s->n == s->cap)
    {
        s->cap = s->cap == 0U ? 1024U : s->cap * 2U;
        grown = (struct e2e_rec *)realloc(s->v, s->cap * sizeof(*s->v));
        assert(grown != NULL);
        s->v = grown;
    }
    s->v[s->n].id = id;
    s->v[s->n].seq = seq;
    s->v[s->n].size = size;
    s->n = s->n + 1U;
}

static int e2e_rec_cmp(const void *a, const void *b)
{
    const struct e2e_rec *x;
    const struct e2e_rec *y;

    x = (const struct e2e_rec *)a;
    y = (const struct e2e_rec *)b;
    if (x->id < y->id)
    {
        return -1;
    }
    if (x->id > y->id)
    {
        return 1;
    }
    if (x->seq < y->seq)
    {
        return -1;
    }
    if (x->seq > y->seq)
    {
        return 1;
    }
    return 0;
}

static struct e2e_rec *e2e_set_find(struct e2e_set *s,
                                    const struct e2e_rec *key)
{
    if (s->n == 0U)
    {
        return NULL;
    }
    return (struct e2e_rec *)bsearch(key, s->v, s->n, sizeof(*s->v),
                                     e2e_rec_cmp);
}

/* Read every accounting file below max_id. */
static void e2e_read_accounting(const char *dir, int max_id,
                                struct e2e_set *attempted,
                                struct e2e_set *acked,
                                unsigned long *zero_attempted,
                                unsigned long *zero_acked,
                                unsigned long *rejects)
{
    int id;

    for (id = 0; id < max_id; ++id)
    {
        char path[256];
        char *buf;
        size_t len;
        size_t pos;

        (void)snprintf(path, sizeof(path), "%s/acct-%d", dir, id);
        assert(e2e_read_file(path, &buf, &len) == 0);
        pos = 0U;
        while (pos < len)
        {
            char line[256];
            char kind[32];
            unsigned long nums[3];
            size_t t;
            int full;
            int nnum;

            t = 0U;
            full = 0;
            while (pos < len && t + 1U < sizeof(line))
            {
                char c;

                c = buf[pos];
                pos = pos + 1U;
                if (c == '\n')
                {
                    full = 1;
                    break;
                }
                line[t] = c;
                t = t + 1U;
            }
            line[t] = '\0';
            if (!full)
            {
                continue; /* partial trailing line */
            }
            nnum = e2e_parse_line(line, kind, sizeof(kind), nums);
            if (strcmp(kind, "attempt") == 0 && nnum == 3)
            {
                e2e_set_add(attempted, nums[0], nums[1],
                            (unsigned long long)nums[2]);
                if (nums[2] == 0UL)
                {
                    *zero_attempted = *zero_attempted + 1UL;
                }
            }
            else if (strcmp(kind, "ack") == 0 && nnum == 3)
            {
                e2e_set_add(acked, nums[0], nums[1],
                            (unsigned long long)nums[2]);
                if (nums[2] == 0UL)
                {
                    *zero_acked = *zero_acked + 1UL;
                }
            }
            else if (strcmp(kind, "reject") == 0 && nnum == 1)
            {
                *rejects = *rejects + 1UL;
            }
        }
        free(buf);
    }
}

/* Full prefix verification against the accounting: acked ⊆ attempted,
 * present ⊆ attempted, acked ⊆ present, no duplicate present records, and
 * the zero-length record count between the acknowledged and attempted
 * bounds. */
static void e2e_verify(const char *path, const char *dir, int max_id)
{
    struct e2e_set attempted;
    struct e2e_set acked;
    struct e2e_set present;
    unsigned long zero_attempted;
    unsigned long zero_acked;
    unsigned long present_zero;
    unsigned long rejects;
    unsigned char *buf;
    ledger89 *l;
    ledger89_iter *it;
    size_t i;
    int rc;

    memset(&attempted, 0, sizeof(attempted));
    memset(&acked, 0, sizeof(acked));
    memset(&present, 0, sizeof(present));
    zero_attempted = 0UL;
    zero_acked = 0UL;
    present_zero = 0UL;
    rejects = 0UL;
    e2e_read_accounting(dir, max_id, &attempted, &acked, &zero_attempted,
                        &zero_acked, &rejects);
    qsort(attempted.v, attempted.n, sizeof(*attempted.v), e2e_rec_cmp);
    qsort(acked.v, acked.n, sizeof(*acked.v), e2e_rec_cmp);

    buf = (unsigned char *)malloc((size_t)APPEND89_RESERVE);
    assert(buf != NULL);
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
    while ((rc = ledger89_iter_next(it)) == LEDGER89_OK)
    {
        unsigned long long len_ll;
        unsigned long id;
        unsigned long seq;
        size_t len;
        size_t done;
        ssize_t n;

        len_ll = ledger89_iter_length(it);
        assert(len_ll < (unsigned long long)APPEND89_RESERVE);
        len = (size_t)len_ll;
        if (len == 0U)
        {
            present_zero = present_zero + 1UL;
            continue;
        }
        assert(len >= E2E_MIN_SIZE);
        done = 0U;
        while (done < len)
        {
            n = ledger89_iter_read(it, buf + done, len - done);
            assert(n > 0);
            done += (size_t)n;
        }
        id = e2e_load_be4(buf);
        seq = e2e_load_be4(buf + 4);
        assert(e2e_pattern_ok(buf, len, id, seq));
        e2e_set_add(&present, id, seq, len_ll);
    }
    assert(rc == LEDGER89_END);
    ledger89_iter_close(it);
    ledger89_close(l);
    qsort(present.v, present.n, sizeof(*present.v), e2e_rec_cmp);

    for (i = 0U; i < acked.n; ++i)
    {
        struct e2e_rec *found;

        found = e2e_set_find(&attempted, &acked.v[i]);
        assert(found != NULL);
        assert(found->size == acked.v[i].size);
        if (acked.v[i].size >= (unsigned long long)E2E_MIN_SIZE)
        {
            found = e2e_set_find(&present, &acked.v[i]);
            assert(found != NULL);
            assert(found->size == acked.v[i].size);
        }
    }
    for (i = 0U; i < present.n; ++i)
    {
        struct e2e_rec *found;

        found = e2e_set_find(&attempted, &present.v[i]);
        assert(found != NULL);
        assert(found->size == present.v[i].size);
        if (i > 0U)
        {
            assert(e2e_rec_cmp(&present.v[i - 1U], &present.v[i]) != 0);
        }
    }
    assert(present_zero >= zero_acked);
    assert(present_zero <= zero_attempted);
    fprintf(stderr, "e2e: verify ok: %lu acked, %lu present, %lu rejects\n",
            (unsigned long)acked.n, (unsigned long)present.n, rejects);
    free(attempted.v);
    free(acked.v);
    free(present.v);
    free(buf);
}

static unsigned long long e2e_acked_total(const char *dir, int max_id)
{
    struct e2e_set attempted;
    struct e2e_set acked;
    unsigned long zero_attempted;
    unsigned long zero_acked;
    unsigned long rejects;
    unsigned long long total;
    size_t i;

    memset(&attempted, 0, sizeof(attempted));
    memset(&acked, 0, sizeof(acked));
    zero_attempted = 0UL;
    zero_acked = 0UL;
    rejects = 0UL;
    e2e_read_accounting(dir, max_id, &attempted, &acked, &zero_attempted,
                        &zero_acked, &rejects);
    total = 0ULL;
    for (i = 0U; i < acked.n; ++i)
    {
        total += acked.v[i].size + (unsigned long long)LEDGER89_TEST_HEADER_SIZE;
    }
    free(attempted.v);
    free(acked.v);
    return total;
}

static void run_generation(const char *path, const char *dir, int *next_id,
                           unsigned long seed)
{
    pid_t pids[E2E_MAX_SPAWNS];
    int spawns;
    int live;
    int stop;
    long loops;
    int i;

    spawns = 0;
    live = 0;
    stop = 0;
    loops = 0L;
    for (i = 0; i < E2E_WRITERS; ++i)
    {
        spawn_writer(path, dir, *next_id, seed, pids, spawns, &live);
        *next_id = *next_id + 1;
        spawns = spawns + 1;
    }
    while (live > 0 || (stop == 0 && spawns < E2E_MAX_SPAWNS))
    {
        e2e_reap(pids, spawns, &live);
        if (stop == 0 && live < E2E_WRITERS && spawns < E2E_MAX_SPAWNS)
        {
            spawn_writer(path, dir, *next_id, seed, pids, spawns, &live);
            *next_id = *next_id + 1;
            spawns = spawns + 1;
        }
        loops = loops + 1L;
        if (stop == 0 && (loops % 5L) == 0L)
        {
            if (e2e_acked_total(dir, *next_id) >=
                (unsigned long long)E2E_GEN_QUOTA)
            {
                stop = 1;
            }
        }
        e2e_sleep_ms(2);
    }
    while (live > 0)
    {
        e2e_reap(pids, spawns, &live);
        e2e_sleep_ms(2);
    }
}

static void reader_run(const char *path, const char *dir)
{
    char done[256];
    char recovering[256];
    unsigned long long polls;
    unsigned long long prev_count;
    unsigned char buf[4096];

    (void)snprintf(done, sizeof(done), "%s/done", dir);
    (void)snprintf(recovering, sizeof(recovering), "%s/recovering", dir);
    polls = 0ULL;
    prev_count = 0ULL;
    for (;;)
    {
        ledger89 *l;
        ledger89_iter *it;
        ledger89_offset expected;
        unsigned long long count;
        int rc;

        if (getppid() == 1)
        {
            _exit(1); /* parent died; do not spin forever */
        }
        if (access(recovering, F_OK) == 0)
        {
            e2e_sleep_ms(5);
            continue;
        }
        assert(ledger89_open_reader(&l, path) == 0);
        assert(ledger89_iter_begin(l, &it) == LEDGER89_OK);
        expected = (ledger89_offset)(16 + 16);
        count = 0ULL;
        while ((rc = ledger89_iter_next(it)) == LEDGER89_OK)
        {
            unsigned long long len_ll;
            size_t len;
            size_t drained;
            ssize_t n;

            assert(ledger89_iter_index(it) == count);
            assert(ledger89_iter_offset(it) == expected);
            len_ll = ledger89_iter_length(it);
            assert(len_ll < (unsigned long long)APPEND89_RESERVE);
            len = (size_t)len_ll;
            if (len >= E2E_MIN_SIZE)
            {
                unsigned char hdr[8];

                n = ledger89_iter_read(it, hdr, 8U);
                assert(n == 8);
                assert(e2e_load_be4(hdr) < 1024UL);
                assert(e2e_load_be4(hdr + 4) < (1UL << 20));
                drained = 8U;
                while (drained < len)
                {
                    n = ledger89_iter_read(it, buf, sizeof(buf));
                    assert(n > 0);
                    drained += (size_t)n;
                }
            }
            else if (len > 0U)
            {
                drained = 0U;
                while (drained < len)
                {
                    n = ledger89_iter_read(it, buf, sizeof(buf));
                    assert(n > 0);
                    drained += (size_t)n;
                }
            }
            expected += (ledger89_offset)len + (ledger89_offset)16;
            count = count + 1ULL;
        }
        assert(rc == LEDGER89_END);
        assert(count >= prev_count);
        prev_count = count;
        ledger89_iter_close(it);
        ledger89_close(l);
        if (access(done, F_OK) == 0)
        {
            break;
        }
        e2e_sleep_ms(50);
        polls = polls + 1ULL;
        assert(polls < (unsigned long long)E2E_MAX_POLLS);
    }
    _exit(0);
}

static void e2e_cleanup(void)
{
    char path[256];
    int i;

    (void)snprintf(path, sizeof(path), "%s/ledger", g_dir);
    (void)unlink(path);
    (void)snprintf(path, sizeof(path), "%s/done", g_dir);
    (void)unlink(path);
    (void)snprintf(path, sizeof(path), "%s/recovering", g_dir);
    (void)unlink(path);
    for (i = 0; i < g_max_id; ++i)
    {
        (void)snprintf(path, sizeof(path), "%s/acct-%d", g_dir, i);
        (void)unlink(path);
    }
    (void)rmdir(g_dir);
}

int main(void)
{
    const char *seed_text;
    unsigned long seed;
    char path[256];
    char recovering[256];
    char done[256];
    pid_t readers[E2E_READERS];
    int next_id;
    int g;
    int i;
    off_t size;
    int reached;

    seed_text = getenv("E2E_SEED");
    seed = 1UL;
    if (seed_text != NULL)
    {
        seed = e2e_parse_ulong(seed_text);
    }
    g_dir[0] = '\0';
    (void)snprintf(g_dir, sizeof(g_dir), "/tmp/ledger89-e2e-XXXXXX");
    assert(mkdtemp(g_dir) != NULL);
    g_max_id = 0;
    assert(atexit(e2e_cleanup) == 0);
    (void)snprintf(path, sizeof(path), "%s/ledger", g_dir);
    (void)snprintf(recovering, sizeof(recovering), "%s/recovering", g_dir);
    (void)snprintf(done, sizeof(done), "%s/done", g_dir);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    for (i = 0; i < E2E_READERS; ++i)
    {
        readers[i] = fork();
        assert(readers[i] >= 0);
        if (readers[i] == 0)
        {
            reader_run(path, g_dir);
        }
    }
    next_id = 0;
    reached = 0;
    size = 0;
    for (g = 0; g < E2E_GENERATIONS; ++g)
    {
        ledger89 *l;

        run_generation(path, g_dir, &next_id, seed);
        g_max_id = next_id;
        /* Quiet point: exclusive recovery, idempotent, then full verify. */
        {
            int fd;

            fd = open(recovering, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            assert(fd >= 0);
            assert(close(fd) == 0);
        }
        e2e_sleep_ms(200); /* let in-flight reader polls finish */
        assert(ledger89_open_writer(&l, path) == 0);
        assert(ledger89_recover(l, NULL, NULL) == 0);
        assert(ledger89_recover(l, NULL, NULL) == 0);
        ledger89_close(l);
        assert(unlink(recovering) == 0);
        e2e_verify(path, g_dir, next_id);
        size = test_logical_size(path);
        fprintf(stderr, "e2e: generation %d done: %ld logical bytes\n", g,
                (long)size);
        if (size >= (off_t)E2E_TARGET)
        {
            reached = 1;
            break;
        }
    }
    assert(reached == 1);
    assert(size < (off_t)(E2E_TARGET + E2E_SLACK));
    {
        int fd;

        fd = open(done, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        assert(fd >= 0);
        assert(close(fd) == 0);
    }
    for (i = 0; i < E2E_READERS; ++i)
    {
        int st;

        assert(waitpid(readers[i], &st, 0) == readers[i]);
        assert(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    }
    fprintf(stderr, "e2e: ok: %ld logical bytes, %d writer lifetimes\n",
            (long)size, next_id);
    return 0;
}
