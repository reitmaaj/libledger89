/* ledger89_cli.c - record-oriented helpers for the ledger89 CLI. */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ledger89_cli.h"
#include "ledger89/admin.h"

#define LEDGER89_CLI_CHUNK 4096U
#define LEDGER89_CLI_PATH_MAX 4096U
#define LEDGER89_CLI_BUF 32U
#define LEDGER89_CLI_POLL_MS 200

/* Native unsigned long long preamble helpers (libll89 is gone). */
static unsigned long long cli_load_be(const unsigned char in[8])
{
    unsigned long long value;
    int i;
    value = 0ULL;
    for (i = 0; i < 8; ++i)
    {
        value = (value << 8) | (unsigned long long)in[i];
    }
    return value;
}

static int cli_to_size(unsigned long long value, size_t *out)
{
    if (value > (unsigned long long)(size_t)-1)
    {
        return -1;
    }
    *out = (size_t)value;
    return 0;
}

static int cli_fail(const char *what, const char *path)
{
    (void)fprintf(stderr, "ledger89: %s '%s'\n", what, path);
    return 1;
}

static uintmax_t cli_u64_to_uintmax(unsigned long long value)
{
    return (uintmax_t)value;
}

static size_t cli_format_u64(char *buf, uintmax_t value)
{
    char tmp[LEDGER89_CLI_BUF];
    size_t n;
    size_t i;
    n = 0U;
    do
    {
        tmp[n] = (char)('0' + (int)(value % 10U));
        value /= 10U;
        ++n;
    } while (value != 0U);
    for (i = 0U; i < n; ++i)
    {
        buf[i] = tmp[n - 1U - i];
    }
    return n;
}

size_t ledger89_cli_format_offset(char *buf, ledger89_offset value)
{
    return cli_format_u64(buf, (uintmax_t)value);
}

size_t ledger89_cli_format_length(char *buf, unsigned long long value)
{
    return cli_format_u64(buf, cli_u64_to_uintmax(value));
}

int ledger89_cli_parse_offset(const char *text, ledger89_offset *out)
{
    uintmax_t maxoff;
    uintmax_t value;
    uintmax_t digit;
    size_t i;

    if (text == NULL || text[0] == '\0')
    {
        return -1;
    }
    maxoff = ((uintmax_t)1 << (sizeof(off_t) * CHAR_BIT - 1)) - (uintmax_t)1;
    value = 0U;
    for (i = 0U; text[i] != '\0'; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return -1;
        }
        digit = (uintmax_t)(text[i] - '0');
        if (value > (maxoff - digit) / 10U)
        {
            return -1;
        }
        value = value * 10U + digit;
    }
    *out = (ledger89_offset)value;
    return 0;
}

ledger89_cli_cmd ledger89_cli_parse(int argc, char **argv, const char **path,
                                    ledger89_offset *offset, int *has_offset)
{
    const char *cmd;

    *path = NULL;
    *offset = (ledger89_offset)0;
    *has_offset = 0;
    if (argc < 3)
    {
        return LEDGER89_CLI_CMD_USAGE;
    }
    cmd = argv[1];
    *path = argv[2];
    if (strcmp(cmd, "init") == 0)
    {
        return argc == 3 ? LEDGER89_CLI_CMD_INIT : LEDGER89_CLI_CMD_USAGE;
    }
    if (strcmp(cmd, "append") == 0)
    {
        return argc == 3 ? LEDGER89_CLI_CMD_APPEND : LEDGER89_CLI_CMD_USAGE;
    }
    if (strcmp(cmd, "check") == 0)
    {
        return argc == 3 ? LEDGER89_CLI_CMD_CHECK : LEDGER89_CLI_CMD_USAGE;
    }
    if (strcmp(cmd, "repair") == 0)
    {
        return argc == 3 ? LEDGER89_CLI_CMD_REPAIR : LEDGER89_CLI_CMD_USAGE;
    }
    if (strcmp(cmd, "tail") == 0)
    {
        return argc == 3 ? LEDGER89_CLI_CMD_TAIL : LEDGER89_CLI_CMD_USAGE;
    }
    if (strcmp(cmd, "read") == 0)
    {
        if (argc != 4 || ledger89_cli_parse_offset(argv[3], offset) != 0)
        {
            return LEDGER89_CLI_CMD_USAGE;
        }
        return LEDGER89_CLI_CMD_READ;
    }
    if (strcmp(cmd, "scan") == 0)
    {
        if (argc == 3)
        {
            return LEDGER89_CLI_CMD_SCAN;
        }
        if (argc == 4 && ledger89_cli_parse_offset(argv[3], offset) == 0)
        {
            *has_offset = 1;
            return LEDGER89_CLI_CMD_SCAN;
        }
        return LEDGER89_CLI_CMD_USAGE;
    }
    return LEDGER89_CLI_CMD_USAGE;
}

