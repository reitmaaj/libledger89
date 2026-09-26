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
     * Optional named extension over the essentials API. It inspects and repairs
     * a single-file ledger by pathname.
     *
     * Both operations use the POSIX syscall profile of the essentials API: 0 on
     * success, -1 with a standard platform errno on failure. check is read-only
     * in effect (it opens the stream for writing so it can read the committed
     * end, but it never truncates, appends, or synchronizes).
     */

    /*
     * Outcome of an administrative inspection or repair.
     *
     * valid_end        committed end of the valid prefix. For a recovered
     * ledger this equals the logical size. records          Number of committed
     * records in the valid prefix, counted during the validation scan.
     * incomplete_tail  Nonzero when the ledger was not clean at the time of the
     *                  operation: for check, the current state has bytes beyond
     *                  the valid prefix (a partial header, an oversize frame, a
     *                  payload past the end, or a checksum mismatch); for
     * repair, such bytes existed before repair and were truncated.
     *
     * Fields are written only on success; callers that zero the structure first
     * can therefore inspect it on failure.
     */
    typedef struct ledger89_admin_report
    {
        ledger89_offset valid_end;
        unsigned long long records;
        int incomplete_tail;
    } ledger89_admin_report;

    /*
     * Inspect a ledger without modifying it.
     *
     * report may be NULL. Returns 0 on success, or -1 with errno set (EINVAL,
     * ENOENT, EILSEQ, EIO, or an operating-system error).
     */
    int ledger89_admin_check(const char *name, ledger89_admin_report *report);

    /*
     * Repair an invalid tail.
     *
     * Runs exclusive recovery: any partial final header, oversize frame,
     * payload past the end, or checksum-invalid frame is truncated to the
     * longest valid prefix, and modified state is synchronized. The caller MUST
     * exclude all other access and openers for the duration, matching
     * ledger89_recover().
     *
     * report may be NULL. Returns 0 on success, or -1 with errno set.
     */
    int ledger89_admin_repair(const char *name, ledger89_admin_report *report);

#ifdef __cplusplus
}
#endif

#endif
