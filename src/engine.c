/* engine.c - the pack list, the scan and the updates, without any output
 * of their own beyond say(): the Shell and the window show it their way. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>
#include "slave.h"
#include "dat.h"
#include "match.h"
#include "amiga.h"
#include "update.h"
#include "engine.h"

#define HOST        "ftp2.grandis.nu"
#define PACKDIR     "/turran/FTP/Retroplay WHDLoad Packs/"   /* web path */
#define FTPDIR      "/Retroplay WHDLoad Packs/"              /* the same by FTP */
#define SETNAME     "Commodore Amiga - WHDLoad - Games ("
#define SETDIR      "Commodore_Amiga_-_WHDLoad_-_Games"
#define CACHEDIR    "PROGDIR:Cache"
#define CACHE_ZIP   "PROGDIR:Cache/Games.zip"
#define CACHE_NAME  "PROGDIR:Cache/Games.name"
#define MAXZIP      (4UL * 1024 * 1024)
#define SLAVEMAX    65536L

int engine_init(Engine *e)
{
    memset(e, 0, sizeof(*e));
    packs_init(&e->ix);
    e->slavebuf = (unsigned char *)malloc(SLAVEMAX);
    return e->slavebuf ? 0 : -1;
}

void engine_clear(Engine *e)
{
    long i;
    for (i = 0; i < e->n; i++)
        free(e->games[i]);
    e->n = 0;
    memset(e->st, 0, sizeof(e->st));
}

void engine_free(Engine *e)
{
    engine_clear(e);
    free(e->games);
    packs_free(&e->ix);
    free(e->slavebuf);
    free(e->pattern);
    memset(e, 0, sizeof(*e));
}

/* ---- the DAT -------------------------------------------------------- */

static unsigned char *load_file(const char *name, unsigned long *len, unsigned long max)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    unsigned char *buf;
    LONG size;
    *len = 0;
    if (!fh)
        return NULL;
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size <= 0 || (unsigned long)size > max || !(buf = (unsigned char *)malloc(size + 1))) {
        Close(fh);
        return NULL;
    }
    if (Read(fh, buf, size) != size) {
        Close(fh);
        free(buf);
        return NULL;
    }
    Close(fh);
    buf[size] = 0;
    *len = size;
    return buf;
}

static int save_file(const char *name, const void *data, unsigned long len)
{
    BPTR fh = Open((STRPTR)name, MODE_NEWFILE);
    int ok;
    if (!fh)
        return 0;
    ok = Write(fh, (APTR)data, len) == (LONG)len;
    Close(fh);
    if (!ok)
        DeleteFile((STRPTR)name);
    return ok;
}

static int add_rom(void *ctx, const char *folder, const char *name,
                   unsigned long size, unsigned long crc)
{
    return packs_add((PackIndex *)ctx, folder, name, size, crc) != 0;
}

static int parse_zip(Engine *e, const unsigned char *zip, unsigned long len)
{
    int rc;
    packs_free(&e->ix);
    packs_init(&e->ix);
    e->have_dat = 0;
    rc = dat_from_zip(zip, len, add_rom, &e->ix, e->datdate);
    if (rc == 0 && e->ix.n < 100)
        rc = -1;
    if (rc == 0 && packs_finish(&e->ix))
        rc = -2;
    if (rc == 0)
        e->have_dat = 1;
    return rc;
}

/* Find the newest Games DAT on the server; fetch it unless the cached one
 * is it. The index page is 7 KB; the zip is only fetched when it changed. */
int engine_dat(Engine *e, int offline)
{
    unsigned long len = 0, clen = 0;
    unsigned char *zip = NULL, *cname;
    char want[160], path[400];
    int rc;

    /* want = the zip's real name, "Commodore Amiga - WHDLoad - Games (2026-10-28).zip" */
    want[0] = 0;
    cname = load_file(CACHE_NAME, &clen, 200);
    if (!offline && SocketBase) {
        unsigned long ilen;
        unsigned char *index;
        const char *p, *q;
        say("Looking for a new pack list...");
        url_encode(PACKDIR, path, sizeof(path));
        index = e->ftp_only ? NULL : http_get(HOST, path, &ilen, 256 * 1024);
        if (index) {
            /* href="Commodore%20Amiga%20-%20WHDLoad%20-%20Games%20(2026-10-28).zip" */
            char enc[120], name[160];
            url_encode(SETNAME, enc, sizeof(enc));
            p = strstr((char *)index, enc);
            if (p && (q = strstr(p, ".zip\"")) != NULL && q - p < 150) {
                memcpy(name, p, q + 4 - p);
                name[q + 4 - p] = 0;
                url_decode(name, want, sizeof(want));
            }
            free(index);
        }
        if (!want[0]) {
            if (!e->ftp_only)
                say("Trying FTP instead...");
            index = ftp_list(HOST, FTPDIR, &ilen, 256 * 1024);
            /* one name per line: "Commodore Amiga - WHDLoad - Games (2026-10-28).zip" */
            if (index && (p = strstr((char *)index, SETNAME)) != NULL &&
                (q = strstr(p, ".zip")) != NULL && q - p < 150) {
                memcpy(want, p, q + 4 - p);
                want[q + 4 - p] = 0;
            }
            free(index);
        }
        if (!want[0])
            say("Could not read the pack list on %s; using the saved one.", HOST);
    }
    if (want[0] && !(cname && !strcmp((char *)cname, want))) {
        char enc[200];
        url_encode(PACKDIR, path, sizeof(path));
        url_encode(want, enc, sizeof(enc));
        strncat(path, enc, sizeof(path) - strlen(path) - 1);
        say("Fetching the new pack list...");
        zip = e->ftp_only ? NULL : http_get(HOST, path, &len, MAXZIP);
        if (!zip) {
            snprintf(path, sizeof(path), "%s%s", FTPDIR, want);
            if (!e->ftp_only)
                say("Trying FTP instead...");
            zip = ftp_get(HOST, path, &len, MAXZIP);
        }
        if (zip) {
            rc = parse_zip(e, zip, len);
            if (rc == 0) {
                BPTR l = CreateDir((STRPTR)CACHEDIR);
                if (l)
                    UnLock(l);
                if (!save_file(CACHE_ZIP, zip, len) || !save_file(CACHE_NAME, want, strlen(want)))
                    say("Note: could not save the pack list in %s", CACHEDIR);
                free(zip);
                free(cname);
                say("Pack list of %s: %d packs.", e->datdate, e->ix.n);
                return 0;
            }
            say("The downloaded pack list is damaged (%d); using the saved one.", rc);
            free(zip);
        }
    }
    free(cname);
    if (e->have_dat && !want[0])
        return 0;                         /* keep the one already read */
    zip = load_file(CACHE_ZIP, &len, MAXZIP);
    if (!zip) {
        say("No pack list: none saved in %s and none downloaded.", CACHEDIR);
        return -1;
    }
    rc = parse_zip(e, zip, len);
    free(zip);
    if (rc) {
        say("The saved pack list is damaged (%d).", rc);
        return -1;
    }
    say("Pack list of %s: %d packs.", e->datdate, e->ix.n);
    return 0;
}

