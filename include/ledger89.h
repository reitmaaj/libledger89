#ifndef LEDGER89_H
#define LEDGER89_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct ledger89 ledger89;
typedef struct ledger89_message ledger89_message;
typedef off_t ledger89_offset;

#define LEDGER89_BEGIN ((ledger89_offset)16)
#define LEDGER89_OK 0
#define LEDGER89_END 1
#define LEDGER89_PARTIAL 2

/* reserve=0 selects the append default. Otherwise reserve must exceed 24.
 * create installs an initialized, file-synced ledger without replacing path.
 * The application syncs the containing directory for durable naming.
 * Existing files use their original reserve; a mismatch is rejected.
 * Fallible calls return 0 (or a positive LEDGER89_* outcome) on success and
 * -1 on failure with a standard platform errno (CONVENTIONS.md section 14).
 * EINVAL marks invalid arguments or a malformed preamble/reserve; EILSEQ marks
 * structural corruption; EIO marks an uncertain or poisoned I/O outcome. */
int ledger89_create(const char *path, mode_t mode, size_t reserve);
int ledger89_open_reader(ledger89 **out, const char *path, size_t reserve);
int ledger89_open_writer(ledger89 **out, const char *path, size_t reserve);
void ledger89_close(ledger89 *a);

/* One lock covers all fragments. No rollback on failure: later writers can
 * start at EOF, and readers skip the abandoned predecessor. offset changes
 * only on success. An EIO failure here may mean the publication/unlock outcome
 * requires caller reconciliation; close the poisoned handle. Sync alone
 * promises durability. Data and iov storage are borrowed for the call. */
int ledger89_append(ledger89 *a, const void *data, size_t size,
                    ledger89_offset *offset);
int ledger89_appendv(ledger89 *a, const struct iovec *iov, int iovcnt,
                     ledger89_offset *offset);
int ledger89_sync(ledger89 *a);

/* Validate the next complete message using constant working memory, then
 * return a payload view borrowing a. Caller closes views before closing a.
 * OK advances cursor past that message. END/PARTIAL advance it past abandoned
 * messages, retaining the current incomplete start for retry. Errors leave
 * cursor unchanged. out is NULL except on OK. Start with LEDGER89_BEGIN.
 * Concurrent appends are supported; truncate/recovery are excluded. */
int ledger89_next(ledger89 *a, ledger89_offset *cursor,
                  ledger89_message **out);
unsigned long long ledger89_message_length(const ledger89_message *message);
ledger89_offset ledger89_message_offset(const ledger89_message *message);
ssize_t ledger89_message_read(ledger89_message *message, void *data, size_t size);
void ledger89_message_close(ledger89_message *message);

/* Exclusive maintenance ONLY: caller must exclude all other access/openers.
 * Scan, shrink any incomplete final message, sync even on a clean tail.
 * Complete malformed headers cause EILSEQ without truncation. before/after
 * are written only on success. Open reader/writer descriptors before locking.
 * System crash requires this phase before restarting normal writers. */
int ledger89_recover(ledger89 *a, ledger89_offset *before,
                     ledger89_offset *after);

#ifdef __cplusplus
}
#endif
#endif
