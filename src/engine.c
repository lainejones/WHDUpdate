/* engine.c - the settings, the pack lists, the scan and the updates, without
 * any output of their own beyond say(): the Shell and the window show it
 * their way. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "slave.h"
#include "dat.h"
#include "match.h"
#include "amiga.h"
#include "update.h"
#include "engine.h"

#define CACHEDIR    "PROGDIR:Cache"
#define PREFSNAME   "WHDUpdate.prefs"
#define MAXZIP      (4UL * 1024 * 1024)
#define SLAVEMAX    65536L

const PackSet pack_sets[NSETS] = {
    {"Games", "Commodore Amiga - WHDLoad - Games (",
     "Commodore_Amiga_-_WHDLoad_-_Games", "Games", FAM_GAMES},
    {"Games - Beta & Unofficial", "Commodore Amiga - WHDLoad - Games - Beta & Unofficial (",
     "Commodore_Amiga_-_WHDLoad_-_Games_-_Beta_&_Unofficial", "GamesBeta", FAM_GAMES},
    {"Demos", "Commodore Amiga - WHDLoad - Demos (",
     "Commodore_Amiga_-_WHDLoad_-_Demos", "Demos", FAM_DEMOS},
    {"Demos - Beta & Unofficial", "Commodore Amiga - WHDLoad - Demos - Beta & Unofficial (",
     "Commodore_Amiga_-_WHDLoad_-_Demos_-_Beta_&_Unofficial", "DemosBeta", FAM_DEMOS},
    {"Magazines", "Commodore Amiga - WHDLoad - Magazines (",
     "Commodore_Amiga_-_WHDLoad_-_Magazines", "Magazines", FAM_MAGS},
};

/* ---- files ------------------------------------------------------------ */

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

/* ---- the settings ------------------------------------------------------ */

void prefs_default(Prefs *p)
{
    memset(p, 0, sizeof(*p));
    strcpy(p->host, "ftp2.grandis.nu");
    strcpy(p->webdir, "/turran/FTP/Retroplay WHDLoad Packs/");
    strcpy(p->ftpdir, "/Retroplay WHDLoad Packs/");
    strcpy(p->user, "ftp");
    strcpy(p->pass, "amiga");
    p->sets = 1UL << SET_GAMES;
    p->allowed = variants_default();
}

static void set_str(char *dst, int size, const char *v)
{
    strncpy(dst, v, size - 1);
    dst[size - 1] = 0;
}

void prefs_load(Prefs *p)
{
    unsigned long len;
    unsigned char *buf;
    char *line, *next;
    prefs_default(p);
    /* the drawers the window used before the settings file existed */
    buf = load_file("ENV:WHDUpdate.dirs", &len, 4000);
    if (!buf)
        buf = load_file("ENVARC:WHDUpdate.dirs", &len, 4000);
    if (buf) {
        char *nl = strpbrk((char *)buf, "\r\n");
        if (nl)
            *nl = 0;
        set_str(p->dirs[FAM_GAMES], sizeof(p->dirs[0]), (char *)buf);
        free(buf);
    }
    buf = load_file("ENV:" PREFSNAME, &len, 16000);
    if (!buf)
        buf = load_file("ENVARC:" PREFSNAME, &len, 16000);
    if (!buf)
        return;
    for (line = (char *)buf; line && *line; line = next) {
        char *v;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        if ((v = strchr(line, '\r')) != NULL)
            *v = 0;
        if (!(v = strchr(line, '=')))
            continue;
        *v++ = 0;
        if (!strcmp(line, "HOST"))            set_str(p->host, sizeof(p->host), v);
        else if (!strcmp(line, "WEBDIR"))     set_str(p->webdir, sizeof(p->webdir), v);
        else if (!strcmp(line, "FTPDIR"))     set_str(p->ftpdir, sizeof(p->ftpdir), v);
        else if (!strcmp(line, "FTPUSER"))    set_str(p->user, sizeof(p->user), v);
        else if (!strcmp(line, "FTPPASS"))    set_str(p->pass, sizeof(p->pass), v);
        else if (!strcmp(line, "FTPONLY"))    p->ftp_only = atoi(v) != 0;
        else if (!strcmp(line, "SETS"))       p->sets = strtoul(v, NULL, 16);
        else if (!strcmp(line, "ALLOWED"))    p->allowed = strtoul(v, NULL, 16);
        else if (!strcmp(line, "GAMEDIRS"))   set_str(p->dirs[FAM_GAMES], sizeof(p->dirs[0]), v);
        else if (!strcmp(line, "DEMODIRS"))   set_str(p->dirs[FAM_DEMOS], sizeof(p->dirs[0]), v);
        else if (!strcmp(line, "MAGDIRS"))    set_str(p->dirs[FAM_MAGS], sizeof(p->dirs[0]), v);
    }
    free(buf);
    if (!p->sets)
        p->sets = 1UL << SET_GAMES;
}

