/* Pure-helper unit tests for the ledger89 record CLI, plus command-level
 * checks for check/repair against a crafted ledger. Links against
 * tool/ledger89_cli.c (not the library) for the helpers, and against the
 * library for ledger89_cli_check/ledger89_cli_repair. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "append89.h"
#include "ledger89.h"
#include "ledger89/admin.h"
#include "ledger89_cli.h"

#define CLI_INDEX_RESERVE 4096U

static void parse_number_ok(const char *text, unsigned long long expected)
{
    unsigned long long out;
    assert(ledger89_cli_parse_number(text, &out) == 0);
    assert(out == expected);
}

static void parse_number_bad(const char *text)
{
    unsigned long long out;
    assert(ledger89_cli_parse_number(text, &out) == -1);
}

static void test_parse_number(void)
{
    parse_number_ok("0", 0ULL);
    parse_number_ok("1", 1ULL);
    parse_number_ok("42", 42ULL);
    parse_number_ok("18446744073709551615", 18446744073709551615ULL);
    parse_number_bad(NULL);
    parse_number_bad("");
    parse_number_bad("x");
    parse_number_bad("12x");
    parse_number_bad("-1");
    parse_number_bad("+1");
    parse_number_bad("18446744073709551616"); /* overflow */
}

static void format_roundtrip(unsigned long long value)
{
    char buf[32];
    size_t n;
    unsigned long long parsed;
    n = ledger89_cli_format_number(buf, value);
    assert(n > 0U && n < sizeof(buf));
    buf[n] = '\0'; /* format_number writes no terminator; add one to parse */
    assert(ledger89_cli_parse_number(buf, &parsed) == 0);
    assert(parsed == value);
}

static void test_format_number(void)
{
    format_roundtrip(0ULL);
    format_roundtrip(1ULL);
    format_roundtrip(9ULL);
    format_roundtrip(10ULL);
    format_roundtrip(1234567890ULL);
    format_roundtrip(18446744073709551615ULL);
}

static ledger89_cli_cmd parse_args(int argc, char **argv, const char **path,
                                   unsigned long long *num, int *has)
{
    return ledger89_cli_parse(argc, argv, path, num, has);
}

static void test_parse(void)
{
    char *init[] = {"ledger89", "init", "p"};
    char *append[] = {"ledger89", "append", "p"};
    char *read_ok[] = {"ledger89", "read", "p", "5"};
    char *read_bad[] = {"ledger89", "read", "p", "x"};
    char *read_missing[] = {"ledger89", "read", "p"};
    char *extra[] = {"ledger89", "scan", "p", "x"};
    char *few[] = {"ledger89", "count"};
    const char *path;
    unsigned long long num;
    int has;

    assert(parse_args(3, init, &path, &num, &has) == LEDGER89_CLI_CMD_INIT);
    assert(strcmp(path, "p") == 0);
    assert(parse_args(3, append, &path, &num, &has) == LEDGER89_CLI_CMD_APPEND);
    assert(parse_args(4, read_ok, &path, &num, &has) == LEDGER89_CLI_CMD_READ);
    assert(has == 1 && num == 5ULL);
    assert(parse_args(4, read_bad, &path, &num, &has) ==
           LEDGER89_CLI_CMD_USAGE);
    assert(parse_args(3, read_missing, &path, &num, &has) ==
           LEDGER89_CLI_CMD_USAGE);
    assert(parse_args(4, extra, &path, &num, &has) == LEDGER89_CLI_CMD_USAGE);
    assert(parse_args(2, few, &path, &num, &has) == LEDGER89_CLI_CMD_USAGE);
}

static void test_read_write_all(void)
{
    int fds[2];
    char *buf;
    size_t len;
    assert(pipe(fds) == 0);
    assert(ledger89_cli_write_all(fds[1], "abc", 3U) == 0);
    assert(ledger89_cli_write_all(fds[1], "", 0U) == 0);
    assert(close(fds[1]) == 0);
    buf = NULL;
    len = 0U;
    assert(ledger89_cli_read_all(fds[0], &buf, &len) == 0);
    assert(len == 3U && memcmp(buf, "abc", 3U) == 0);
    free(buf);
    assert(close(fds[0]) == 0);
}

static void test_check_repair(void)
{
    char path[128];
    char index[160];
    char data[160];
    append89 *w;
    ledger89 *l;
    unsigned char partial[5] = {1U, 2U, 3U, 4U, 5U};
    (void)snprintf(path, sizeof(path), "/tmp/ledger89-cli-%ld",
                   (long)getpid());
    assert(ledger89_create(path, (mode_t)0600) == 0);
    assert(ledger89_open_writer(&l, path) == 0);
    assert(ledger89_append(l, "base", 4U, NULL) == 0);
    ledger89_close(l);
    /* Craft a partial final INDEX entry. */
    (void)snprintf(index, sizeof(index), "%s.index", path);
    assert(append89_open_writer_reserve(&w, index, (mode_t)0,
                                        CLI_INDEX_RESERVE) == 0);
    assert(append89_append(w, partial, sizeof(partial), NULL) == 0);
    append89_close(w);
    /* check reports an incomplete tail (exit 1); repair removes it (exit 0). */
    assert(ledger89_cli_check(path) == 1);
    assert(ledger89_cli_repair(path) == 0);
    assert(ledger89_cli_check(path) == 0);
    (void)snprintf(data, sizeof(data), "%s.data", path);
    assert(unlink(data) == 0);
    assert(unlink(index) == 0);
}

int main(void)
{
    test_parse_number();
    test_format_number();
    test_parse();
    test_read_write_all();
    test_check_repair();
    return 0;
}
