/* ledger89.c - record CLI for libledger89.
 *
 *     ledger89 init   <path>         create a ledger
 *     ledger89 append <path>         append all of stdin as one record
 *     ledger89 scan   <path>         print INDEX<TAB>OFFSET<TAB>LENGTH
 *     ledger89 count  <path>         print the record count (by iteration)
 *     ledger89 check  <path>         verify completeness and integrity
 *     ledger89 repair <path>         truncate an invalid tail
 *     ledger89 tail   <path>         print new records as they appear
 *
 * Not part of the library: tool/ is never compiled into libledger89.a. */

#include <stdio.h>

#include "ledger89_cli.h"

static int usage(const char *prog)
{
    (void)fprintf(stderr, "usage: %s init   <path>\n", prog);
    (void)fprintf(stderr, "       %s append <path>\n", prog);
    (void)fprintf(stderr, "       %s scan   <path>\n", prog);
    (void)fprintf(stderr, "       %s count  <path>\n", prog);
    (void)fprintf(stderr, "       %s check  <path>\n", prog);
    (void)fprintf(stderr, "       %s repair <path>\n", prog);
    (void)fprintf(stderr, "       %s tail   <path>\n", prog);
    return 2;
}

static int ledger89_cli_dispatch(ledger89_cli_cmd cmd, const char *path,
                                 const char *prog)
{
    int rc;

    switch (cmd)
    {
    case LEDGER89_CLI_CMD_INIT:
        rc = ledger89_cli_init(path);
        break;
    case LEDGER89_CLI_CMD_APPEND:
        rc = ledger89_cli_append(path);
        break;
    case LEDGER89_CLI_CMD_SCAN:
        rc = ledger89_cli_scan(path);
        break;
    case LEDGER89_CLI_CMD_COUNT:
        rc = ledger89_cli_count(path);
        break;
    case LEDGER89_CLI_CMD_CHECK:
        rc = ledger89_cli_check(path);
        break;
    case LEDGER89_CLI_CMD_REPAIR:
        rc = ledger89_cli_repair(path);
        break;
    case LEDGER89_CLI_CMD_TAIL:
        rc = ledger89_cli_tail(path);
        break;
    default:
        rc = usage(prog);
        break;
    }
    return rc;
}

int main(int argc, char **argv)
{
    ledger89_cli_cmd cmd;
    const char *path;
    int rc;

    path = NULL;
    cmd = ledger89_cli_parse(argc, argv, &path);
    rc = ledger89_cli_dispatch(cmd, path, argv[0]);
    return rc;
}
