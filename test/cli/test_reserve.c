/* test_reserve.c - the record CLI must honor a non-default reserve. */

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ledger89_cli.h"

int main(void)
{
    char path[128];
    ledger89 *a;
    ledger89_offset off;
    int devnull;

    (void)snprintf(path, sizeof(path), "/tmp/ledger89-cli-reserve-%ld",
                   (long)getpid());
    assert(ledger89_create(path, (mode_t)0600, 64U) == 0);
    assert(ledger89_open_writer(&a, path, 64U) == 0);
    assert(ledger89_append(a, "hello", 5U, &off) == 0);
    assert(off == LEDGER89_BEGIN);
    ledger89_close(a);

    /* Read-only commands must discover the non-default reserve. */
    assert(ledger89_cli_scan(path, (ledger89_offset)0, 0) == 0);
    assert(ledger89_cli_read(path, LEDGER89_BEGIN) == 0);
    assert(ledger89_cli_check(path) == 0);

    /* append must discover the reserve of an existing ledger. */
    devnull = open("/dev/null", O_RDONLY);
    assert(devnull >= 0);
    assert(dup2(devnull, STDIN_FILENO) == STDIN_FILENO);
    (void)close(devnull);
    assert(ledger89_cli_append(path) == 0);

    assert(unlink(path) == 0);
    return 0;
}
