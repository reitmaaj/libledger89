#include "support.h"

/* Exhaustive prefix-recovery model over multiple messages. For every cut of
 * the physical stream, recovery must retain exactly the complete-record
 * prefix predicted by the encoding, never a torn record or fabricated bytes. */

#define MODEL_RESERVE 32U
#define MODEL_CAP 8U /* MODEL_RESERVE - 24 */
#define MODEL_MSG_COUNT 6

static const size_t model_sizes[MODEL_MSG_COUNT] = {0U, 1U, 8U, 9U, 17U, 40U};

static void model_payload(unsigned char *buf, size_t size, unsigned char seed)
{
    size_t i;
    for (i = 0U; i < size; ++i)
    {
        buf[i] = (unsigned char)(seed + i * 7U);
    }
}

/* Physical stream bytes occupied by one message: per-fragment header plus
 * payload. An empty message is a single empty fragment. */
static size_t model_msg_bytes(size_t size)
{
    size_t fragments;
    fragments = size == 0U ? 1U : 1U + (size - 1U) / MODEL_CAP;
    return fragments * 24U + size;
}

/* Complete records retained when the stream ends at `cut` logical bytes. */
static int model_expected_count(off_t cut)
{
    off_t position;
    int count;
    int i;

    position = LEDGER89_BEGIN;
    count = 0;
    for (i = 0; i < MODEL_MSG_COUNT; ++i)
    {
        if (position + (off_t)model_msg_bytes(model_sizes[i]) > cut)
        {
            return count;
        }
        position += (off_t)model_msg_bytes(model_sizes[i]);
        ++count;
    }
    return count;
}

static void model_check_cut(off_t cut)
{
    char path[160];
    unsigned char buf[64];
    unsigned char expected[64];
    ledger89 *a;
    ledger89_message *m;
    ledger89_offset cursor;
    int count;
    int i;
    int fd;

    test_path(path, sizeof(path), "model", (int)cut);
    assert(ledger89_create(path, (mode_t)0600, MODEL_RESERVE) == 0);
    assert(ledger89_open_writer(&a, path, MODEL_RESERVE) == 0);
    for (i = 0; i < MODEL_MSG_COUNT; ++i)
    {
        model_payload(buf, model_sizes[i], (unsigned char)(i + 1));
        assert(ledger89_append(a, model_sizes[i] == 0U ? NULL : buf,
                               model_sizes[i], NULL) == 0);
    }
    assert(ledger89_sync(a) == 0);
    ledger89_close(a);

    fd = open(path, O_WRONLY);
    assert(fd >= 0);
    assert(ftruncate(fd, cut + (off_t)MODEL_RESERVE) == 0);
    assert(close(fd) == 0);

    assert(ledger89_open_writer(&a, path, MODEL_RESERVE) == 0);
    assert(ledger89_recover(a, NULL, NULL) == 0);
    count = model_expected_count(cut);
    cursor = LEDGER89_BEGIN;
    for (i = 0; i < count; ++i)
    {
        model_payload(expected, model_sizes[i], (unsigned char)(i + 1));
        test_expect(a, &cursor, expected, model_sizes[i]);
    }
    assert(ledger89_next(a, &cursor, &m) == LEDGER89_END);
    ledger89_close(a);
    assert(unlink(path) == 0);
}

int main(void)
{
    off_t end;
    off_t cut;
    int i;

    end = LEDGER89_BEGIN;
    for (i = 0; i < MODEL_MSG_COUNT; ++i)
    {
        end += (off_t)model_msg_bytes(model_sizes[i]);
    }
    for (cut = LEDGER89_BEGIN; cut <= end; ++cut)
    {
        model_check_cut(cut);
    }
    return 0;
}
