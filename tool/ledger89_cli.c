/* ledger89_cli.c - record-oriented helpers for the ledger89 CLI. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ledger89/admin.h"
#include "ledger89_cli.h"

#define GREEN_PURE

#define LEDGER89_CLI_CHUNK 4096U
#define LEDGER89_CLI_PATH_MAX 4096U
#define LEDGER89_CLI_BUF 32U
#define LEDGER89_CLI_POLL_MS 200

static int cli_fail(const char *what, const char *path)
{
    fprintf(stderr, "ledger89: %s '%s'\n", what, path);
    return 1;
}

static char cli_digit(unsigned long long value)
{
    return (char)('0' + (int)(value % 10ULL));
}

static void cli_digit_step(char tmp[LEDGER89_CLI_BUF], size_t *n,
                           unsigned long long *value)
{
    tmp[*n] = cli_digit(*value);
    *value = *value / 10ULL;
    *n = *n + 1U;
}

static size_t cli_digits(char tmp[LEDGER89_CLI_BUF], unsigned long long value)
{
    size_t n;

    n = 0U;
    do
    {
        cli_digit_step(tmp, &n, &value);
    } while (value != 0ULL);
    return n;
}

static void cli_store_reversed(char *buf, const char *tmp, size_t n, size_t i)
{
    buf[i] = tmp[n - 1U - i];
}

size_t ledger89_cli_format_number(char *buf, unsigned long long value)
{
    char tmp[LEDGER89_CLI_BUF];
    size_t n;
    size_t i;

    n = cli_digits(tmp, value);
    for (i = 0U; i < n; ++i)
    {
        cli_store_reversed(buf, tmp, n, i);
    }
    return n;
}

GREEN_PURE
static int cli_digit_ok(char c)
{
    if (c < '0')
    {
        return 0;
    }
    if (c > '9')
    {
        return 0;
    }
    return 1;
}

static int cli_parse_step(unsigned long long *value, char c)
{
    unsigned long long max;
    unsigned long long digit;

    if (!cli_digit_ok(c))
    {
        return -1;
    }
    max = (unsigned long long)-1;
    digit = (unsigned long long)(c - '0');
    if (*value > (max - digit) / 10ULL)
    {
        return -1;
    }
    *value = *value * 10ULL + digit;
    return 0;
}

int ledger89_cli_parse_number(const char *text, unsigned long long *out)
{
    unsigned long long value;
    size_t i;
    int rc;

    if (text == NULL)
    {
        return -1;
    }
    if (text[0] == '\0')
    {
        return -1;
    }
    value = 0ULL;
    for (i = 0U; text[i] != '\0'; ++i)
    {
        rc = cli_parse_step(&value, text[i]);
        if (rc != 0)
        {
            return -1;
        }
    }
    *out = value;
    return 0;
}

GREEN_PURE
static ledger89_cli_cmd cli_arity(int argc, int expected, ledger89_cli_cmd cmd)
{
    if (argc == expected)
    {
        return cmd;
    }
    return LEDGER89_CLI_CMD_USAGE;
}

ledger89_cli_cmd ledger89_cli_parse(int argc, char **argv, const char **path,
                                    unsigned long long *number, int *has_number)
{
    const char *cmd;
    int rc;

    *path = NULL;
    *number = 0ULL;
    *has_number = 0;
    if (argc < 3)
    {
        return LEDGER89_CLI_CMD_USAGE;
    }
    cmd = argv[1];
    *path = argv[2];
    if (strcmp(cmd, "init") == 0)
    {
        return cli_arity(argc, 3, LEDGER89_CLI_CMD_INIT);
    }
    if (strcmp(cmd, "append") == 0)
    {
        return cli_arity(argc, 3, LEDGER89_CLI_CMD_APPEND);
    }
    if (strcmp(cmd, "scan") == 0)
    {
        return cli_arity(argc, 3, LEDGER89_CLI_CMD_SCAN);
    }
    if (strcmp(cmd, "count") == 0)
    {
        return cli_arity(argc, 3, LEDGER89_CLI_CMD_COUNT);
    }
    if (strcmp(cmd, "check") == 0)
    {
        return cli_arity(argc, 3, LEDGER89_CLI_CMD_CHECK);
    }
    if (strcmp(cmd, "repair") == 0)
    {
        return cli_arity(argc, 3, LEDGER89_CLI_CMD_REPAIR);
    }
    if (strcmp(cmd, "tail") == 0)
    {
        return cli_arity(argc, 3, LEDGER89_CLI_CMD_TAIL);
    }
    if (strcmp(cmd, "read") == 0)
    {
        if (argc != 4)
        {
            return LEDGER89_CLI_CMD_USAGE;
        }
        rc = ledger89_cli_parse_number(argv[3], number);
        if (rc != 0)
        {
            return LEDGER89_CLI_CMD_USAGE;
        }
        *has_number = 1;
        return LEDGER89_CLI_CMD_READ;
    }
    return LEDGER89_CLI_CMD_USAGE;
}

static int cli_write_once(int fd, const unsigned char *p, size_t size,
                          size_t *done)
{
    ssize_t n;

    n = write(fd, p + *done, size - *done);
    if (n < 0)
    {
        if (errno == EINTR)
        {
            return 1;
        }
        return -1;
    }
    if (n == 0)
    {
        return -1;
    }
    *done = *done + (size_t)n;
    return 0;
}

int ledger89_cli_write_all(int fd, const void *data, size_t size)
{
    const unsigned char *p;
    size_t done;
    int rc;

    p = (const unsigned char *)data;
    done = 0U;
    while (done < size)
    {
        rc = cli_write_once(fd, p, size, &done);
        if (rc < 0)
        {
            return -1;
        }
    }
    return 0;
}

static int cli_grow(char **buf, size_t *cap)
{
    char *grown;

    grown = (char *)realloc(*buf, *cap * 2U);
    if (grown == NULL)
    {
        free(*buf);
        return -1;
    }
    *buf = grown;
    *cap = *cap * 2U;
    return 0;
}

static int cli_read_once(int fd, char *buf, size_t cap, size_t len,
                         size_t *len_out)
{
    ssize_t n;

    n = read(fd, buf + len, cap - len);
    if (n < 0)
    {
        if (errno == EINTR)
        {
            return 1;
        }
        return -1;
    }
    if (n == 0)
    {
        return 0;
    }
    *len_out = len + (size_t)n;
    return 2;
}

int ledger89_cli_read_all(int fd, char **out, size_t *out_len)
{
    char *buf;
    size_t cap;
    size_t len;
    int rc;

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
            rc = cli_grow(&buf, &cap);
            if (rc != 0)
            {
                return -1;
            }
        }
        rc = cli_read_once(fd, buf, cap, len, &len);
        if (rc == -1)
        {
            free(buf);
            return -1;
        }
        if (rc == 0)
        {
            break;
        }
    }
    *out = buf;
    *out_len = len;
    return 0;
}

static int cli_dirname(const char *path, const char *slash, char *dir,
                       size_t dirsize)
{
    size_t len;

    if (slash == NULL)
    {
        strcpy(dir, ".");
        return 0;
    }
    if (slash == path)
    {
        strcpy(dir, "/");
        return 0;
    }
    len = (size_t)(slash - path);
    if (len >= dirsize)
    {
        return -1;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    return 0;
}

static int cli_sync_close_fail(int fd)
{
    int saved;

    saved = errno;
    close(fd);
    errno = saved;
    return -1;
}

static int cli_sync_fd(int fd)
{
    int rc;

    rc = fsync(fd);
    if (rc != 0)
    {
        rc = cli_sync_close_fail(fd);
        return rc;
    }
    rc = close(fd);
    if (rc != 0)
    {
        return -1;
    }
    return 0;
}

/* Synchronize the containing directory so a newly created pathname is
 * crash-durable. */