/* ---- the games ------------------------------------------------------ */

static int read_slave(Engine *e, const char *path, const char *slave, SlaveInfo *si)
{
    char full[600];
    BPTR fh;
    LONG n;
    int l = (int)strlen(path);
    if (l + (int)strlen(slave) + 2 > (int)sizeof(full))
        return 0;
    strcpy(full, path);
    if (l && path[l - 1] != ':' && path[l - 1] != '/')
        strcat(full, "/");
    strcat(full, slave);
    fh = Open((STRPTR)full, MODE_OLDFILE);
    if (!fh)
        return 0;
    n = Read(fh, e->slavebuf, SLAVEMAX);
    Close(fh);
    return n > 0 && slave_parse(e->slavebuf, n, si);
}

static int on_game(void *ctx, const char *path, const char *name, const char *slave)
{
    Engine *e = (Engine *)ctx;
    SlaveInfo si;
    Match m;
    Game *g;
    long pl = (long)strlen(path);

    if (e->pattern && !MatchPatternNoCase((STRPTR)e->pattern, (STRPTR)name))
        return break_pressed();
    if (!read_slave(e, path, slave, &si))
        memset(&si, 0, sizeof(si));
    match_game(&e->ix, name, slave, &si, &m);

    if (e->n == e->cap) {
        long nc = e->cap ? e->cap * 2 : 512;
        Game **ng = (Game **)realloc(e->games, nc * sizeof(Game *));
        if (!ng)
            return 1;
        e->games = ng;
        e->cap = nc;
    }
    g = (Game *)malloc(sizeof(Game) + pl + 1);
    if (!g)
        return 1;
    memset(g, 0, sizeof(Game));
    g->path = (char *)(g + 1);
    strcpy(g->path, path);
    g->name = g->path + (name - path);
    g->status = m.status;
    strcpy(g->installed, m.installed);
    g->pack = m.newest;
    e->games[e->n++] = g;
    e->st[m.status]++;
    if (e->seen)
        e->seen(e->seen_ctx, g);
    if (!(e->n & 31))
        busy("Looking through the games... %ld so far", e->n);
    return break_pressed();
}

int engine_scan(Engine *e, const char *root, int depth)
{
    if (!strcmp(root, "ALL") || !strcmp(root, "all")) {
        char *vols = volume_list(), *v, *w;
        int rc = 0;
        for (v = vols; v && *v && rc != -2; v += strlen(v) + 1) {
            for (w = vols; w < v && strcmp(w, v); w += strlen(w) + 1)
                ;
            if (w < v)
                continue;                     /* the same name twice */
            say("Looking through %s", v);
            rc = scan_tree(v, depth, on_game, e) == -2 ? -2 : 0;
        }
        free(vols);
        return rc;
    }
    return scan_tree(root, depth, on_game, e);
}

BPTR engine_open_log(void)
{
    BPTR log = Open((STRPTR)LOGFILE, MODE_READWRITE);
    if (log)
        Seek(log, 0, OFFSET_END);
    return log;
}

int engine_update(Engine *e, Game *g, BPTR log)
{
    UpdateConf c;
    int rc;
    if (!g->pack)
        return -1;
    if (!SocketBase) {
        say("Updating needs the network (no bsdsocket.library).");
        return -1;
    }
    memset(&c, 0, sizeof(c));
    c.host = HOST;
    c.webdir = PACKDIR;
    c.ftpdir = FTPDIR;
    c.setdir = SETDIR;
    c.ftp_only = e->ftp_only;
    c.log = log;
    rc = update_game(&c, g->path, g->pack, g->installed);
    if (rc == 0) {
        e->st[g->status]--;
        g->status = M_CURRENT;
        e->st[M_CURRENT]++;
        strncpy(g->installed, g->pack->ver, sizeof(g->installed) - 1);
        g->marked = 0;
    }
    return rc;
}
