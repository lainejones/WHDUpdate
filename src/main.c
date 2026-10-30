/* main.c - WHDUpdate: are your WHDLoad games up to date?
 *
 * This first version only checks. It reads Retroplay's list of WHDLoad
 * packs (the DAT, fetched from the pack server and kept in PROGDIR:Cache),
 * finds the game drawers in the drawers you name (or on every volume),
 * reads each game's slave for its version and says which games have a
 * newer pack. It changes nothing on your disks.
 *
 *   WHDUpdate Games:            check the games under Games:
 *   WHDUpdate ALL               check every volume
 *   WHDUpdate Games: SHOWALL    list every game, not just the ones to look at
 *   WHDUpdate Games: REPORT=RAM:whd.txt   also write a full tab-separated list
 *   WHDUpdate Games: OFFLINE    use the DAT already in PROGDIR:Cache
 *   WHDUpdate Games: FTP        fetch it by FTP (it falls back to FTP anyway
 *                               when HTTP fails)
 *
 * Return code 5 (WARN) when there are updates, 0 when there are none. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "slave.h"
#include "dat.h"
#include "match.h"
#include "amiga.h"

#define HOST        "ftp2.grandis.nu"
#define PACKDIR     "/turran/FTP/Retroplay WHDLoad Packs/"   /* web path */
#define FTPDIR      "/Retroplay WHDLoad Packs/"              /* the same by FTP */
#define SETNAME     "Commodore Amiga - WHDLoad - Games ("
#define CACHEDIR    "PROGDIR:Cache"
#define CACHE_ZIP   "PROGDIR:Cache/Games.zip"
#define CACHE_NAME  "PROGDIR:Cache/Games.name"
#define MAXZIP      (4UL * 1024 * 1024)
#define SLAVEMAX    65536L

static const char verstag[] __attribute__((used)) =
    "$VER: WHDUpdate " WHDU_VERSION " (30.10.2026) Retroplay WHDLoad pack checker";

struct Library *SocketBase = NULL;

#define TEMPLATE "DIR/M,ALL/S,SHOWALL/S,REPORT/K,OFFLINE/S,FTP/S,DEPTH/K/N"
enum { A_DIR, A_ALL, A_SHOWALL, A_REPORT, A_OFFLINE, A_FTP, A_DEPTH, A_COUNT };

typedef struct {
    PackIndex ix;
    unsigned char *slavebuf;
    int showall;
    int ftp_only;
    BPTR report;
    long games, st[6];
    long printed;
} Run;

int break_pressed(void)
{
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
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

static int parse_zip(Run *r, const unsigned char *zip, unsigned long len, char *date)
{
    int rc;
    packs_free(&r->ix);
    packs_init(&r->ix);
    rc = dat_from_zip(zip, len, add_rom, &r->ix, date);
    if (rc == 0 && r->ix.n < 100)
        rc = -1;
    if (rc == 0 && packs_finish(&r->ix))
        rc = -2;
    return rc;
}

/* Find the newest Games DAT on the server; fetch it unless the cached one
 * is it. The index page is 7 KB; the zip is only fetched when it changed. */
static int get_dat(Run *r, int offline)
{
    unsigned long len = 0, clen = 0;
    unsigned char *zip = NULL, *cname;
    char want[160], path[400], date[16];
    int rc;

    /* want = the zip's real name, "Commodore Amiga - WHDLoad - Games (2026-10-28).zip" */
    want[0] = 0;
    cname = load_file(CACHE_NAME, &clen, 200);
    if (!offline && SocketBase) {
        unsigned long ilen;
        unsigned char *index;
        const char *p, *q;
        url_encode(PACKDIR, path, sizeof(path));
        index = r->ftp_only ? NULL : http_get(HOST, path, &ilen, 256 * 1024);
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
            if (!r->ftp_only)
                printf("Trying FTP instead...\n");
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
            printf("Could not read the pack list on %s; using the saved one.\n", HOST);
    }
    if (want[0] && !(cname && !strcmp((char *)cname, want))) {
        char enc[200];
        url_encode(PACKDIR, path, sizeof(path));
        url_encode(want, enc, sizeof(enc));
        strncat(path, enc, sizeof(path) - strlen(path) - 1);
        printf("Fetching the new pack list...\n");
        zip = r->ftp_only ? NULL : http_get(HOST, path, &len, MAXZIP);
        if (!zip) {
            snprintf(path, sizeof(path), "%s%s", FTPDIR, want);
            if (!r->ftp_only)
                printf("Trying FTP instead...\n");
            zip = ftp_get(HOST, path, &len, MAXZIP);
        }
        if (zip) {
            rc = parse_zip(r, zip, len, date);
            if (rc == 0) {
                BPTR l = CreateDir((STRPTR)CACHEDIR);
                if (l)
                    UnLock(l);
                if (!save_file(CACHE_ZIP, zip, len) || !save_file(CACHE_NAME, want, strlen(want)))
                    printf("Note: could not save the pack list in %s\n", CACHEDIR);
                free(zip);
                free(cname);
                printf("Pack list of %s: %d packs.\n", date, r->ix.n);
                return 0;
            }
            printf("The downloaded pack list is damaged (%d); using the saved one.\n", rc);
            free(zip);
        }
    }
    free(cname);
    zip = load_file(CACHE_ZIP, &len, MAXZIP);
    if (!zip) {
        printf("No pack list: none saved in %s and none downloaded.\n", CACHEDIR);
        return -1;
    }
    rc = parse_zip(r, zip, len, date);
    free(zip);
    if (rc) {
        printf("The saved pack list is damaged (%d).\n", rc);
        return -1;
    }
    printf("Pack list of %s: %d packs.\n", date, r->ix.n);
    return 0;
}

/* ---- the games ------------------------------------------------------ */

static int read_slave(Run *r, const char *path, const char *slave, SlaveInfo *si)
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
    n = Read(fh, r->slavebuf, SLAVEMAX);
    Close(fh);
    return n > 0 && slave_parse(r->slavebuf, n, si);
}

