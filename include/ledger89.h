#ifndef LEDGER89_H
#define LEDGER89_H

/*
 * ledger89.h - a durable append-only sequence of opaque byte records stored
 * in one libappend89 byte stream: a fixed preamble followed by self-framed
 * records [size u64 BE][crc u64 BE][payload]. Each record frame fits within
 * the append reserve, so one reserve append publishes the whole record; the
 * append's sync is its commit point.
 *
 * The API is ISO C89. It depends on libappend89 and libchecksum89. Public
 * functions use the POSIX syscall error profile (CONVENTIONS.md section 14):
 * 0 on success, -1 on failure with errno set to a standard platform value.
 * LEDGER89_OK and LEDGER89_END are ordinary outcomes returned by iteration.
 *
 * Random access by record number (count, read by index, length by index,
 * offset by index) is not provided: locating record N requires a scan. Use
 * iteration for sequential access.
 */

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Opaque handles. A ledger handle owns the single stream. An iterator
     * borrows its ledger and yields one committed record at a time.
     */
    typedef struct ledger89 ledger89;
    typedef struct ledger89_iter ledger89_iter;

    /*
     * An absolute byte offset into the ledger stream.
     */
    typedef off_t ledger89_offset;

#define LEDGER89_OK 0
#define LEDGER89_END 1

    /*
     * Create a new empty ledger at "name". The file is created inside one
     * exclusively created sibling temporary directory and installed by
     * non-replacing hard link, so if the target already exists the call fails
     * with EEXIST and nothing is installed. The 16-byte preamble is written
     * and synchronized before installation. mode is subject to the process
     * umask. The application synchronizes the containing directory for
     * durable naming.
     *
     * On success return 0. On failure return -1 with errno meaningful.
     */
    int ledger89_create(const char *name, mode_t mode);

    /*
     * Open an existing ledger for reading, or for reading and writing. name
     * must be non-NULL; *out is set to NULL before any operation that may
     * fail. Opening never creates or repairs a file. It validates the
     * preamble: a missing, mismatched, or wrong-reserve preamble fails with
     * EINVAL. A reader handle permits iteration; a writer handle additionally
     * permits append and recover.
     *
     * On success return 0 with *out set. On failure return -1 with *out NULL
     * and errno meaningful.
     */
    int ledger89_open_reader(ledger89 **out, const char *name);
    int ledger89_open_writer(ledger89 **out, const char *name);

    /*
     * Close the handle and release resources. NULL is a no-op. Iterators
     * borrowing the ledger must be closed first.
     */
    void ledger89_close(ledger89 *l);

    /*
     * Append one record. data may be NULL only when size is zero. The frame
     * header and payload are copied into one contiguous buffer that is
     * submitted as a single candidate append. The record commits only after
     * its frame is synchronized. On success return 0 and, when offset is not
     * NULL, set *offset to the payload offset of the new record (the
     * committed end before the append plus the frame header size).
     *
     * size + 16 must not exceed the append reserve; otherwise the call fails
     * with E2BIG before any I/O. A failure may leave a complete but
     * unsynchronized final frame; recovery resolves it. Do not infer commit
     * status from the return value alone after an I/O error during
     * synchronization.
     */
    int ledger89_append(ledger89 *l, const void *data, size_t size,
                        ledger89_offset *offset);

    /*
     * Begin iteration over committed records. On success return LEDGER89_OK
     * with *out set to an iterator positioned before the first record. On
     * failure return -1 with *out NULL.
     */
    int ledger89_iter_begin(ledger89 *l, ledger89_iter **out);

    /*
     * Advance the iterator to the next committed record. Returns LEDGER89_OK
     * on advance, LEDGER89_END when no further record exists, or -1 on
     * failure. A newly created iterator must be advanced once before use.
     */
    int ledger89_iter_next(ledger89_iter *it);

    /*
     * Accessors over the current record of a positioned iterator. index is
     * the 0-based record number, length its payload length, and offset its
     * payload offset. When the iterator is not positioned the values are
     * undefined.
     */
    unsigned long long ledger89_iter_index(const ledger89_iter *it);
    unsigned long long ledger89_iter_length(const ledger89_iter *it);
    ledger89_offset ledger89_iter_offset(const ledger89_iter *it);

    /*
     * Read from the iterator's current record, advancing an internal position.
     * Returns the number of bytes read, 0 at the end of the record or for a
     * zero-sized request, or -1 on failure. The checksum is verified while
     * reading; a mismatch or a payload ending early fails with EILSEQ.
     */
    ssize_t ledger89_iter_read(ledger89_iter *it, void *data, size_t size);

    /*
     * Close an iterator. NULL is a no-op. The borrowed ledger must outlive the
     * iterator.
     */
    void ledger89_iter_close(ledger89_iter *it);

    /*
     * Exclusive recovery. The caller MUST exclude all other access and openers.
     * Recovery validates the record prefix (header completeness, size cap,
     * stream bounds, payload checksums) and truncates the invalid suffix,
     * then synchronizes modified state. It is idempotent. On success return 0
     * and, when non-NULL, write the committed end before and after recovery
     * to before and after. On failure return -1.
     */
    int ledger89_recover(ledger89 *l, ledger89_offset *before,
                         ledger89_offset *after);

#ifdef __cplusplus
}
#endif

#endif /* LEDGER89_H */
