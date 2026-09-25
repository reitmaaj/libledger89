/* ledger89.c - record CLI for libledger89.
 *
 *     ledger89 init   <path>         create a ledger
 *     ledger89 append <path>         append all of stdin as one record
 *     ledger89 read   <path> <off>   write one record's payload to stdout
 *     ledger89 scan   <path> [off]   print OFFSET<TAB>LENGTH per record
 *     ledger89 check  <path>         verify completeness and integrity
 *     ledger89 repair <path>         truncate an incomplete tail
 *     ledger89 tail   <path>         print new records as they appear
 *
 * Not part of the library: tool/ is never compiled into libledger89.a. */

#include <stdio.h>

#include "ledger89_cli.h"

static int usage(const char *prog)
{
    (void)fprintf(stderr, "usage: %s init   <path>\n", prog);
    (void)fprintf(stderr, "       %s append <path>\n", prog);
    (void)fprintf(stderr, "       %s read   <path> <offset>\n", prog);
    (void)fprintf(stderr, "       %s scan   <path> [offset]\n", prog);
    (void)fprintf(stderr, "       %s check  <path>\n", prog);
    (void)fprintf(stderr, "       %s repair <path>\n", prog);
    (void)fprintf(stderr, "       %s tail   <path>\n", prog);
    return 2;
}

int main(int argc, char **argv)
{
    ledger89_cli_cmd cmd;
    ledger89_offset offset;
    const char *path;
    int has_offset;

    path = NULL;
    offset = (ledger89_offset)0;
    has_offset = 0;
    cmd = ledger89_cli_parse(argc, argv, &path, &offset, &has_offset);
    switch (cmd)
    {
    case LEDGER89_CLI_CMD_INIT:
        return ledger89_cli_init(path);
    case LEDGER89_CLI_CMD_APPEND:
        return ledger89_cli_append(path);
    case LEDGER89_CLI_CMD_READ:
        return ledger89_cli_read(path, offset);
    case LEDGER89_CLI_CMD_SCAN:
        return ledger89_cli_scan(path, offset, has_offset);
    case LEDGER89_CLI_CMD_CHECK:
        return ledger89_cli_check(path);
    case LEDGER89_CLI_CMD_REPAIR:
        return ledger89_cli_repair(path);
    case LEDGER89_CLI_CMD_TAIL:
        return ledger89_cli_tail(path);
    default:
        return usage(argv[0]);
    }
}
