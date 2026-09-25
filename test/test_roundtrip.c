#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ledger89.h"

int main(void)
{
    char path[128];
    char payload[100];
    char actual[100];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    ledger89_offset start;
    size_t done;
    ssize_t n;

    (void)snprintf(path, sizeof(path), "/tmp/ledger89-round-%ld", (long)getpid());
    memset(payload, 'a', sizeof(payload));
    assert(ledger89_create(path, (mode_t)0600, 32U) == 0);
    assert(ledger89_create(path, (mode_t)0600, 32U) == -1);
    assert(errno == EEXIST);
    assert(ledger89_open_writer(&a, path, 32U) == 0);
    assert(ledger89_append(a, payload, sizeof(payload), &start) == 0);
    assert(start == LEDGER89_BEGIN);
    assert(ledger89_append(a, NULL, 0U, NULL) == 0);
    assert(ledger89_sync(a) == 0);
    cursor = LEDGER89_BEGIN;
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_OK);
    assert(ledger89_message_offset(m) == start);
    done = 0U;
    while ((n = ledger89_message_read(m, actual + done, 3U)) > 0)
    {
        done += (size_t)n;
    }
    assert(n == 0 && done == sizeof(payload));
    assert(memcmp(payload, actual, sizeof(payload)) == 0);
    ledger89_message_close(m);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_OK);
    assert(ledger89_message_length(m) == 0ULL);
    assert(ledger89_message_read(m, actual, sizeof(actual)) == 0);
    ledger89_message_close(m);
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);
    assert(ledger89_open_reader(&a, path, 31U) == -1);
    assert(errno == EINVAL);
    assert(unlink(path) == 0);
    return 0;
}
