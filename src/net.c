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
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/dos.h>
#include "amiga.h"
#include "dat.h"

#define TIMEOUT_SECS 30
#ifndef EINPROGRESS
#define EINPROGRESS 36
#endif

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

/* The FTP login (the settings' FTPUSER / FTPPASS; Turran's is ftp / amiga). */
static char ftp_user[40] = "ftp", ftp_pass[40] = "amiga";

void ftp_login(const char *user, const char *pass)
{
    strncpy(ftp_user, user[0] ? user : "ftp", sizeof(ftp_user) - 1);
    strncpy(ftp_pass, pass, sizeof(ftp_pass) - 1);
}

/* Does s start with word (given in lower case), ignoring case? */
static int same_word(const char *s, const char *word)
{
    for (; *word; s++, word++)
        if ((*s >= 'A' && *s <= 'Z' ? *s | 32 : *s) != *word)
            return 0;
    return 1;
}

/* Wait until fd is readable (or writable) - 1, the timeout passed - 0, or
 * Ctrl-C / Stop in the window - -1. In one-second steps, so the window
 * keeps answering while the network doesn't. */
static int wait_fd(LONG fd, int write)
{
    int s;
    for (s = 0; s < TIMEOUT_SECS; s++) {
        fd_set fds;
        struct timeval tv;
        ULONG sigs = SIGBREAKF_CTRL_C;
        LONG r;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_secs = 1;
        tv.tv_micro = 0;
        r = WaitSelect(fd + 1, write ? NULL : &fds, write ? &fds : NULL, NULL, &tv, &sigs);
        if ((sigs & SIGBREAKF_CTRL_C) || break_pressed())
            return -1;
        if (r > 0)
            return 1;
        if (r < 0)
            return 0;
    }
    return 0;
}

static int wait_readable(LONG fd)
{
    return wait_fd(fd, 0);
}

static int resolve(const char *host, ULONG *addr)
{
    struct hostent *he = gethostbyname((STRPTR)host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        say("WHDUpdate: cannot find %s (no network?)", host);
        return 0;
    }
    *addr = *(ULONG *)he->h_addr_list[0];
    return 1;
}