static int on_game(void *ctx, const char *path, const char *name, const char *slave)
{
    Run *r = (Run *)ctx;
    SlaveInfo si;
    Match m;
    char line[700];

    if (!read_slave(r, path, slave, &si))
        memset(&si, 0, sizeof(si));
    match_game(&r->ix, name, slave, &si, &m);
    r->games++;
    r->st[m.status]++;

    if (r->showall || m.status == M_UPDATE || m.status == M_CHECK) {
        const char *iv = m.installed[0] ? m.installed : "?";
        switch (m.status) {
        case M_UPDATE:
        case M_CHECK:
        case M_NEWER:
        case M_CURRENT:
            snprintf(line, sizeof(line), "%-8s %s  %s -> %s  (%s)\n", status_word(m.status), path,
                     iv, m.newest->ver, m.newest->file);
            break;
        case M_UNKNOWN:
            snprintf(line, sizeof(line), "%-8s %s  newest pack %s\n", status_word(m.status), path,
                     m.newest->file);
            break;
        default:
            snprintf(line, sizeof(line), "%-8s %s\n", status_word(m.status), path);
        }
        fputs(line, stdout);
        fflush(stdout);
        r->printed++;
    }
    if (r->report) {
        snprintf(line, sizeof(line), "%s\t%s\t%s\t%s\t%s\t%s\n", status_word(m.status), path,
                 m.installed, m.newest ? m.newest->ver : "", m.newest ? m.newest->file : "",
                 m.newest ? m.newest->folder : "");
        FPuts(r->report, (STRPTR)line);
    }
    return break_pressed();
}

int main(void)
{
    LONG args[A_COUNT] = {0};
    struct RDArgs *rda;
    Run run;
    int rc = RETURN_OK, depth = 3, stopped = 0;

    memset(&run, 0, sizeof(run));
    packs_init(&run.ix);
    rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!rda) {
        PrintFault(IoErr(), (STRPTR)"WHDUpdate");
        return RETURN_ERROR;
    }
    if (!args[A_DIR] && !args[A_ALL]) {
        printf("WHDUpdate " WHDU_VERSION " - checks your WHDLoad games against Retroplay's packs.\n"
               "Name the drawers your games are in, or ALL for every volume:\n"
               "  WHDUpdate Games:  [SHOWALL] [REPORT=file] [OFFLINE] [FTP] [DEPTH=n]\n");
        FreeArgs(rda);
        return RETURN_WARN;
    }
    if (args[A_DEPTH])
        depth = (int)*(LONG *)args[A_DEPTH];
    run.showall = args[A_SHOWALL] != 0;
    run.ftp_only = args[A_FTP] != 0;
    run.slavebuf = (unsigned char *)malloc(SLAVEMAX);
    if (!args[A_OFFLINE]) {
        SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
        if (!SocketBase)
            printf("No TCP/IP stack running; using the saved pack list.\n");
    }
    if (!run.slavebuf || get_dat(&run, args[A_OFFLINE] != 0)) {
        rc = RETURN_FAIL;
        goto done;
    }
    if (args[A_REPORT]) {
        run.report = Open((STRPTR)args[A_REPORT], MODE_NEWFILE);
        if (!run.report)
            printf("Cannot write %s\n", (char *)args[A_REPORT]);
        else
            FPuts(run.report, (STRPTR)"status\tdrawer\tinstalled\tnewest\tpack\tfolder\n");
    }

    if (args[A_ALL]) {
        char *vols = volume_list(), *v, *w;
        for (v = vols; v && *v && !stopped; v += strlen(v) + 1) {
            for (w = vols; w < v && strcmp(w, v); w += strlen(w) + 1)
                ;
            if (w < v)
                continue;                     /* the same name twice */
            printf("Looking through %s\n", v);
            if (scan_tree(v, depth, on_game, &run) == -2)
                stopped = 1;
        }
        free(vols);
    } else {
        STRPTR *d;
        for (d = (STRPTR *)args[A_DIR]; *d && !stopped; d++) {
            int s = scan_tree((char *)*d, depth, on_game, &run);
            if (s == -1)
                printf("Cannot find %s\n", (char *)*d);
            else if (s == -2)
                stopped = 1;
        }
    }

    if (stopped)
        printf("*** Stopped.\n");
    printf("\n%ld games: %ld up to date, %ld with an update, %ld to check by hand,\n"
           "%ld without a version in the slave, %ld not in the pack list, %ld newer than the pack.\n",
           run.games, run.st[M_CURRENT], run.st[M_UPDATE], run.st[M_CHECK],
           run.st[M_UNKNOWN], run.st[M_NOMATCH], run.st[M_NEWER]);
    if (run.st[M_CHECK])
        printf("\"check\": the two versions can be read either way (1.51 vs 1.6).\n");
    if (run.st[M_UPDATE])
        rc = RETURN_WARN;

done:
    if (run.report)
        Close(run.report);
    packs_free(&run.ix);
    free(run.slavebuf);
    if (SocketBase)
        CloseLibrary(SocketBase);
    FreeArgs(rda);
    return rc;
}
