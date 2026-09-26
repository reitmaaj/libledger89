#ifndef LEDGER89_CLI_H
#define LEDGER89_CLI_H

/*
 * ledger89_cli.h - helpers for the ledger89 record CLI.
 *
 * Not part of the library: tool/ is never compiled into libledger89.a. The
 * CLI uses only the public ledger89 API (essentials plus the admin extension).
 */

#include <stddef.h>

#include "ledger89.h"

typedef enum
{
    LEDGER89_CLI_CMD_INIT = 0,
    LEDGER89_CLI_CMD_APPEND,
    LEDGER89_CLI_CMD_SCAN,
    LEDGER89_CLI_CMD_COUNT,
    LEDGER89_CLI_CMD_CHECK,
    LEDGER89_CLI_CMD_REPAIR,
    LEDGER89_CLI_CMD_TAIL,
    LEDGER89_CLI_CMD_USAGE
} ledger89_cli_cmd;

/* Parse argv into a command and its path. Pure. */
ledger89_cli_cmd ledger89_cli_parse(int argc, char **argv, const char **path);

/* Parse a non-negative decimal number. Return 0 or -1. */
int ledger89_cli_parse_number(const char *text, unsigned long long *out);

/* Format a non-negative integer as decimal digits into buf. Returns the digit
 * count; no NUL terminator. buf must hold at least 32 bytes. */
size_t ledger89_cli_format_number(char *buf, unsigned long long value);

/* Read all of fd into a growable buffer. Return 0, or -1 on error. */
int ledger89_cli_read_all(int fd, char **out, size_t *out_len);

/* Write exactly size bytes to fd. Return 0 or -1. */
int ledger89_cli_write_all(int fd, const void *data, size_t size);

/* Commands. Each returns a process exit status. */
int ledger89_cli_init(const char *path);
int ledger89_cli_append(const char *path);
int ledger89_cli_scan(const char *path);
int ledger89_cli_count(const char *path);
int ledger89_cli_check(const char *path);
int ledger89_cli_repair(const char *path);
int ledger89_cli_tail(const char *path);

#endif /* LEDGER89_CLI_H */