static int cli_sync_parent(const char *path)
{
    char dir[LEDGER89_CLI_PATH_MAX];
    const char *slash;
    int fd;
    int rc;

    slash = strrchr(path, '/');
    rc = cli_dirname(path, slash, dir, sizeof(dir));
    if (rc != 0)
    {
        return -1;
    }
    fd = open(dir, O_RDONLY);
    if (fd < 0)
    {
        return -1;
    }
    rc = cli_sync_fd(fd);
    return rc;
}

int ledger89_cli_init(const char *path)
{
    int rc;

    rc = ledger89_create(path, (mode_t)0600);
    if (rc != 0)
    {
        rc = cli_fail("cannot init", path);
        return rc;
    }
    rc = cli_sync_parent(path);
    if (rc != 0)
    {
        rc = cli_fail("cannot sync directory of", path);
        return rc;
    }
    return 0;
}

static int cli_write_number(unsigned long long value, const char *sep)
{
    char digits[LEDGER89_CLI_BUF];
    size_t n;
    int rc;

    n = ledger89_cli_format_number(digits, value);
    rc = ledger89_cli_write_all(STDOUT_FILENO, digits, n);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_cli_write_all(STDOUT_FILENO, sep, 1U);
    if (rc != 0)
    {
        return -1;
    }
    return 0;
}