int ledger89_cli_write_all(int fd, const void *data, size_t size)
{
    const unsigned char *p;
    size_t done;
    ssize_t n;

    p = (const unsigned char *)data;
    done = 0U;
    while (done < size)
    {
        n = write(fd, p + done, size - done);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
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

int ledger89_cli_read_all(int fd, char **out, size_t *out_len)
{
    char *buf;
    char *grown;
    size_t cap;
    size_t len;
    ssize_t n;

    cap = LEDGER89_CLI_CHUNK;
    buf = (char *)malloc(cap);
    if (buf == NULL)
    {
        return -1;
    }
    len = 0U;
    for (;;)
    {
        if (len == cap)
        {
            grown = (char *)realloc(buf, cap * 2U);
            if (grown == NULL)
            {
                free(buf);
                return -1;
            }
            buf = grown;
            cap *= 2U;
        }
        n = read(fd, buf + len, cap - len);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            free(buf);
            return -1;
        }
        if (n == 0)
        {
            break;
        }
        len += (size_t)n;
    }
    *out = buf;
    *out_len = len;
    return 0;
}

/* Synchronize the containing directory so a newly created pathname is
 * crash-durable. Best effort is not enough for `init`, so errors propagate. */
static int cli_sync_parent(const char *path)
{
    char dir[LEDGER89_CLI_PATH_MAX];
    const char *slash;
    size_t len;
    int fd;
    int saved;

    slash = strrchr(path, '/');
    if (slash == NULL)
    {
        strcpy(dir, ".");
    }
    else if (slash == path)
    {
        strcpy(dir, "/");
    }
    else
    {
        len = (size_t)(slash - path);
        if (len >= sizeof(dir))
        {
            return -1;
        }
        memcpy(dir, path, len);
        dir[len] = '\0';
    }
    fd = open(dir, O_RDONLY);
    if (fd < 0)
    {
        return -1;
    }
    if (fsync(fd) != 0)
    {
        saved = errno;
        (void)close(fd);
        errno = saved;
        return -1;
    }
    if (close(fd) != 0)
    {
        return -1;
    }
    return 0;
}

/* Read the immutable reserve from the 16-byte preamble. Returns 0 with
 * *reserve set, or -1 with errno (ENOENT for a missing file, EINVAL for a
 * short or malformed preamble, EOVERFLOW when it exceeds size_t). */
static int cli_discover_reserve(const char *path, size_t *reserve)
{
    unsigned char preamble[16];
    unsigned long long value;
    size_t got;
    ssize_t n;
    int fd;
    int saved;

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return -1;
    }
    got = 0U;
    while (got < sizeof(preamble))
    {
        n = read(fd, preamble + got, sizeof(preamble) - got);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            saved = errno;
            (void)close(fd);
            errno = saved;
            return -1;
        }
        if (n == 0)
        {
            break;
        }
        got += (size_t)n;
    }
    saved = errno;
    (void)close(fd);
    errno = saved;
    if (got != sizeof(preamble) || memcmp(preamble, "LEDG89F1", 8U) != 0)
    {
        errno = EINVAL;
        return -1;
    }
    value = cli_load_be(preamble + 8);
    if (cli_to_size(value, reserve) != 0)
    {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

/* Open a ledger, discovering its immutable reserve from the preamble. */
static int cli_open(ledger89 **out, const char *path, int writable)
{
    size_t reserve;

    if (cli_discover_reserve(path, &reserve) != 0)
    {
        return -1;
    }
    if (writable)
    {
        return ledger89_open_writer(out, path, reserve);
    }
    return ledger89_open_reader(out, path, reserve);
}

int ledger89_cli_init(const char *path)
{
    if (ledger89_create(path, (mode_t)0600, 0U) != 0)
    {
        return cli_fail("cannot init", path);
    }
    if (cli_sync_parent(path) != 0)
    {
        return cli_fail("cannot sync directory of", path);
    }
    return 0;
}

int ledger89_cli_append(const char *path)
{
    ledger89 *a;
    ledger89_offset offset;
    char *data;
    char digits[LEDGER89_CLI_BUF];
    size_t len;
    size_t n;

    data = NULL;
    len = 0U;
    if (ledger89_cli_read_all(STDIN_FILENO, &data, &len) != 0)
    {
        return cli_fail("cannot read stdin for", path);
    }
    if (cli_open(&a, path, 1) != 0)
    {
        free(data);
        return cli_fail("cannot append to", path);
    }
    if (ledger89_append(a, data, len, &offset) != 0)
    {
        ledger89_close(a);
        free(data);
        return cli_fail("cannot append to", path);
    }
    if (ledger89_sync(a) != 0)
    {
        ledger89_close(a);
        free(data);
        return cli_fail("cannot sync", path);
    }
    ledger89_close(a);
    free(data);
    n = ledger89_cli_format_offset(digits, offset);
    if (ledger89_cli_write_all(STDOUT_FILENO, digits, n) != 0 ||
        ledger89_cli_write_all(STDOUT_FILENO, "\n", 1U) != 0)
    {
        return cli_fail("cannot write stdout for", path);
    }
    return 0;
}

int ledger89_cli_read(const char *path, ledger89_offset offset)
{
    ledger89 *a;
    ledger89_message *message;
    ledger89_offset cursor;
    unsigned char buf[LEDGER89_CLI_CHUNK];
    ssize_t n;
    int rc;

    if (offset < LEDGER89_BEGIN)
    {
        return cli_fail("invalid offset for", path);
    }
    if (cli_open(&a, path, 0) != 0)
    {
        return cli_fail("cannot read", path);
    }
    cursor = offset;
    rc = ledger89_next(a, &cursor, &message);
    if (rc != LEDGER89_OK)
    {
        ledger89_close(a);
        return cli_fail("no complete record at offset for", path);
    }
    for (;;)
    {
        n = ledger89_message_read(message, buf, sizeof(buf));
        if (n < 0)
        {
            ledger89_message_close(message);
            ledger89_close(a);
            return cli_fail("cannot read record for", path);
        }
        if (n == 0)
        {
            break;
        }
        if (ledger89_cli_write_all(STDOUT_FILENO, buf, (size_t)n) != 0)
        {
            ledger89_message_close(message);
            ledger89_close(a);
            return cli_fail("cannot write stdout for", path);
        }
    }
    ledger89_message_close(message);
    ledger89_close(a);
    return 0;
}

int ledger89_cli_scan(const char *path, ledger89_offset offset, int has_offset)
{
    ledger89 *a;
    ledger89_message *message;
    ledger89_offset cursor;
    char digits[LEDGER89_CLI_BUF];
    size_t n;
    int rc;

    if (cli_open(&a, path, 0) != 0)
    {
        return cli_fail("cannot scan", path);
    }
    cursor = has_offset ? offset : LEDGER89_BEGIN;
    if (cursor < LEDGER89_BEGIN)
    {
        ledger89_close(a);
        return cli_fail("invalid offset for", path);
    }
    for (;;)
    {
        rc = ledger89_next(a, &cursor, &message);
        if (rc == LEDGER89_OK)
        {
            n = ledger89_cli_format_offset(digits, ledger89_message_offset(message));
            if (ledger89_cli_write_all(STDOUT_FILENO, digits, n) != 0 ||
                ledger89_cli_write_all(STDOUT_FILENO, "\t", 1U) != 0)
            {
                ledger89_message_close(message);
                ledger89_close(a);
                return cli_fail("cannot write stdout for", path);
            }
            n = ledger89_cli_format_length(digits, ledger89_message_length(message));
            ledger89_message_close(message);
            if (ledger89_cli_write_all(STDOUT_FILENO, digits, n) != 0 ||
                ledger89_cli_write_all(STDOUT_FILENO, "\n", 1U) != 0)
            {
                ledger89_close(a);
                return cli_fail("cannot write stdout for", path);
            }
            continue;
        }
        if (rc == LEDGER89_END)
        {
            break;
        }
        if (rc == LEDGER89_PARTIAL)
        {
            (void)fprintf(stderr, "ledger89: note: incomplete tail\n");
            break;
        }
        ledger89_close(a);
        return cli_fail("cannot scan", path);
    }
    ledger89_close(a);
    return 0;
}

int ledger89_cli_check(const char *path)
{
    ledger89_admin_report report;
    char digits[LEDGER89_CLI_BUF];
    size_t n;

    memset(&report, 0, sizeof(report));
    if (ledger89_admin_check(path, &report) != 0)
    {
        return cli_fail("check failed for", path);
    }
    if (report.incomplete_tail)
    {
        n = ledger89_cli_format_offset(digits, report.valid_end);
        (void)fprintf(stderr, "ledger89: incomplete tail at offset %.*s\n",
                      (int)n, digits);
        return 1;
    }
    return 0;
}

int ledger89_cli_repair(const char *path)
{
    ledger89_admin_report report;
    char digits[LEDGER89_CLI_BUF];
    size_t n;

    memset(&report, 0, sizeof(report));
    if (ledger89_admin_repair(path, &report) != 0)
    {
        return cli_fail("repair failed for", path);
    }
    if (report.incomplete_tail)
    {
        n = ledger89_cli_format_offset(digits, report.valid_end);
        (void)fprintf(stderr, "ledger89: repaired, valid end %.*s\n",
                      (int)n, digits);
    }
    return 0;
}

int ledger89_cli_tail(const char *path)
{
    ledger89 *a;
    ledger89_message *message;
    ledger89_offset cursor;
    unsigned char buf[LEDGER89_CLI_CHUNK];
    ssize_t n;
    int rc;

    if (cli_open(&a, path, 0) != 0)
    {
        return cli_fail("cannot tail", path);
    }
    /* Drain to the current frontier, discarding existing complete records. */
    cursor = LEDGER89_BEGIN;
    for (;;)
    {
        rc = ledger89_next(a, &cursor, &message);
        if (rc == LEDGER89_OK)
        {
            ledger89_message_close(message);
            continue;
        }
        if (rc == LEDGER89_END || rc == LEDGER89_PARTIAL)
        {
            break;
        }
        ledger89_close(a);
        return cli_fail("cannot tail", path);
    }
    for (;;)
    {
        rc = ledger89_next(a, &cursor, &message);
        if (rc == LEDGER89_OK)
        {
            for (;;)
            {
                n = ledger89_message_read(message, buf, sizeof(buf));
                if (n < 0)
                {
                    ledger89_message_close(message);
                    ledger89_close(a);
                    return cli_fail("cannot read record for", path);
                }
                if (n == 0)
                {
                    break;
                }
                if (ledger89_cli_write_all(STDOUT_FILENO, buf, (size_t)n) != 0)
                {
                    ledger89_message_close(message);
                    ledger89_close(a);
                    return cli_fail("cannot write stdout for", path);
                }
            }
            ledger89_message_close(message);
            continue;
        }
        if (rc == LEDGER89_END || rc == LEDGER89_PARTIAL)
        {
            struct timespec pause;
            pause.tv_sec = 0;
            pause.tv_nsec = (long)LEDGER89_CLI_POLL_MS * 1000000L;
            (void)nanosleep(&pause, NULL);
            continue;
        }
        ledger89_close(a);
        return cli_fail("cannot tail", path);
    }
}
