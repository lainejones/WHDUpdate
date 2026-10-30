/* inflate.c - raw DEFLATE decoder (RFC 1951).
 *
 * Canonical Huffman decoding one bit at a time from per-length counts, the
 * way RFC 1951 describes it: small (no big lookup tables, which matters on
 * a 512 KB machine) and plenty fast for a DAT file. Output goes through a
 * 32 KB window that doubles as the back-reference history. */
#include <stdlib.h>
#include <string.h>
#include "inflate.h"

#define MAXBITS 15
#define WSIZE   32768u

typedef struct {
    const unsigned char *in;
    unsigned long inlen, inpos;
    unsigned long bitbuf;
    int bitcnt;
    unsigned char *win;          /* WSIZE bytes */
    unsigned long wpos;          /* bytes written since the start */
    unsigned long flushed;       /* bytes handed to the sink */
    inflate_sink sink;
    void *ctx;
    int err;
} State;

typedef struct {
    short count[MAXBITS + 1];
    short symbol[288];
} Huff;

static int bits(State *s, int need)
{
    unsigned long v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) {
            s->err = -1;
            return 0;
        }
        v |= (unsigned long)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1UL << need) - 1));
}

static int flush(State *s)
{
    unsigned long from = s->flushed % WSIZE, n = s->wpos - s->flushed;
    while (n) {
        unsigned long chunk = WSIZE - from < n ? WSIZE - from : n;
        if (s->sink(s->ctx, s->win + from, chunk)) {
            s->err = -3;
            return -1;
        }
        s->flushed += chunk;
        n -= chunk;
        from = 0;
    }
    return 0;
}

static int put(State *s, unsigned char c)
{
    s->win[s->wpos % WSIZE] = c;
    s->wpos++;
    if (s->wpos - s->flushed == WSIZE)
        return flush(s);
    return 0;
}

static int decode(State *s, const Huff *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len <= MAXBITS; len++) {
        int count;
        code |= bits(s, 1);
        if (s->err)
            return -1;
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = -1;
    return -1;
}

/* 0 = complete code, >0 = incomplete (allowed only for some), <0 = oversubscribed */
static int construct(Huff *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    int sym, len, left;
    for (len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (sym = 0; sym < n; sym++)
        h->count[length[sym]]++;
    if (h->count[0] == n)
        return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++)
        if (length[sym])
            h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static const short lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577};
static const short dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(State *s, const Huff *lencode, const Huff *distcode)
{
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (put(s, (unsigned char)sym))
                return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            int len, dsym;
            unsigned long dist;
            sym -= 257;
            if (sym >= 29)
                return s->err = -1;
            len = lbase[sym] + bits(s, lext[sym]);
            dsym = decode(s, distcode);
            if (dsym < 0 || dsym >= 30)
                return s->err = -1;
            dist = (unsigned long)dbase[dsym] + (unsigned long)bits(s, dext[dsym]);
            if (s->err || dist > s->wpos || dist > WSIZE)
                return s->err = -1;
            while (len--)
                if (put(s, s->win[(s->wpos - dist) % WSIZE]))
                    return -1;
        }
    }
}

static int stored(State *s)
{
    unsigned len;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inpos + 4 > s->inlen)
        return s->err = -1;
    len = s->in[s->inpos] | (unsigned)s->in[s->inpos + 1] << 8;
    if ((s->in[s->inpos + 2] | (unsigned)s->in[s->inpos + 3] << 8) != (~len & 0xffff))
        return s->err = -1;
    s->inpos += 4;
    if (s->inpos + len > s->inlen)
        return s->err = -1;
    while (len--)
        if (put(s, s->in[s->inpos++]))
            return -1;
    return 0;
}

static int fixed(State *s, Huff *lencode, Huff *distcode)
{
    short lengths[288];
    int i;
    for (i = 0; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    construct(lencode, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    construct(distcode, lengths, 30);
    return codes(s, lencode, distcode);
}

static int dynamic(State *s, Huff *lencode, Huff *distcode)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    int nlen, ndist, ncode, index, err;
    nlen = bits(s, 5) + 257;
    ndist = bits(s, 5) + 1;
    ncode = bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30)
        return s->err = -1;
    for (index = 0; index < ncode; index++)
        lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    if (s->err || construct(lencode, lengths, 19) != 0)
        return s->err = -1;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, lencode), len = 0, rep;
        if (symbol < 0)
            return -1;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
            continue;
        }
        if (symbol == 16) {
            if (index == 0)
                return s->err = -1;
            len = lengths[index - 1];
            rep = 3 + bits(s, 2);
        } else if (symbol == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (s->err || index + rep > nlen + ndist)
            return s->err = -1;
        while (rep--)
            lengths[index++] = (short)len;
    }
    if (lengths[256] == 0)
        return s->err = -1;
    err = construct(lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode->count[0] != 1))
        return s->err = -1;
    err = construct(distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode->count[0] != 1))
        return s->err = -1;
    return codes(s, lencode, distcode);
}

int inflate_raw(const unsigned char *in, unsigned long inlen,
                inflate_sink sink, void *ctx, unsigned long *used)
{
    State s;
    Huff *h;
    int last, type, rc = 0;

    memset(&s, 0, sizeof(s));
    s.in = in;
    s.inlen = inlen;
    s.sink = sink;
    s.ctx = ctx;
    s.win = (unsigned char *)malloc(WSIZE);
    h = (Huff *)malloc(2 * sizeof(Huff));
    if (!s.win || !h) {
        free(s.win);
        free(h);
        return -2;
    }
    do {
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (s.err)
            break;
        if (type == 0)
            stored(&s);
        else if (type == 1)
            fixed(&s, &h[0], &h[1]);
        else if (type == 2)
            dynamic(&s, &h[0], &h[1]);
        else
            s.err = -1;
    } while (!last && !s.err);
    if (!s.err)
        flush(&s);
    rc = s.err;
    if (used)
        *used = s.inpos;
    free(s.win);
    free(h);
    return rc;
}