static int cli_fail_free(char *data, const char *what, const char *path)
{
    int rc;

    free(data);
    rc = cli_fail(what, path);
    return rc;
}

static int cli_fail_append(ledger89 *l, char *data, const char *path)
{
    int rc;

    ledger89_close(l);
    free(data);
    rc = cli_fail("cannot append to", path);
    return rc;
}

int ledger89_cli_append(const char *path)
{
    ledger89 *l;
    ledger89_offset offset;
    unsigned long long number;
    char *data;
    size_t len;
    int rc;

    data = NULL;
    len = 0U;
    rc = ledger89_cli_read_all(STDIN_FILENO, &data, &len);
    if (rc != 0)
    {
        rc = cli_fail("cannot read stdin for", path);
        return rc;
    }
    rc = ledger89_open_writer(&l, path);
    if (rc != 0)
    {
        rc = cli_fail_free(data, "cannot append to", path);
        return rc;
    }
    rc = ledger89_count(l, &number);
    if (rc != 0)
    {
        rc = cli_fail_append(l, data, path);
        return rc;
    }
    rc = ledger89_append(l, data, len, &offset);
    if (rc != 0)
    {
        rc = cli_fail_append(l, data, path);
        return rc;
    }
    ledger89_close(l);
    free(data);
    rc = cli_write_number(number, "\t");
    if (rc != 0)
    {
        rc = cli_fail("cannot write stdout for", path);
        return rc;
    }
    rc = cli_write_number((unsigned long long)offset, "\n");
    if (rc != 0)
    {
        rc = cli_fail("cannot write stdout for", path);
        return rc;
    }
    return 0;
}

static int cli_fail_reader(ledger89 *l, ledger89_iter *it, const char *what,
                           const char *path)
{
    int rc;

    ledger89_iter_close(it);
    ledger89_close(l);
    rc = cli_fail(what, path);
    return rc;
}

static int cli_fail_close(ledger89 *l, const char *what, const char *path)
{
    int rc;

    ledger89_close(l);
    rc = cli_fail(what, path);
    return rc;
}

static int cli_find_record(ledger89_iter *it, unsigned long long number)
{
    int rc;

    for (;;)
    {
        rc = ledger89_iter_next(it);
        if (rc == LEDGER89_OK)
        {
            if (ledger89_iter_index(it) == number)
            {
                return 0;
            }
            continue;
        }
        if (rc == LEDGER89_END)
        {
            return 1;
        }
        return -1;
    }
}