static LONG tcp_connect(ULONG addr, int port)
{
    volatile UBYTE sabuf[16];
    LONG fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), i, one;
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
    /* Non-blocking, so a server that never answers costs TIMEOUT_SECS and
     * not the stack's own (minutes long) connect timeout. */
    one = 1;
    IoctlSocket(fd, FIONBIO, (char *)&one);
    if (connect(fd, (struct sockaddr *)sabuf, 16) < 0) {
        LONG err = 0;
        socklen_t len = sizeof(err);
        if (Errno() != EINPROGRESS || wait_fd(fd, 1) != 1 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err) {
            CloseSocket(fd);
            return -1;
        }
    }
    one = 0;
    IoctlSocket(fd, FIONBIO, (char *)&one);
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
                say("WHDUpdate: the download is larger than expected");
                goto fail;
            }
            nb = (unsigned char *)realloc(buf, nc);
            if (!nb) {
                say("WHDUpdate: out of memory while downloading");
                goto fail;
            }
            buf = nb;
            cap = nc;
        }
        w = wait_readable(fd);
        if (w < 0) {
            say("WHDUpdate: stopped");
            goto fail;
        }
        if (w == 0) {
            say("WHDUpdate: the server stopped answering");
            goto fail;
        }
        n = recv(fd, buf + have, 8192, 0);
        if (n < 0) {
            say("WHDUpdate: the connection broke");
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
        say("WHDUpdate: cannot connect to %s", host);
        return NULL;
    }
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: WHDUpdate/" WHDU_VERSION " (AmigaOS)\r\n"
             "Connection: close\r\n\r\n", path, host);
    if (!send_all(fd, req)) {
        say("WHDUpdate: sending the request failed");
        CloseSocket(fd);
        return NULL;
    }
    buf = recv_all(fd, maxlen, &have);
    CloseSocket(fd);
    if (!buf)
        return NULL;

    body = (unsigned char *)strstr((char *)buf, "\r\n\r\n");
    if (!body || strncmp((char *)buf, "HTTP/1.", 7)) {
        say("WHDUpdate: %s sent no proper reply", host);
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
        say("WHDUpdate: %s answered %ld", host, (long)status);
        goto fail;
    }
    *len = have - hdrlen;
    if (got_clen && clen != *len) {
        say("WHDUpdate: the download stopped early (%lu of %lu bytes)", *len, clen);
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
        say("WHDUpdate: cannot connect to %s by FTP", host);
        return NULL;
    }
    if (ftp_reply(cfd, reply, sizeof(reply)) != 220 ||
        ((code = ftp_cmd(cfd, "USER", ftp_user, reply, sizeof(reply))) != 331 && code != 230) ||
        (code == 331 && ftp_cmd(cfd, "PASS", ftp_pass, reply, sizeof(reply)) != 230) ||
        ftp_cmd(cfd, "TYPE", "I", reply, sizeof(reply)) != 200 ||
        ftp_cmd(cfd, "PASV", NULL, reply, sizeof(reply)) != 227) {
        say("WHDUpdate: FTP login failed: %s", reply);
        goto done;
    }
    /* 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2) */
    p = strchr(reply, '(');
    if (!p || sscanf(p + 1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        say("WHDUpdate: odd FTP passive reply: %s", reply);
        goto done;
    }
    dfd = tcp_connect((ULONG)h1 << 24 | (ULONG)h2 << 16 | (ULONG)h3 << 8 | (ULONG)h4, p1 * 256 + p2);
    if (dfd < 0) {
        say("WHDUpdate: cannot open the FTP data connection");
        goto done;
    }
    code = ftp_cmd(cfd, list ? "NLST" : "RETR", path, reply, sizeof(reply));
    if (code != 150 && code != 125) {
        say("WHDUpdate: FTP: %s", reply);
        goto done;
    }
    data = recv_all(dfd, maxlen, len);
    CloseSocket(dfd);
    dfd = -1;
    if (data && ftp_reply(cfd, reply, sizeof(reply)) != 226) {
        say("WHDUpdate: FTP transfer incomplete: %s", reply);
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

/* ---- downloads to a file ---------------------------------------------- */

/* Write what arrives on fd to fh until the other side closes; first[] is
 * data already received. Shows progress, keeps the CRC. */
static int recv_to_file(LONG fd, BPTR fh, const unsigned char *first, unsigned long nfirst,
                        unsigned long expect, unsigned long *got, unsigned long *crc)
{
    static unsigned char buf[8192];
    unsigned long shown = 0;
    *got = 0;
    *crc = 0;
    if (nfirst) {
        if (Write(fh, (APTR)first, nfirst) != (LONG)nfirst)
            return -1;
        *crc = crc32_update(*crc, first, nfirst);
        *got = nfirst;
    }
    for (;;) {
        LONG n, w = wait_readable(fd);
        if (w < 0) {
            say("WHDUpdate: stopped");
            return -1;
        }
        if (w == 0) {
            say("WHDUpdate: the server stopped answering");
            return -1;
        }
        n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            say("WHDUpdate: the connection broke");
            return -1;
        }
        if (n == 0)
            break;
        if (Write(fh, buf, n) != n) {
            say("WHDUpdate: cannot write the download (disk full?)");
            return -1;
        }
        *crc = crc32_update(*crc, buf, n);
        *got += n;
        if (expect && *got - shown >= 65536) {
            shown = *got;
            busy("Downloading: %lu of %lu KB", *got / 1024, expect / 1024);
            if (break_pressed()) {           /* Stop in the window */
                say("WHDUpdate: stopped");
                return -1;
            }
        }
    }
    return 0;
}

int http_download(const char *host, const char *path, BPTR fh,
                  unsigned long expect, unsigned long *got, unsigned long *crc)
{
    char req[768];
    unsigned char hdr[4096];
    unsigned long have = 0;
    LONG fd, status;
    ULONG addr;
    char *end;
    int rc;

    *got = 0;
    if (!resolve(host, &addr))
        return -1;
    fd = tcp_connect(addr, 80);
    if (fd < 0) {
        say("WHDUpdate: cannot connect to %s", host);
        return -1;
    }
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: WHDUpdate/" WHDU_VERSION " (AmigaOS)\r\n"
             "Connection: close\r\n\r\n", path, host);
    if (!send_all(fd, req)) {
        CloseSocket(fd);
        return -1;
    }
    /* the headers, then whatever came with them */
    for (;;) {
        LONG n;
        if (have >= sizeof(hdr) - 1 || wait_readable(fd) <= 0 ||
            (n = recv(fd, hdr + have, sizeof(hdr) - 1 - have, 0)) <= 0) {
            say("WHDUpdate: %s sent no proper reply", host);
            CloseSocket(fd);
            return -1;
        }
        have += n;
        hdr[have] = 0;
        if ((end = strstr((char *)hdr, "\r\n\r\n")) != NULL)
            break;
    }
    status = strncmp((char *)hdr, "HTTP/1.", 7) ? 0 : atol((char *)hdr + 9);
    if (status != 200) {
        say("WHDUpdate: %s answered %ld", host, (long)status);
        CloseSocket(fd);
        return -1;
    }
    end += 4;
    rc = recv_to_file(fd, fh, (unsigned char *)end, have - ((unsigned char *)end - hdr), expect, got, crc);
    CloseSocket(fd);
    return rc;
}

