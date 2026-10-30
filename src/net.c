/* net.c - HTTP/1.0 GET and a passive-mode FTP client over bsdsocket.library.
 *
 * The pack server answers plain HTTP, so no TLS is needed; FTP (user "ftp",
 * password "amiga") is the way in when HTTP fails. HTTP/1.0 keeps the
 * reply simple: no chunked encoding, and the server closes the connection
 * at the end of the body. Every wait is a WaitSelect() with a timeout that
 * also wakes up for Ctrl-C. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiga.h"

#define TIMEOUT_SECS 30

void url_encode(const char *in, char *out, int size)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    for (; *in && n < size - 4; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == '!' ||
            c == '(' || c == ')') {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
}

void url_decode(const char *in, char *out, int size)
{
    int n = 0;
    while (*in && n < size - 1) {
        int a, b;
        if (in[0] == '%' && in[1] && in[2]) {
            a = in[1] <= '9' ? in[1] - '0' : (in[1] | 32) - 'a' + 10;
            b = in[2] <= '9' ? in[2] - '0' : (in[2] | 32) - 'a' + 10;
            out[n++] = (char)(a << 4 | b);
            in += 3;
        } else if (!strncmp(in, "&amp;", 5)) {
            out[n++] = '&';
            in += 5;
        } else {
            out[n++] = *in++;
        }
    }
    out[n] = 0;
}

/* Does s start with word (given in lower case), ignoring case? */
static int same_word(const char *s, const char *word)
{
    for (; *word; s++, word++)
        if ((*s >= 'A' && *s <= 'Z' ? *s | 32 : *s) != *word)
            return 0;
    return 1;
}

/* Wait until fd is readable (1), the timeout passed (0) or Ctrl-C (-1). */
static int wait_readable(LONG fd)
{
    fd_set rfds;
    struct timeval tv;
    ULONG sigs = SIGBREAKF_CTRL_C;
    LONG r;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_secs = TIMEOUT_SECS;
    tv.tv_micro = 0;
    r = WaitSelect(fd + 1, &rfds, NULL, NULL, &tv, &sigs);
    if (sigs & SIGBREAKF_CTRL_C)
        return -1;
    return r > 0 ? 1 : 0;
}

static int resolve(const char *host, ULONG *addr)
{
    struct hostent *he = gethostbyname((STRPTR)host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        printf("WHDUpdate: cannot find %s (no network?)\n", host);
        return 0;
    }
    *addr = *(ULONG *)he->h_addr_list[0];
    return 1;
}

static LONG tcp_connect(ULONG addr, int port)
{
    volatile UBYTE sabuf[16];
    LONG fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), i;
    if (fd < 0)
        return -1;
    /* Byte by byte and volatile: with -O2 this compiler merges the small
     * stores of a struct sockaddr_in and loses sin_family (see AmiTime). */
    for (i = 0; i < 16; i++)
        sabuf[i] = 0;
    sabuf[1] = AF_INET;
    sabuf[2] = (UBYTE)(port >> 8);  sabuf[3] = (UBYTE)port;
    sabuf[4] = (UBYTE)(addr >> 24); sabuf[5] = (UBYTE)(addr >> 16);
    sabuf[6] = (UBYTE)(addr >> 8);  sabuf[7] = (UBYTE)addr;
    if (connect(fd, (struct sockaddr *)sabuf, 16) < 0) {
        CloseSocket(fd);
        return -1;
    }
    return fd;
}

static int send_all(LONG fd, const char *s)
{
    LONG n = (LONG)strlen(s);
    return send(fd, (APTR)s, n, 0) == n;
}

/* Read until the other side closes. Returns the data with a NUL after it. */
static unsigned char *recv_all(LONG fd, unsigned long maxlen, unsigned long *len)
{
    unsigned char *buf = NULL;
    unsigned long have = 0, cap = 0;
    *len = 0;
    for (;;) {
        LONG n, w;
        if (have + 8193 > cap) {
            unsigned long nc = cap ? cap * 2 : 65536;
            unsigned char *nb;
            if (nc > maxlen + 65536)
                nc = maxlen + 65536;
            if (have + 8193 > nc) {
                printf("WHDUpdate: the download is larger than expected\n");
                goto fail;
            }
            nb = (unsigned char *)realloc(buf, nc);
            if (!nb) {
                printf("WHDUpdate: out of memory while downloading\n");
                goto fail;
            }
            buf = nb;
            cap = nc;
        }
        w = wait_readable(fd);
        if (w < 0) {
            printf("WHDUpdate: stopped\n");
            goto fail;
        }
        if (w == 0) {
            printf("WHDUpdate: the server stopped answering\n");
            goto fail;
        }
        n = recv(fd, buf + have, 8192, 0);
        if (n < 0) {
            printf("WHDUpdate: the connection broke\n");
            goto fail;
        }
        if (n == 0)
            break;
        have += n;
    }
    buf[have] = 0;
    *len = have;
    return buf;
fail:
    free(buf);
    return NULL;
}

