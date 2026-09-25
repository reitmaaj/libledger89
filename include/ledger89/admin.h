#ifndef LEDGER89_ADMIN_H
#define LEDGER89_ADMIN_H

#include <ledger89.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * ledger89/admin.h - administrative inspection and repair.
 *
 * Optional named extension over the essentials API. It inspects and repairs a
 * ledger by pathname, discovering the reserve from the file preamble so that
 * no caller-supplied reserve is required.
 *
 * Both operations use the POSIX syscall profile of the essentials API: 0 on
 * success, -1 with a standard platform errno on failure.
 */

/*
 * Outcome of an administrative scan or repair.
 *
 * valid_end        Logical offset of the end of the largest complete-record
 *                  prefix. For a clean ledger this is the logical end. For a
 *                  ledger with an incomplete trailing record it is the start
 *                  of that record, i.e. the boundary a repair truncates to.
 * records          Number of complete records observed (check) or remaining
 *                  after repair.
 * incomplete_tail  Nonzero when a trailing incomplete record existed at the
 *                  time of the operation.
 *
 * Fields are written only on success; callers that zero the structure first
 * can therefore inspect it on failure.
 */
typedef struct ledger89_admin_report
{
    ledger89_offset valid_end;
    unsigned long records;
    int incomplete_tail;
} ledger89_admin_report;

/*
 * Inspect a ledger without modifying it.
 *
 * Opens the file read-only, discovers the reserve, and scans from the first
 * record boundary. A structurally corrupt interior fails with EILSEQ and
 * truncates nothing. An incomplete trailing record is reported through
 * report->incomplete_tail; it is not an error.
 *
 * report may be NULL. Returns 0 on success, or -1 with errno set (EINVAL,
 * ENOENT, EOVERFLOW, EILSEQ, EIO, or an operating-system error).
 */
int ledger89_admin_check(const char *path, ledger89_admin_report *report);

/*
 * Repair an incomplete trailing record.
 *
 * Opens the file for writing, discovers the reserve, and runs exclusive
 * recovery: any incomplete final record is truncated exactly at its start and
 * the result is synchronized. A corrupt interior fails with EILSEQ and is not
 * truncated. The caller must exclude all other access and openers for the
 * duration, matching ledger89_recover().
 *
 * report may be NULL. Returns 0 on success, or -1 with errno set.
 */
int ledger89_admin_repair(const char *path, ledger89_admin_report *report);

#ifdef __cplusplus
}
#endif

#endif