static int prefs_write(const Prefs *p, int keep)
{
    char *buf = (char *)malloc(4096);
    int ok = 0, n;
    if (!buf)
        return 0;
    n = snprintf(buf, 4096,
                 "HOST=%s\nWEBDIR=%s\nFTPDIR=%s\nFTPUSER=%s\nFTPPASS=%s\nFTPONLY=%d\n"
                 "SETS=%lx\nALLOWED=%lx\nGAMEDIRS=%s\nDEMODIRS=%s\nMAGDIRS=%s\n",
                 p->host, p->webdir, p->ftpdir, p->user, p->pass, p->ftp_only,
                 p->sets, p->allowed, p->dirs[FAM_GAMES], p->dirs[FAM_DEMOS], p->dirs[FAM_MAGS]);
    if (n > 0 && n < 4096) {
        ok = save_file("ENV:" PREFSNAME, buf, n);
        if (keep)
            ok = save_file("ENVARC:" PREFSNAME, buf, n) && ok;
    }
    free(buf);
    return ok;
}

int prefs_save(const Prefs *p)
{
    return prefs_write(p, 1);
}

int prefs_use(const Prefs *p)
{
    return prefs_write(p, 0);
}

/* ---- the engine --------------------------------------------------------- */

int engine_init(Engine *e)
{
    memset(e, 0, sizeof(*e));
    packs_init(&e->ix);
    prefs_load(&e->prefs);
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

/* ---- the pack lists ----------------------------------------------------- */

static int cur_set;

static int add_rom(void *ctx, const char *folder, const char *name,
                   unsigned long size, unsigned long crc)
{
    PackIndex *ix = (PackIndex *)ctx;
    if (packs_add(ix, folder, name, size, crc))
        return 1;
    ix->packs[ix->n - 1].set = cur_set;
    return 0;
}

/* The zip's real name for set s in the server's folder list: HTML (a link
 * with %20 for spaces and &amp; for &) or an FTP name list. */
static int find_zipname(const char *list, int html, int s, char *want, int size)
{
    char pat[160], name[200];
    const char *zn = pack_sets[s].zipname, *p, *q;
    int n = 0;
    if (html) {
        for (; *zn && n < (int)sizeof(pat) - 8; zn++) {
            if (*zn == ' ') { memcpy(pat + n, "%20", 3); n += 3; }
            else if (*zn == '&') { memcpy(pat + n, "&amp;", 5); n += 5; }
            else pat[n++] = *zn;
        }
        pat[n] = 0;
    } else {
        set_str(pat, sizeof(pat), zn);
    }
    if (!(p = strstr(list, pat)) || !(q = strstr(p, ".zip")) || q - p > 180)
        return 0;
    memcpy(name, p, q + 4 - p);
    name[q + 4 - p] = 0;
    if (html)
        url_decode(name, want, size);
    else
        set_str(want, size, name);
    return 1;
}

static void date_of(const char *zipname, char *date)
{
    const char *p = strrchr(zipname, '(');
    if (p && strlen(p) >= 11 && strncmp(p + 1, date, 10) > 0) {
        memcpy(date, p + 1, 10);
        date[10] = 0;
    }
}

/* One set: the zip named want (fetch it unless it is the saved one), into
 * the index. 0 = added. */
static int load_set(Engine *e, int s, const char *want, int offline)
{
    const Prefs *pr = &e->prefs;
    char cname[120], czip[120], path[500], enc[300];
    unsigned long len = 0, clen;
    unsigned char *zip = NULL, *saved;
    int rc, fetched = 0;

    snprintf(cname, sizeof(cname), CACHEDIR "/%s.name", pack_sets[s].key);
    snprintf(czip, sizeof(czip), CACHEDIR "/%s.zip", pack_sets[s].key);
    saved = load_file(cname, &clen, 300);  /* (0.1-0.4 saved Games under the same names) */
    if (!offline && want[0] && !(saved && !strcmp((char *)saved, want))) {
        say("Fetching the %s pack list...", pack_sets[s].label);
        if (!pr->ftp_only) {
            url_encode(pr->webdir, path, sizeof(path));
            url_encode(want, enc, sizeof(enc));
            strncat(path, enc, sizeof(path) - strlen(path) - 1);
            zip = http_get(pr->host, path, &len, MAXZIP);
        }
        if (!zip) {
            snprintf(path, sizeof(path), "%s%s", pr->ftpdir, want);
            if (!pr->ftp_only)
                say("Trying FTP instead...");
            zip = ftp_get(pr->host, path, &len, MAXZIP);
        }
        fetched = zip != NULL;
    }
    if (!zip)
        zip = load_file(czip, &len, MAXZIP);
    if (!zip) {
        free(saved);
        say("No %s pack list: none saved and none downloaded.", pack_sets[s].label);
        return -1;
    }
    cur_set = s;
    rc = dat_from_zip(zip, len, add_rom, &e->ix, path);   /* path: its <date> */
    if (rc == 0 && fetched) {
        BPTR l = CreateDir((STRPTR)CACHEDIR);
        if (l)
            UnLock(l);
        if (!save_file(czip, zip, len) || !save_file(cname, want, strlen(want)))
            say("Note: could not save the pack list in %s", CACHEDIR);
    }
    if (rc == 0) {
        if (strlen(path) == 10 && strcmp(path, e->datdate) > 0)
            strcpy(e->datdate, path);
        else if (want[0])
            date_of(want, e->datdate);
    } else {
        say("The %s pack list is damaged (%d).", pack_sets[s].label, rc);
    }
    free(zip);
    free(saved);
    return rc;
}

int engine_dat(Engine *e, int offline)
{
    const Prefs *pr = &e->prefs;
    char *list = NULL, wants[NSETS][200];
    unsigned long len;
    int s, html = 1, loaded = 0;

    match_allowed = pr->allowed;
    ftp_login(pr->user, pr->pass);
    memset(wants, 0, sizeof(wants));
    if (!offline && SocketBase) {
        char path[400];
        say("Looking for new pack lists on %s...", pr->host);
        if (!pr->ftp_only) {
            url_encode(pr->webdir, path, sizeof(path));
            list = (char *)http_get(pr->host, path, &len, 256 * 1024);
        }
        if (!list) {
            if (!pr->ftp_only)
                say("Trying FTP instead...");
            list = (char *)ftp_list(pr->host, pr->ftpdir, &len, 256 * 1024);
            html = 0;
        }
        if (list) {
            for (s = 0; s < NSETS; s++)
                if (pr->sets & (1UL << s))
                    find_zipname(list, html, s, wants[s], sizeof(wants[s]));
            free(list);
        } else {
            say("Could not read the pack lists on %s; using the saved ones.", pr->host);
        }
    }
    packs_free(&e->ix);
    packs_init(&e->ix);
    e->have_dat = 0;
    e->dat_sets = 0;
    e->datdate[0] = 0;
    for (s = 0; s < NSETS; s++) {
        if (!(pr->sets & (1UL << s)))
            continue;
        if (load_set(e, s, wants[s], offline || !SocketBase) == 0) {
            e->dat_sets |= 1UL << s;
            loaded++;
        }
        if (break_pressed())
            return -1;
    }
    if (!loaded || packs_finish(&e->ix)) {
        say("No pack list to check against.");
        return -1;
    }
    e->have_dat = 1;
    say("Pack lists of %s: %d packs.", e->datdate[0] ? e->datdate : "?", e->ix.n);
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

/* ArchiveName.txt in the drawer (written after an update here, or by
 * WHDFetch) names the pack installed: that beats the slave's version, which
 * can lack the pack's letter (2.1 for v2.1b). */
static void installed_pack(Engine *e, const char *path, Match *m)
{
    char full[600], name[200];
    BPTR fh;
    LONG n;
    int i, l = (int)strlen(path);
    if (l + 20 > (int)sizeof(full))
        return;
    strcpy(full, path);
    if (l && path[l - 1] != ':' && path[l - 1] != '/')
        strcat(full, "/");
    strcat(full, "ArchiveName.txt");
    if (!(fh = Open((STRPTR)full, MODE_OLDFILE)))
        return;
    n = Read(fh, name, sizeof(name) - 1);
    Close(fh);
    if (n <= 0)
        return;
    name[n] = 0;
    for (i = 0; name[i] && name[i] != '\n' && name[i] != '\r'; i++)
        ;
    name[i] = 0;
    if (m->newest && !strcmp(name, m->newest->file)) {
        m->status = M_CURRENT;                    /* the newest pack is what's there */
        strncpy(m->installed, m->newest->ver, sizeof(m->installed) - 1);
        return;
    }
    for (i = 0; i < e->ix.n; i++)                 /* an older pack of this game */
        if (!strcmp(e->ix.packs[i].file, name)) {
            strncpy(m->installed, e->ix.packs[i].ver, sizeof(m->installed) - 1);
            if (m->newest) {
                int c = version_cmp(m->installed, m->newest->ver);
                m->status = c == VERSION_UNSURE ? M_CHECK : c < 0 ? M_UPDATE :
                            c > 0 ? M_NEWER : M_CURRENT;
            }
            return;
        }
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
    installed_pack(e, path, &m);

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
    g->family = e->cur_family;
    e->games[e->n++] = g;
    e->st[m.status]++;
    if (e->seen)
        e->seen(e->seen_ctx, g);
    if (!(e->n & 31))
        busy("Looking through the games... %ld so far", e->n);
    return break_pressed();
}

static unsigned long family_sets(const Engine *e, int fam)
{
    unsigned long bits = 0;
    int s;
    for (s = 0; s < NSETS; s++)
        if (fam < 0 || pack_sets[s].family == fam)
            bits |= 1UL << s;
    return bits & e->dat_sets;
}

int engine_scan(Engine *e, const char *root, int depth, int fam)
{
    e->cur_family = fam;
    match_sets = family_sets(e, fam);
    match_allowed = e->prefs.allowed;
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

int engine_scan_all(Engine *e, int depth)
{
    int f;
    for (f = 0; f < NFAMS; f++) {
        char dirs[400], *d, *next;
        if (!family_sets(e, f))
            continue;                         /* none of its sets in use */
        set_str(dirs, sizeof(dirs), e->prefs.dirs[f]);
        for (d = dirs; d && *d; d = next) {
            next = strchr(d, ';');
            if (next)
                *next++ = 0;
            while (*d == ' ')
                d++;
            if (!*d)
                continue;
            switch (engine_scan(e, d, depth, f)) {
            case -1: say("Cannot find %s", d); break;
            case -2: return -2;
            }
        }
    }
    return 0;
}

static int add_game(Engine *e, Game *g)
{
    if (e->n == e->cap) {
        long nc = e->cap ? e->cap * 2 : 512;
        Game **ng = (Game **)realloc(e->games, nc * sizeof(Game *));
        if (!ng)
            return 0;
        e->games = ng;
        e->cap = nc;
    }
    e->games[e->n++] = g;
    e->st[g->status]++;
    return 1;
}

/* The drawer name a pack unpacks to: its base and variant words, without the
 * version and the numbers - "Corporation&Missions_v1.4_2Disk_0107&0266.lha"
 * is "Corporation&Missions2Disk". */
static void pack_title(const char *file, char *out, int size)
{
    const char *p = file, *v = strstr(file, "_v");
    int o = 0;
    while (*p && p != v && *p != '.' && o < size - 1)
        out[o++] = *p++;
    if (v) {
        p = strchr(v + 1, '_');                       /* the words after the version */
        while (p && *p == '_') {
            const char *q = ++p;
            int digits = 1;
            while (*q && *q != '_' && *q != '.') {
                if (!((*q >= '0' && *q <= '9') || *q == '&'))
                    digits = 0;
                q++;
            }
            if (!digits)
                while (p < q && o < size - 1)
                    out[o++] = *p++;
            p = *q == '_' ? q : NULL;
        }
    }
    out[o] = 0;
}

static void on_missing(void *ctx, const Pack *p)
{
    Engine *e = (Engine *)ctx;
    char name[100];
    Game *g;
    pack_title(p->file, name, sizeof(name));
    g = (Game *)malloc(sizeof(Game) + strlen(name) + 1);
    if (!g)
        return;
    memset(g, 0, sizeof(Game));
    g->name = (char *)(g + 1);
    strcpy((char *)g->name, name);
    g->status = M_MISSING;
    g->pack = p;
    g->family = pack_sets[p->set].family;
    if (!add_game(e, g))
        free(g);
}

static int cmp_base(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

void engine_missing(Engine *e)
{
    const char **present;
    long i, n = 0, have = e->n;
    if (!e->have_dat)
        return;
    present = (const char **)malloc((have + 1) * sizeof(char *));
    if (!present)
        return;
    for (i = 0; i < have; i++)
        if (e->games[i]->pack)
            present[n++] = e->games[i]->pack->base;
    qsort(present, n, sizeof(char *), cmp_base);
    match_sets = e->dat_sets;
    match_allowed = e->prefs.allowed;
    packs_missing(&e->ix, present, n, on_missing, e);
    free(present);
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
    c.host = e->prefs.host;
    c.webdir = e->prefs.webdir;
    c.ftpdir = e->prefs.ftpdir;
    c.setdir = pack_sets[g->pack->set].dir;
    c.ftp_only = e->prefs.ftp_only;
    c.log = log;
    ftp_login(e->prefs.user, e->prefs.pass);
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