unsigned char *http_get(const char *host, const char *path,
                        unsigned long *len, unsigned long maxlen)
{
    char req[768];
    unsigned char *buf, *body;
    unsigned long have, hdrlen, clen = 0;
    LONG fd, i, status;
    ULONG addr;
    int got_clen = 0;

    *len = 0;
    if (!resolve(host, &addr))
        return NULL;
    fd = tcp_connect(addr, 80);
    if (fd < 0) {
        printf("WHDUpdate: cannot connect to %s\n", host);
        return NULL;
    }
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: WHDUpdate/" WHDU_VERSION " (AmigaOS)\r\n"
             "Connection: close\r\n\r\n", path, host);
    if (!send_all(fd, req)) {
        printf("WHDUpdate: sending the request failed\n");
        CloseSocket(fd);
        return NULL;
    }
    buf = recv_all(fd, maxlen, &have);
    CloseSocket(fd);
    if (!buf)
        return NULL;

    body = (unsigned char *)strstr((char *)buf, "\r\n\r\n");
    if (!body || strncmp((char *)buf, "HTTP/1.", 7)) {
        printf("WHDUpdate: %s sent no proper reply\n", host);
        goto fail;
    }
    hdrlen = body + 4 - buf;
    status = atol((char *)buf + 9);
    for (i = 0; i < (LONG)hdrlen; i++)
        if (buf[i] == '\n' && same_word((char *)buf + i + 1, "content-length:")) {
            clen = strtoul((char *)buf + i + 16, NULL, 10);
            got_clen = 1;
        }
    if (status != 200) {
        printf("WHDUpdate: %s answered %ld\n", host, (long)status);
        goto fail;
    }
    *len = have - hdrlen;
    if (got_clen && clen != *len) {
        printf("WHDUpdate: the download stopped early (%lu of %lu bytes)\n", *len, clen);
        goto fail;
    }
    memmove(buf, buf + hdrlen, *len + 1);
    return buf;
fail:
    free(buf);
    *len = 0;
    return NULL;
}

/* ---- FTP ------------------------------------------------------------ */

/* Read one reply (multi-line ones end with "NNN " on their last line).
 * Returns the code, 0 on a broken connection or a timeout. */
static int ftp_reply(LONG fd, char *last, int lastsize)
{
    char line[512];
    int n = 0, code = 0, multi = 0;
    for (;;) {
        char c;
        LONG r;
        if (wait_readable(fd) <= 0)
            return 0;
        r = recv(fd, &c, 1, 0);
        if (r <= 0)
            return 0;
        if (c != '\n') {
            if (n < (int)sizeof(line) - 1 && c != '\r')
                line[n++] = c;
            continue;
        }
        line[n] = 0;
        n = 0;
        if (line[0] >= '1' && line[0] <= '5' && line[1] >= '0' && line[2] >= '0') {
            int lc = (line[0] - '0') * 100 + (line[1] - '0') * 10 + line[2] - '0';
            if (!multi && line[3] == '-') {
                multi = 1;
                code = lc;
                continue;
            }
            if (line[3] == ' ' && (!multi || lc == code)) {
                if (last) {
                    strncpy(last, line, lastsize - 1);
                    last[lastsize - 1] = 0;
                }
                return lc;
            }
        }
    }
}

static int ftp_cmd(LONG fd, const char *cmd, const char *arg, char *last, int lastsize)
{
    char buf[600];
    snprintf(buf, sizeof(buf), arg ? "%s %s\r\n" : "%s\r\n", cmd, arg);
    if (!send_all(fd, buf))
        return 0;
    return ftp_reply(fd, last, lastsize);
}

/* RETR (list = 0) or NLST (list = 1) of path, user "ftp" / "amiga". */
static unsigned char *ftp_fetch(const char *host, const char *path, int list,
                                unsigned long *len, unsigned long maxlen)
{
    char reply[512];
    unsigned char *data = NULL;
    LONG cfd, dfd = -1;
    ULONG addr;
    int code, h1, h2, h3, h4, p1, p2;
    const char *p;

    *len = 0;
    if (!resolve(host, &addr))
        return NULL;
    cfd = tcp_connect(addr, 21);
    if (cfd < 0) {
        printf("WHDUpdate: cannot connect to %s by FTP\n", host);
        return NULL;
    }
    if (ftp_reply(cfd, reply, sizeof(reply)) != 220 ||
        ((code = ftp_cmd(cfd, "USER", "ftp", reply, sizeof(reply))) != 331 && code != 230) ||
        (code == 331 && ftp_cmd(cfd, "PASS", "amiga", reply, sizeof(reply)) != 230) ||
        ftp_cmd(cfd, "TYPE", "I", reply, sizeof(reply)) != 200 ||
        ftp_cmd(cfd, "PASV", NULL, reply, sizeof(reply)) != 227) {
        printf("WHDUpdate: FTP login failed: %s\n", reply);
        goto done;
    }
    /* 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2) */
    p = strchr(reply, '(');
    if (!p || sscanf(p + 1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        printf("WHDUpdate: odd FTP passive reply: %s\n", reply);
        goto done;
    }
    dfd = tcp_connect((ULONG)h1 << 24 | (ULONG)h2 << 16 | (ULONG)h3 << 8 | (ULONG)h4, p1 * 256 + p2);
    if (dfd < 0) {
        printf("WHDUpdate: cannot open the FTP data connection\n");
        goto done;
    }
    code = ftp_cmd(cfd, list ? "NLST" : "RETR", path, reply, sizeof(reply));
    if (code != 150 && code != 125) {
        printf("WHDUpdate: FTP: %s\n", reply);
        goto done;
    }
    data = recv_all(dfd, maxlen, len);
    CloseSocket(dfd);
    dfd = -1;
    if (data && ftp_reply(cfd, reply, sizeof(reply)) != 226) {
        printf("WHDUpdate: FTP transfer incomplete: %s\n", reply);
        free(data);
        data = NULL;
        *len = 0;
    }
done:
    if (dfd >= 0)
        CloseSocket(dfd);
    send_all(cfd, "QUIT\r\n");
    CloseSocket(cfd);
    return data;
}

unsigned char *ftp_get(const char *host, const char *path,
                       unsigned long *len, unsigned long maxlen)
{
    return ftp_fetch(host, path, 0, len, maxlen);
}

unsigned char *ftp_list(const char *host, const char *dir,
                        unsigned long *len, unsigned long maxlen)
{
    return ftp_fetch(host, dir, 1, len, maxlen);
}