int ftp_download(const char *host, const char *path, BPTR fh,
                 unsigned long expect, unsigned long *got, unsigned long *crc)
{
    char reply[512];
    LONG cfd, dfd = -1;
    ULONG addr;
    int code, h1, h2, h3, h4, p1, p2, rc = -1;
    const char *p;

    *got = 0;
    if (!resolve(host, &addr))
        return -1;
    cfd = tcp_connect(addr, 21);
    if (cfd < 0) {
        say("WHDUpdate: cannot connect to %s by FTP", host);
        return -1;
    }
    reply[0] = 0;
    if (ftp_reply(cfd, reply, sizeof(reply)) != 220 ||
        ((code = ftp_cmd(cfd, "USER", ftp_user, reply, sizeof(reply))) != 331 && code != 230) ||
        (code == 331 && ftp_cmd(cfd, "PASS", ftp_pass, reply, sizeof(reply)) != 230) ||
        ftp_cmd(cfd, "TYPE", "I", reply, sizeof(reply)) != 200 ||
        ftp_cmd(cfd, "PASV", NULL, reply, sizeof(reply)) != 227 ||
        !(p = strchr(reply, '(')) ||
        sscanf(p + 1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        say("WHDUpdate: FTP login failed: %s", reply);
        goto done;
    }
    dfd = tcp_connect((ULONG)h1 << 24 | (ULONG)h2 << 16 | (ULONG)h3 << 8 | (ULONG)h4, p1 * 256 + p2);
    if (dfd < 0)
        goto done;
    code = ftp_cmd(cfd, "RETR", path, reply, sizeof(reply));
    if (code != 150 && code != 125) {
        say("WHDUpdate: FTP: %s", reply);
        goto done;
    }
    rc = recv_to_file(dfd, fh, NULL, 0, expect, got, crc);
    CloseSocket(dfd);
    dfd = -1;
    if (rc == 0 && ftp_reply(cfd, reply, sizeof(reply)) != 226)
        rc = -1;
done:
    if (dfd >= 0)
        CloseSocket(dfd);
    send_all(cfd, "QUIT\r\n");
    CloseSocket(cfd);
    return rc;
}

unsigned char *ftp_list(const char *host, const char *dir,
                        unsigned long *len, unsigned long maxlen)
{
    return ftp_fetch(host, dir, 1, len, maxlen);
}
