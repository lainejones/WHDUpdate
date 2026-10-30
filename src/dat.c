/* dat.c - find the .dat in a zip, unpack it and hand over each <rom>.
 *
 * The DAT puts one element per line, so it is read line by line as the
 * decoder produces it; only the attributes WHDUpdate needs are picked out.
 * The zip is read through its central directory, which has the sizes even
 * when the local headers leave them out (bit 3, sizes after the data). */
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "inflate.h"

#define LINEMAX 1024

typedef struct {
    dat_rom_fn fn;
    void *ctx;
    char line[LINEMAX];
    int len;
    int toolong;
    char folder[64];
    char *date;
    unsigned long crc;
    int stopped;
} Parser;

static unsigned get16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned long get32(const unsigned char *p)
{
    return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static unsigned long crctab[256];

unsigned long crc32_update(unsigned long crc, const unsigned char *p, unsigned long n)
{
    if (!crctab[1]) {
        unsigned long c;
        int i, k;
        for (i = 0; i < 256; i++) {
            c = (unsigned long)i;
            for (k = 0; k < 8; k++)
                c = c & 1 ? 0xedb88320UL ^ (c >> 1) : c >> 1;
            crctab[i] = c;
        }
    }
    crc = ~crc & 0xffffffffUL;
    while (n--)
        crc = crctab[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc & 0xffffffffUL;
}

/* Copy the value of attr="..." from the line into out, decoding entities. */
static int attr(const char *line, const char *name, char *out, int size)
{
    const char *p = line;
    int nl = (int)strlen(name), n = 0;
    while ((p = strstr(p, name)) != 0) {
        if ((p == line || p[-1] == ' ' || p[-1] == '\t') && p[nl] == '=' && p[nl + 1] == '"')
            break;
        p += nl;
    }
    if (!p)
        return 0;
    p += nl + 2;
    while (*p && *p != '"' && n < size - 1) {
        if (*p == '&') {
            static const char *ent[] = {"&amp;", "&", "&lt;", "<", "&gt;", ">", "&quot;", "\"", "&apos;", "'", 0};
            int i, hit = 0;
            for (i = 0; ent[i]; i += 2) {
                int el = (int)strlen(ent[i]);
                if (!strncmp(p, ent[i], el)) {
                    out[n++] = ent[i + 1][0];
                    p += el;
                    hit = 1;
                    break;
                }
            }
            if (hit)
                continue;
        }
        out[n++] = *p++;
    }
    out[n] = 0;
    return 1;
}

static unsigned long hexval(const char *s)
{
    unsigned long v = 0;
    for (; *s; s++) {
        int c = *s, d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if ((c | 32) >= 'a' && (c | 32) <= 'f') d = (c | 32) - 'a' + 10;
        else break;
        v = v << 4 | (unsigned long)d;
    }
    return v;
}

static void do_line(Parser *ps)
{
    char *l = ps->line, name[256], num[24];
    while (*l == ' ' || *l == '\t')
        l++;
    if (!strncmp(l, "<rom ", 5)) {
        unsigned long size = 0, crc = 0;
        if (!attr(l, "name", name, sizeof(name)))
            return;
        if (attr(l, "size", num, sizeof(num)))
            size = strtoul(num, 0, 10);
        if (attr(l, "crc", num, sizeof(num)))
            crc = hexval(num);
        if (ps->fn(ps->ctx, ps->folder, name, size, crc))
            ps->stopped = 1;
    } else if (!strncmp(l, "<machine ", 9) || !strncmp(l, "<game ", 6)) {
        attr(l, "name", ps->folder, sizeof(ps->folder));
    } else if (!strncmp(l, "<date>", 6) && ps->date && !ps->date[0]) {
        int n = 0;
        l += 6;
        while (*l && *l != '<' && n < 15)
            ps->date[n++] = *l++;
        ps->date[n] = 0;
    }
}

static int sink(void *ctx, const unsigned char *data, unsigned long len)
{
    Parser *ps = (Parser *)ctx;
    unsigned long i;
    ps->crc = crc32_update(ps->crc, data, len);
    for (i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            if (ps->len && !ps->toolong) {
                ps->line[ps->len] = 0;
                do_line(ps);
                if (ps->stopped)
                    return 1;
            }
            ps->len = 0;
            ps->toolong = 0;
        } else if (ps->len < LINEMAX - 1) {
            ps->line[ps->len++] = c;
        } else {
            ps->toolong = 1;
        }
    }
    return 0;
}

int dat_from_zip(const unsigned char *zip, unsigned long ziplen,
                 dat_rom_fn fn, void *ctx, char date[16])
{
    unsigned long eocd, cd, i, n, pos;
    Parser *ps;
    int rc = -1;

    if (date)
        date[0] = 0;
    if (ziplen < 22)
        return -1;
    /* end of central directory: last 22..65557 bytes */
    for (eocd = ziplen - 22;; eocd--) {
        if (get32(zip + eocd) == 0x06054b50UL)
            break;
        if (eocd == 0 || ziplen - eocd > 65557)
            return -1;
    }
    n = get16(zip + eocd + 10);
    cd = get32(zip + eocd + 16);
    for (i = 0, pos = cd; i < n && pos + 46 <= ziplen; i++) {
        unsigned method = get16(zip + pos + 10);
        unsigned long csize = get32(zip + pos + 20), crc = get32(zip + pos + 16);
        unsigned nlen = get16(zip + pos + 28), xlen = get16(zip + pos + 30), clen = get16(zip + pos + 32);
        unsigned long lofs = get32(zip + pos + 42), data;
        const char *name = (const char *)zip + pos + 46;
        if (pos + 46 + nlen > ziplen)
            return -1;
        if (nlen > 4 && !memcmp(name + nlen - 4, ".dat", 4) && (method == 8 || method == 0)) {
            if (lofs + 30 > ziplen || get32(zip + lofs) != 0x04034b50UL)
                return -1;
            data = lofs + 30 + get16(zip + lofs + 26) + get16(zip + lofs + 28);
            if (data + csize > ziplen)
                return -1;
            ps = (Parser *)calloc(1, sizeof(Parser));
            if (!ps)
                return -2;
            ps->fn = fn;
            ps->ctx = ctx;
            ps->date = date;
            if (method == 8) {
                rc = inflate_raw(zip + data, csize, sink, ps, 0);
                if (rc == -1)
                    rc = -4;
            } else {
                rc = sink(ps, zip + data, csize) ? -3 : 0;
            }
            if (rc == 0 && ps->crc != crc)
                rc = -4;                  /* unpacked, but not what was packed */
            if (rc == 0 && ps->len && !ps->toolong) {
                ps->line[ps->len] = 0;    /* a last line without a newline */
                do_line(ps);
            }
            if (rc == 0 && ps->stopped)
                rc = -3;
            free(ps);
            return rc;
        }
        pos += 46 + nlen + xlen + clen;
    }
    return -1;
}