int ledger89_cli_read(const char *path, unsigned long long number)
{
    ledger89 *l;
    ledger89_iter *it;
    unsigned char buf[LEDGER89_CLI_CHUNK];
    ssize_t n;
    int rc;

    rc = ledger89_open_reader(&l, path);
    if (rc != 0)
    {
        rc = cli_fail("cannot read", path);
        return rc;
    }
    rc = ledger89_iter_begin(l, &it);
    if (rc != LEDGER89_OK)
    {
        rc = cli_fail_close(l, "cannot read", path);
        return rc;
    }
    rc = cli_find_record(it, number);
    if (rc == 1)
    {
        rc = cli_fail_reader(l, it, "no such record for", path);
        return rc;
    }
    if (rc != 0)
    {
        rc = cli_fail_reader(l, it, "cannot read record for", path);
        return rc;
    }
    for (;;)
    {
        n = ledger89_iter_read(it, buf, sizeof(buf));
        if (n < 0)
        {
            rc = cli_fail_reader(l, it, "cannot read record for", path);
            return rc;
        }
        if (n == 0)
        {
            break;
        }
        rc = ledger89_cli_write_all(STDOUT_FILENO, buf, (size_t)n);
        if (rc != 0)
        {
            rc = cli_fail_reader(l, it, "cannot write stdout for", path);
            return rc;
        }
    }
    ledger89_iter_close(it);
    ledger89_close(l);
    return 0;
}

static int cli_write_scan_line(ledger89_iter *it)
{
    size_t n;
    char digits[LEDGER89_CLI_BUF];
    int rc;

    n = ledger89_cli_format_number(digits, ledger89_iter_index(it));
    rc = ledger89_cli_write_all(STDOUT_FILENO, digits, n);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_cli_write_all(STDOUT_FILENO, "\t", 1U);
    if (rc != 0)
    {
        return -1;
    }
    n = ledger89_cli_format_number(
        digits, (unsigned long long)ledger89_iter_offset(it));
    rc = ledger89_cli_write_all(STDOUT_FILENO, digits, n);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_cli_write_all(STDOUT_FILENO, "\t", 1U);
    if (rc != 0)
    {
        return -1;
    }
    n = ledger89_cli_format_number(digits, ledger89_iter_length(it));
    rc = ledger89_cli_write_all(STDOUT_FILENO, digits, n);
    if (rc != 0)
    {
        return -1;
    }
    rc = ledger89_cli_write_all(STDOUT_FILENO, "\n", 1U);
    if (rc != 0)
    {
        return -1;
    }
    return 0;
}

static int cli_scan_loop(ledger89_iter *it)
{
    int rc;

    for (;;)
    {
        rc = ledger89_iter_next(it);
        if (rc == LEDGER89_OK)
        {
            rc = cli_write_scan_line(it);
            if (rc != 0)
            {
                return -1;
            }
            continue;
        }
        if (rc == LEDGER89_END)
        {
            return 0;
        }
        return -1;
    }
}

int ledger89_cli_scan(const char *path)
{
    ledger89 *l;
    ledger89_iter *it;
    int rc;

    rc = ledger89_open_reader(&l, path);
    if (rc != 0)
    {
        rc = cli_fail("cannot scan", path);
        return rc;
    }
    rc = ledger89_iter_begin(l, &it);
    if (rc != LEDGER89_OK)
    {
        rc = cli_fail_close(l, "cannot scan", path);
        return rc;
    }
    rc = cli_scan_loop(it);
    ledger89_iter_close(it);
    ledger89_close(l);
    if (rc != 0)
    {
        rc = cli_fail("cannot scan", path);
        return rc;
    }
    return 0;
}

int ledger89_cli_count(const char *path)
{
    ledger89 *l;
    unsigned long long number;
    int rc;

    rc = ledger89_open_reader(&l, path);
    if (rc != 0)
    {
        rc = cli_fail("cannot count", path);
        return rc;
    }
    rc = ledger89_count(l, &number);
    if (rc != 0)
    {
        rc = cli_fail_close(l, "cannot count", path);
        return rc;
    }
    ledger89_close(l);
    rc = cli_write_number(number, "\n");
    if (rc != 0)
    {
        rc = cli_fail("cannot write stdout for", path);
        return rc;
    }
    return 0;
}

