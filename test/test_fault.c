/* Layer 5: fault-injection tests over the append89 syscall wrapper. */
#include "support.h"

static void enospc_write(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "fault-enospc", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    a89_fault_reset();
    a89_fault_errno("pwrite", 0, ENOSPC);
    assert(ledger89_append(l, "hello", 5U, NULL) == -1);
    assert(errno == ENOSPC);
    a89_fault_reset();
    assert(ledger89_append(l, "replacement", 11U, NULL) == 0);
    test_expect(l, 0ULL, "replacement", 11U);
    ledger89_close(l);
    test_unlink(path);
}

static void eio_data_sync(void)
{
    char path[160];
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "fault-dsync", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    a89_fault_reset();
    a89_fault_errno("fdatasync", 0, EIO);
    assert(ledger89_append(l, "hello", 5U, NULL) == -1);
    assert(errno == EIO);
    a89_fault_reset();
    /* The failed append committed nothing: an empty ledger after recovery. */
    ledger89_close(l);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 0ULL);
    assert(ledger89_append(l, "replacement", 11U, NULL) == 0);
    test_expect(l, 0ULL, "replacement", 11U);
    ledger89_close(l);
    test_unlink(path);
}

static void eio_index_sync_ambiguous(void)
{
    char path[160];
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "fault-isync", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    a89_fault_reset();
    /* Fail the final INDEX synchronization; the entry is already published. */
    a89_fault_errno("fdatasync", 4, EIO);
    assert(ledger89_append(l, "hello", 5U, NULL) == -1);
    assert(errno == EIO);
    ledger89_close(l);
    /* After reopen and recovery the committed entry survives. */
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_recover(l, NULL, NULL) == 0);
    assert(ledger89_count(l, &count) == 0 && count == 1ULL);
    test_expect(l, 0ULL, "hello", 5U);
    ledger89_close(l);
    test_unlink(path);
}

static void short_writes_eintr(void)
{
    char path[160];
    ledger89 *l;
    test_path(path, sizeof(path), "fault-short", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    a89_fault_reset();
    a89_fault_short_writes(2U);
    a89_fault_eintr("pwrite", 0, 3);
    a89_fault_eintr("fdatasync", 0, 2);
    a89_fault_eintr("ftruncate", 0, 2);
    assert(ledger89_append(l, "abcdefghijklmno", 15U, NULL) == 0);
    a89_fault_reset();
    test_expect(l, 0ULL, "abcdefghijklmno", 15U);
    ledger89_close(l);
    test_unlink(path);
}

static void poisoned_unlock(void)
{
    char path[160];
    ledger89 *l;
    unsigned long long count;
    test_path(path, sizeof(path), "fault-unlock", 0);
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    a89_fault_reset();
    /* Fail the final INDEX unlock; the handle is poisoned. */
    a89_fault_errno("fcntl", 8, EIO);
    assert(ledger89_append(l, "hello", 5U, NULL) == -1);
    assert(errno == EIO);
    a89_fault_reset();
    assert(ledger89_append(l, "bad", 3U, NULL) == -1 && errno == EIO);
    ledger89_close(l);
    /* The record may have committed; recovery resolves it deterministically. */
    assert(ledger89_open_reader(&l, path) == 0);
    assert(ledger89_count(l, &count) == 0);
    ledger89_close(l);
    test_unlink(path);
}

int main(void)
{
    enospc_write();
    eio_data_sync();
    eio_index_sync_ambiguous();
    short_writes_eintr();
    poisoned_unlock();
    return 0;
}
