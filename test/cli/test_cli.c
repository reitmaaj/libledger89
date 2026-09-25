/* test_cli.c - unit tests for the pure ledger89 CLI helpers. */

#include <assert.h>
#include <string.h>

#include "ledger89_cli.h"

static void test_parse_offset(void)
{
    ledger89_offset off;

    off = (ledger89_offset)7;
    assert(ledger89_cli_parse_offset("0", &off) == 0 && off == 0);
    assert(ledger89_cli_parse_offset("16", &off) == 0 && off == 16);
    assert(ledger89_cli_parse_offset("123456789", &off) == 0 && off == 123456789);
    assert(ledger89_cli_parse_offset("", &off) == -1);
    assert(ledger89_cli_parse_offset("-1", &off) == -1);
    assert(ledger89_cli_parse_offset("12x", &off) == -1);
    assert(ledger89_cli_parse_offset("99999999999999999999999999999999", &off) == -1);
}

static void test_format_offset(void)
{
    char buf[32];
    size_t n;

    n = ledger89_cli_format_offset(buf, (ledger89_offset)0);
    assert(n == 1 && buf[0] == '0');
    n = ledger89_cli_format_offset(buf, (ledger89_offset)16);
    assert(n == 2 && buf[0] == '1' && buf[1] == '6');
    n = ledger89_cli_format_offset(buf, (ledger89_offset)123456789);
    assert(n == 9 && memcmp(buf, "123456789", 9) == 0);
}

static void test_format_length(void)
{
    char buf[32];
    unsigned long long v;
    size_t n;

    v = 0ULL;
    n = ledger89_cli_format_length(buf, v);
    assert(n == 1 && buf[0] == '0');

    v = 4294967295ULL;
    n = ledger89_cli_format_length(buf, v);
    assert(n == 10 && memcmp(buf, "4294967295", 10) == 0);

    /* 2^32 straddles the limb boundary. */
    v = 4294967296ULL;
    n = ledger89_cli_format_length(buf, v);
    assert(n == 10 && memcmp(buf, "4294967296", 10) == 0);

    /* 2^64 - 1. */
    v = 18446744073709551615ULL;
    n = ledger89_cli_format_length(buf, v);
    assert(n == 20 && memcmp(buf, "18446744073709551615", 20) == 0);
}

static void test_parse(void)
{
    char *init_argv[3];
    char *read_argv[4];
    char *scan_argv[4];
    char *tail_argv[4];
    const char *path;
    ledger89_offset off;
    int has;
    ledger89_cli_cmd cmd;

    init_argv[0] = (char *)"ledger89";
    init_argv[1] = (char *)"init";
    init_argv[2] = (char *)"log";
    path = NULL;
    off = (ledger89_offset)0;
    has = 9;
    cmd = ledger89_cli_parse(3, init_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_INIT);
    assert(strcmp(path, "log") == 0);
    assert(has == 0);

    read_argv[0] = (char *)"ledger89";
    read_argv[1] = (char *)"read";
    read_argv[2] = (char *)"log";
    read_argv[3] = (char *)"42";
    cmd = ledger89_cli_parse(4, read_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_READ);
    assert(off == 42);

    scan_argv[0] = (char *)"ledger89";
    scan_argv[1] = (char *)"scan";
    scan_argv[2] = (char *)"log";
    cmd = ledger89_cli_parse(3, scan_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_SCAN);
    assert(has == 0);

    scan_argv[3] = (char *)"16";
    cmd = ledger89_cli_parse(4, scan_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_SCAN);
    assert(has == 1);
    assert(off == 16);

    read_argv[3] = (char *)"nope";
    cmd = ledger89_cli_parse(4, read_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_USAGE);

    tail_argv[0] = (char *)"ledger89";
    tail_argv[1] = (char *)"tail";
    tail_argv[2] = (char *)"log";
    cmd = ledger89_cli_parse(3, tail_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_TAIL);
    assert(has == 0);

    tail_argv[3] = (char *)"extra";
    cmd = ledger89_cli_parse(4, tail_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_USAGE);

    cmd = ledger89_cli_parse(2, init_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_USAGE);

    scan_argv[1] = (char *)"bogus";
    cmd = ledger89_cli_parse(3, scan_argv, &path, &off, &has);
    assert(cmd == LEDGER89_CLI_CMD_USAGE);
}

int main(void)
{
    test_parse_offset();
    test_format_offset();
    test_format_length();
    test_parse();
    return 0;
}
