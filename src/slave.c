/* slave.c - read the header of a WHDLoad slave.
 *
 * A slave is an AmigaDOS executable whose code starts with
 *   moveq #-1,d0 / rts      (ws_Security, 4 bytes)
 *   "WHDLOADS"              (ws_ID)
 *   ws_Version.w, ws_Flags.w, ws_BaseMemSize.l, ws_ExecInstall.l,
 *   ws_GameLoader.w, ws_CurrentDir.w, ws_DontCache.w,
 *   ws_keydebug.b, ws_keyexit.b      (version 4+)
 *   ws_ExpMem.l                      (version 8+)
 *   ws_name.w, ws_copy.w, ws_info.w  (version 10+, offsets from the start)
 * The strings are NUL-terminated; ws_info uses $0a for a new line and $ff
 * for a line break that keeps going. Finding "WHDLOADS" is enough to locate
 * the start: it sits 4 bytes in, right after the hunk header. */
#include <string.h>
#include "slave.h"

#define OFS_NAME 0x24

static unsigned get16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* Copy the NUL-terminated string at buf[ofs] (if it lies inside buf). */
static const unsigned char *str_at(const unsigned char *buf, long len, long ofs)
{
    long i;
    if (ofs <= 0 || ofs >= len)
        return 0;
    for (i = ofs; i < len; i++)
        if (!buf[i])
            return buf + ofs;
    return 0;
}

static void copy_clean(char *dst, int size, const unsigned char *src)
{
    int n = 0;
    while (*src && n < size - 1) {
        unsigned char c = *src++;
        dst[n++] = (c < 32 || c == 0xff) ? ' ' : (char)c;
    }
    dst[n] = 0;
}

/* "Version 1.2", "version 2.06b", "V1.3" (only as a whole word). */
static void find_version(const unsigned char *s, char *out, int size)
{
    const unsigned char *p;
    out[0] = 0;
    for (p = s; *p; p++) {
        const unsigned char *q = 0;
        int n = 0;
        if ((p == s || p[-1] < 'A') &&
            (p[0] | 32) == 'v' && (p[1] | 32) == 'e' && (p[2] | 32) == 'r' &&
            (p[3] | 32) == 's' && (p[4] | 32) == 'i' && (p[5] | 32) == 'o' && (p[6] | 32) == 'n') {
            q = p + 7;
            while (*q == ' ')
                q++;
        } else if ((p == s || p[-1] == ' ' || p[-1] == '\n') && (p[0] | 32) == 'v' && is_digit(p[1])) {
            q = p + 1;
        }
        if (!q || !is_digit(*q))
            continue;
        while (n < size - 1 && (is_digit(*q) || (*q == '.' && is_digit(q[1]))))
            out[n++] = (char)*q++;
        if (n < size - 1 && *q >= 'a' && *q <= 'z' && !(q[1] >= 'a' && q[1] <= 'z'))
            out[n++] = (char)*q;
        out[n] = 0;
        return;
    }
}

/* "(16.07.2012)" or "(16.07.12)" -> "2012-07-16" */
static void find_date(const unsigned char *s, char *out)
{
    const unsigned char *p;
    out[0] = 0;
    for (p = s; *p; p++) {
        int d, m, y = 0, k;
        if (*p != '(' || !is_digit(p[1]) || !is_digit(p[2]) || p[3] != '.' ||
            !is_digit(p[4]) || !is_digit(p[5]) || p[6] != '.')
            continue;
        for (k = 7; k < 11 && is_digit(p[k]); k++)
            y = y * 10 + p[k] - '0';
        if ((k != 9 && k != 11) || p[k] != ')')
            continue;
        d = (p[1] - '0') * 10 + p[2] - '0';
        m = (p[4] - '0') * 10 + p[5] - '0';
        if (y < 100)
            y += y >= 80 ? 1900 : 2000;
        if (d >= 1 && d <= 31 && m >= 1 && m <= 12) {
            out[0] = (char)('0' + y / 1000);
            out[1] = (char)('0' + y / 100 % 10);
            out[2] = (char)('0' + y / 10 % 10);
            out[3] = (char)('0' + y % 10);
            out[4] = '-';
            out[5] = (char)('0' + m / 10);
            out[6] = (char)('0' + m % 10);
            out[7] = '-';
            out[8] = (char)('0' + d / 10);
            out[9] = (char)('0' + d % 10);
            out[10] = 0;
            return;
        }
    }
}

int slave_parse(const unsigned char *buf, long len, SlaveInfo *out)
{
    long i, start = -1;
    memset(out, 0, sizeof(*out));
    for (i = 4; i + 8 <= len && i < 2048; i++)
        if (buf[i] == 'W' && !memcmp(buf + i, "WHDLOADS", 8)) {
            start = i - 4;
            break;
        }
    if (start < 0 || start + 0x2a > len)
        return 0;
    out->ws_version = (unsigned short)get16(buf + start + 12);
    if (out->ws_version >= 10) {
        const unsigned char *s;
        long tail = len - start;
        s = str_at(buf + start, tail, get16(buf + start + OFS_NAME));
        if (s)
            copy_clean(out->name, sizeof(out->name), s);
        s = str_at(buf + start, tail, get16(buf + start + OFS_NAME + 4));
        if (s) {
            find_version(s, out->version, sizeof(out->version));
            find_date(s, out->date);
        }
    }
    return 1;
}

static long next_num(const char **p)
{
    long n = 0;
    while (is_digit(**p))
        n = n * 10 + *(*p)++ - '0';
    return n;
}

/* decimal = 1: the part after the first dot compares digit by digit, as a
 * fraction (1.51 < 1.6); 0: every part is a whole number (1.10 > 1.9). Any
 * further parts (1.2.1) are whole numbers either way. */
static int vcmp(const char *a, const char *b, int decimal)
{
    int part = 0;
    if (!*a || !*b)
        return (*a != 0) - (*b != 0);
    for (;; part++) {
        if (part == 1 && decimal) {
            while (is_digit(*a) || is_digit(*b)) {
                int x = is_digit(*a) ? *a++ - '0' : 0, y = is_digit(*b) ? *b++ - '0' : 0;
                if (x != y)
                    return x < y ? -1 : 1;
            }
        } else {
            long x = next_num(&a), y = next_num(&b);
            if (x != y)
                return x < y ? -1 : 1;
        }
        if (*a == '.' && *b == '.') {
            a++;
            b++;
            continue;
        }
        if (*a == '.')
            return 1;           /* 1.2.1 > 1.2 */
        if (*b == '.')
            return -1;
        /* trailing letter */
        if (*a != *b)
            return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
        return 0;
    }
}

int version_cmp(const char *a, const char *b)
{
    int d = vcmp(a, b, 1), w = vcmp(a, b, 0);
    return d == w ? d : VERSION_UNSURE;
}