static void cli_print_report(const ledger89_admin_report *report,
                             const char *tag)
{
    char digits[LEDGER89_CLI_BUF];
    size_t n;

    n = ledger89_cli_format_number(digits, report->records);
    fprintf(stderr, "ledger89: %s: %.*s records, valid end ", tag, (int)n,
            digits);
    n = ledger89_cli_format_number(digits,
                                   (unsigned long long)report->valid_end);
    fprintf(stderr, "%.*s\n", (int)n, digits);
}

int ledger89_cli_check(const char *path)
{
    ledger89_admin_report report;
    int rc;

    memset(&report, 0, sizeof(report));
    rc = ledger89_admin_check(path, &report);
    if (rc != 0)
    {
        rc = cli_fail("check failed for", path);
        return rc;
    }
    if (report.incomplete_tail)
    {
        cli_print_report(&report, "incomplete tail");
        return 1;
    }
    return 0;
}

int ledger89_cli_repair(const char *path)
{
    ledger89_admin_report report;
    int rc;

    memset(&report, 0, sizeof(report));
    rc = ledger89_admin_repair(path, &report);
    if (rc != 0)
    {
        rc = cli_fail("repair failed for", path);
        return rc;
    }
    if (report.incomplete_tail)
    {
        cli_print_report(&report, "repaired");
    }
    return 0;
}

static int cli_drain(ledger89_iter *it)
{
    int rc;

    for (;;)
    {
        rc = ledger89_iter_next(it);
        if (rc == LEDGER89_OK)
        {
            continue;
        }
        if (rc == LEDGER89_END)
        {
            return 0;
        }
        return -1;
    }
}

static int cli_emit_record(ledger89_iter *it,
                           unsigned char buf[LEDGER89_CLI_CHUNK])
{
    ssize_t n;
    int rc;

    for (;;)
    {
        n = ledger89_iter_read(it, buf, LEDGER89_CLI_CHUNK);
        if (n < 0)
        {
            return -1;
        }
        if (n == 0)
        {
            return 0;
        }
        rc = ledger89_cli_write_all(STDOUT_FILENO, buf, (size_t)n);
        if (rc != 0)
        {
            return -1;
        }
    }
}

static void cli_pause(void)
{
    struct timespec pause;

    pause.tv_sec = 0;
    pause.tv_nsec = (long)LEDGER89_CLI_POLL_MS * 1000000L;
    nanosleep(&pause, NULL);
}

int ledger89_cli_tail(const char *path)
{
    ledger89 *l;
    ledger89_iter *it;
    unsigned char buf[LEDGER89_CLI_CHUNK];
    int rc;

    rc = ledger89_open_reader(&l, path);
    if (rc != 0)
    {
        rc = cli_fail("cannot tail", path);
        return rc;
    }
    rc = ledger89_iter_begin(l, &it);
    if (rc != LEDGER89_OK)
    {
        rc = cli_fail_close(l, "cannot tail", path);
        return rc;
    }
    rc = cli_drain(it);
    if (rc != 0)
    {
        rc = cli_fail_reader(l, it, "cannot tail", path);
        return rc;
    }
    for (;;)
    {
        rc = ledger89_iter_next(it);
        if (rc == LEDGER89_OK)
        {
            rc = cli_emit_record(it, buf);
            if (rc != 0)
            {
                rc = cli_fail_reader(l, it, "cannot read record for", path);
                return rc;
            }
            continue;
        }
        if (rc == LEDGER89_END)
        {
            cli_pause();
            continue;
        }
        rc = cli_fail_reader(l, it, "cannot tail", path);
        return rc;
    }
}
