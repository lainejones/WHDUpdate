/* engine.c - the settings, the pack lists, the scan and the updates, without
 * any output of their own beyond say(): the Shell and the window show it
 * their way. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
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

static void cache_save(Engine *e);
static void cache_free(Engine *e);

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
    scan_errors = 0;
    free(e->dupord);              /* it points at the games */
    e->dupord = NULL;
    e->ndup = 0;
}

void engine_free(Engine *e)
{
    engine_clear(e);
    free(e->games);
    packs_free(&e->ix);
    cache_save(e);
    cache_free(e);
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

/* ---- the scan cache ------------------------------------------------- */

/* Reading 2,600 slaves takes an A500 ten minutes, so what a scan read from
 * a game drawer - the slave's facts and ArchiveName.txt's first line - is
 * kept in PROGDIR:Cache/games.txt with the files' sizes and dates, and the
 * next check reads a drawer again only when those changed. One line a
 * drawer, tab-separated: path, slave, slave size, its date (3 numbers),
 * parsed (0/1), ws_version, name, version, date, archive size (-1 = no
 * file), its date (3 numbers), archive name, and the drawer's own date (3
 * numbers; older lines lack it). A drawer whose date is unchanged is not
 * even listed: one Examine instead of a listing and two file reads. The
 * pack matching is redone every time, so a new pack list needs no new
 * scan. */
#define SCANCACHE CACHEDIR "/games.txt"

typedef struct {
    char *path, *slave;
    struct DateStamp dd;    /* the drawer's date when it was read (0 = unknown) */
    long size;
    struct DateStamp ds;
    int ok;
    SlaveInfo si;
    long asize;
    struct DateStamp ads;
    char arc[120];
    int req;                /* si has the flags, memory and Kickstart: 2 = with its CRCs */
} CacheEntry;

struct ScanCache {
    CacheEntry *e;
    long n, cap, sorted;    /* the first `sorted` entries are in path order */
    int dirty;
};

/* Shell sort: no recursion (the library's qsort recurses once per entry on
 * an ordered list, and this list comes back from the file in order - 2,600
 * levels blew the 32 KB stack). */
static void cache_sort(CacheEntry *e, long n)
{
    long gap, i, j;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            CacheEntry t = e[i];
            for (j = i; j >= gap && strcmp(e[j - gap].path, t.path) > 0; j -= gap)
                e[j] = e[j - gap];
            e[j] = t;
        }
}

static CacheEntry *cache_search(CacheEntry *e, long n, const char *path)
{
    long lo = 0, hi = n - 1;
    while (lo <= hi) {
        long mid = (lo + hi) / 2;
        int c = strcmp(e[mid].path, path);
        if (!c)
            return &e[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

static int same_date(const struct DateStamp *a, const struct DateStamp *b)
{
    return a->ds_Days == b->ds_Days && a->ds_Minute == b->ds_Minute && a->ds_Tick == b->ds_Tick;
}

/* The next tab-separated field of a line; the line is cut at the tab. */
static char *field(char **p)
{
    char *s = *p, *t;
    if (!s)
        return NULL;
    t = strchr(s, '\t');
    if (t)
        *t++ = 0;
    *p = t;
    return s;
}

static CacheEntry *cache_add(struct ScanCache *c)
{
    if (c->n == c->cap) {
        long nc = c->cap ? c->cap * 2 : 1024;
        CacheEntry *ne = (CacheEntry *)realloc(c->e, nc * sizeof(CacheEntry));
        if (!ne)
            return NULL;
        c->e = ne;
        c->cap = nc;
    }
    memset(&c->e[c->n], 0, sizeof(CacheEntry));
    return &c->e[c->n++];
}

static void cache_load(Engine *e)
{
    struct ScanCache *c;
    unsigned long len;
    unsigned char *buf;
    char *line, *next;

    if (e->cache)
        return;
    c = e->cache = (struct ScanCache *)calloc(1, sizeof(*c));
    if (!c)
        return;
    buf = load_file(SCANCACHE, &len, 2000000UL);
    if (!buf)
        return;
    for (line = (char *)buf; line && *line; line = next) {
        char *p = line, *f[24];
        CacheEntry *ce;
        int i;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        for (i = 0; i < 24; i++)
            f[i] = field(&p);
        if (!f[15] || !f[0][0] || !f[1][0])
            continue;
        ce = cache_add(c);
        if (!ce)
            break;
        ce->path = strdup(f[0]);
        ce->slave = strdup(f[1]);
        ce->size = atol(f[2]);
        ce->ds.ds_Days = atol(f[3]); ce->ds.ds_Minute = atol(f[4]); ce->ds.ds_Tick = atol(f[5]);
        ce->ok = atoi(f[6]) != 0;
        ce->si.ws_version = (unsigned short)atoi(f[7]);
        set_str(ce->si.name, sizeof(ce->si.name), f[8]);
        set_str(ce->si.version, sizeof(ce->si.version), f[9]);
        set_str(ce->si.date, sizeof(ce->si.date), f[10]);
        ce->asize = atol(f[11]);
        ce->ads.ds_Days = atol(f[12]); ce->ads.ds_Minute = atol(f[13]); ce->ads.ds_Tick = atol(f[14]);
        set_str(ce->arc, sizeof(ce->arc), f[15]);
        if (f[18]) {
            ce->dd.ds_Days = atol(f[16]); ce->dd.ds_Minute = atol(f[17]); ce->dd.ds_Tick = atol(f[18]);
        }
        if (f[22]) {                          /* what the slave asks of the machine */
            ce->si.flags = (unsigned short)atoi(f[19]);
            ce->si.basemem = (unsigned long)atol(f[20]);
            ce->si.expmem = atol(f[21]);
            set_str(ce->si.kick, sizeof(ce->si.kick), f[22]);
            ce->req = 1;
        }
        if (f[23]) {                          /* the Kickstarts' CRCs, '|' between */
            char *c = f[23];
            int k;
            for (k = 0; k < 4 && *c; k++) {
                ce->si.kickcrc[k] = (unsigned short)strtoul(c, &c, 16);
                if (*c == '|')
                    c++;
            }
            ce->req = 2;
        }
        if (!ce->path || !ce->slave) {
            free(ce->path);
            free(ce->slave);
            c->n--;
            break;
        }
    }
    free(buf);
    cache_sort(c->e, c->n);
    c->sorted = c->n;
}

static void cache_save(Engine *e)
{
    struct ScanCache *c = e->cache;
    BPTR fh;
    long i;
    if (!c || !c->dirty)
        return;
    c->dirty = 0;
    {
        BPTR l = CreateDir((STRPTR)CACHEDIR);
        if (l)
            UnLock(l);
    }
    fh = Open((STRPTR)SCANCACHE, MODE_NEWFILE);
    if (!fh)
        return;
    for (i = 0; i < c->n; i++) {
        const CacheEntry *ce = &c->e[i];
        char line[1100];
        int n = snprintf(line, sizeof(line), "%s\t%s\t%ld\t%ld\t%ld\t%ld\t%d\t%u\t%s\t%s\t%s\t%ld\t%ld\t%ld\t%ld\t%s\t%ld\t%ld\t%ld",
                         ce->path, ce->slave, ce->size, (long)ce->ds.ds_Days, (long)ce->ds.ds_Minute,
                         (long)ce->ds.ds_Tick, ce->ok, (unsigned)ce->si.ws_version, ce->si.name,
                         ce->si.version, ce->si.date, ce->asize, (long)ce->ads.ds_Days,
                         (long)ce->ads.ds_Minute, (long)ce->ads.ds_Tick, ce->arc,
                         (long)ce->dd.ds_Days, (long)ce->dd.ds_Minute, (long)ce->dd.ds_Tick);
        if (n > 0 && n < (int)sizeof(line) - 1 && ce->req)
            n += snprintf(line + n, sizeof(line) - n, "\t%u\t%lu\t%ld\t%s\t%x|%x|%x|%x", (unsigned)ce->si.flags,
                          ce->si.basemem, ce->si.expmem, ce->si.kick, ce->si.kickcrc[0], ce->si.kickcrc[1],
                          ce->si.kickcrc[2], ce->si.kickcrc[3]);
        if (n > 0 && n < (int)sizeof(line) - 1) {
            line[n++] = '\n';
            line[n] = 0;
        }
        if (!ce->slave[0] || n <= 0 || n >= (int)sizeof(line))
            continue;                         /* no slave: a removed game */
        if (Write(fh, line, n) != n)
            break;
    }
    Close(fh);
}

static void cache_free(Engine *e)
{
    struct ScanCache *c = e->cache;
    long i;
    if (!c)
        return;
    for (i = 0; i < c->n; i++) {
        free(c->e[i].path);
        free(c->e[i].slave);
    }
    free(c->e);
    free(c);
    e->cache = NULL;
}

/* The entry for a drawer: the saved one when it is still right for these
 * files, else NULL (and *slot = where the fresh facts go: the stale entry
 * or a new one). */
static CacheEntry *cache_find(Engine *e, const char *path, const char *slave,
                              const GameFiles *gf, CacheEntry **slot)
{
    struct ScanCache *c = e->cache;
    CacheEntry *ce = NULL;
    *slot = NULL;
    if (!c)
        return NULL;
    if (c->sorted)
        ce = cache_search(c->e, c->sorted, path);
    /* (a drawer added in this run is not looked for again: a scan visits
     * each drawer once, and a second visit would only add a twin line) */
    if (ce) {
        *slot = ce;
        if (!strcmp(ce->slave, slave) && ce->size == gf->slave_size && same_date(&ce->ds, &gf->slave_date) &&
            ce->asize == gf->arc_size && (gf->arc_size < 0 || same_date(&ce->ads, &gf->arc_date)))
            return ce;
        return NULL;
    }
    ce = cache_add(c);
    if (ce && !(ce->path = strdup(path))) {
        c->n--;
        ce = NULL;
    }
    *slot = ce;
    return NULL;
}

/* Keep what was just read from the drawer. */
static void cache_put(Engine *e, CacheEntry *ce, const char *slave, const GameFiles *gf,
                      int ok, const SlaveInfo *si, const char *arc)
{
    char *sl;
    if (!ce || !e->cache)
        return;
    sl = strdup(slave);
    if (!sl)
        return;
    if (ce->slave)
        free(ce->slave);
    ce->slave = sl;
    ce->dd = gf->dir_date;
    ce->size = gf->slave_size;
    ce->ds = gf->slave_date;
    ce->ok = ok;
    ce->si = *si;
    ce->asize = gf->arc_size;
    ce->ads = gf->arc_date;
    set_str(ce->arc, sizeof(ce->arc), arc ? arc : "");
    ce->req = 2;
    e->cache->dirty = 1;
}

static int read_slave(Engine *e, const char *path, const char *slave, SlaveInfo *si);

/* An entry from a cache written before 0.9 lacks what the slave asks of the
 * machine: the slave is read once more (the drawer itself needs no listing). */
static void cache_req(Engine *e, CacheEntry *ce, const char *path)
{
    SlaveInfo si;
    if (ce->req == 2 || (ce->req && !ce->si.kick[0]))
        return;                           /* (no Kickstart: no CRCs to read) */
    if (ce->ok && read_slave(e, path, ce->slave, &si)) {
        ce->si.flags = si.flags;
        ce->si.basemem = si.basemem;
        ce->si.expmem = si.expmem;
        strcpy(ce->si.kick, si.kick);
        memcpy(ce->si.kickcrc, si.kickcrc, sizeof(si.kickcrc));
    }
    ce->req = 2;
    e->cache->dirty = 1;
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
static int read_archive_name(const char *path, char *name, int size)
{
    char full[600];
    BPTR fh;
    LONG n;
    int i, l = (int)strlen(path);
    name[0] = 0;
    if (l + 20 > (int)sizeof(full))
        return 0;
    strcpy(full, path);
    if (l && path[l - 1] != ':' && path[l - 1] != '/')
        strcat(full, "/");
    strcat(full, "ArchiveName.txt");
    if (!(fh = Open((STRPTR)full, MODE_OLDFILE)))
        return 0;
    n = Read(fh, name, size - 1);
    Close(fh);
    if (n <= 0)
        return 0;
    name[n] = 0;
    for (i = 0; name[i] && name[i] != '\n' && name[i] != '\r'; i++)
        ;
    name[i] = 0;
    return 1;
}

/* name = the archive name the drawer carries (read, or from the cache). */
static void installed_pack(Engine *e, const char *name, Match *m)
{
    int i;
    if (!name[0])
        return;
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

/* A game drawer whose facts are known (read, or from the cache): match it
 * and add it to the list. */
static int add_found(Engine *e, const char *path, const char *name, const char *slave,
                     const SlaveInfo *si, const char *arc)
{
    Match m;
    Game *g;
    long pl = (long)strlen(path);

    match_game(&e->ix, name, slave, si, &m);
    installed_pack(e, arc, &m);

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
    g->wsver = si->ws_version;
    g->wsflags = si->flags;
    g->basemem = si->basemem;
    g->expmem = si->expmem;
    strcpy(g->kick, si->kick);
    memcpy(g->kickcrc, si->kickcrc, sizeof(g->kickcrc));
    g->saves = -1;
    e->games[e->n++] = g;
    e->st[m.status]++;
    if (e->seen)
        e->seen(e->seen_ctx, g);
    if (!(e->n & 31))
        busy("Looking through the games... %ld so far", e->n);
    return break_pressed();
}

static int on_game(void *ctx, const char *path, const char *name, const char *slave, const GameFiles *gf)
{
    Engine *e = (Engine *)ctx;
    SlaveInfo si;
    CacheEntry *ce, *slot;
    char arc[120];

    if (e->pattern && !MatchPatternNoCase((STRPTR)e->pattern, (STRPTR)name))
        return break_pressed();
    ce = cache_find(e, path, slave, gf, &slot);
    if (ce) {                                     /* the drawer as it was: no file to read */
        cache_req(e, ce, path);
        if (ce->ok)
            si = ce->si;
        else
            memset(&si, 0, sizeof(si));
        set_str(arc, sizeof(arc), ce->arc);
        if (!same_date(&ce->dd, &gf->dir_date)) {  /* the listing was needed: keep the new date */
            ce->dd = gf->dir_date;
            e->cache->dirty = 1;
        }
    } else {
        int ok = read_slave(e, path, slave, &si);
        if (!ok)
            memset(&si, 0, sizeof(si));
        if (gf->arc_size < 0 || !read_archive_name(path, arc, sizeof(arc)))
            arc[0] = 0;
        cache_put(e, slot, slave, gf, ok, &si, arc);
    }
    return add_found(e, path, name, slave, &si, arc);
}

/* Before a drawer is listed: a game drawer in the cache whose own date has
 * not changed is taken from the cache without a listing. */
static int on_dir(void *ctx, const char *path, const char *name, const struct DateStamp *date)
{
    Engine *e = (Engine *)ctx;
    struct ScanCache *c = e->cache;
    CacheEntry *ce;
    SlaveInfo si;

    if (!c || !c->sorted || !date->ds_Days)
        return 0;
    ce = cache_search(c->e, c->sorted, path);
    if (!ce || !ce->dd.ds_Days || !same_date(&ce->dd, date))
        return 0;
    if (e->pattern && !MatchPatternNoCase((STRPTR)e->pattern, (STRPTR)name))
        return break_pressed() ? -1 : 1;
    cache_req(e, ce, path);
    if (ce->ok)
        si = ce->si;
    else
        memset(&si, 0, sizeof(si));
    return add_found(e, path, name, ce->slave, &si, ce->arc) ? -1 : 1;
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
    int rc;
    e->cur_family = fam;
    match_sets = family_sets(e, fam);
    match_allowed = e->prefs.allowed;
    cache_load(e);
    if (!strcmp(root, "ALL") || !strcmp(root, "all")) {
        char *vols = volume_list(), *v, *w;
        rc = 0;
        for (v = vols; v && *v && rc != -2; v += strlen(v) + 1) {
            for (w = vols; w < v && strcmp(w, v); w += strlen(w) + 1)
                ;
            if (w < v)
                continue;                     /* the same name twice */
            say("Looking through %s", v);
            rc = scan_tree(v, depth, on_game, on_dir, e) == -2 ? -2 : 0;
        }
        free(vols);
    } else
        rc = scan_tree(root, depth, on_game, on_dir, e);
    cache_save(e);                            /* also after a stop: what was read stays read */
    return rc;
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
    g->saves = -1;
    g->pack = p;
    g->family = pack_sets[p->set].family;
    if (!add_game(e, g))
        free(g);
}

/* Shell sort of the base names: the library's qsort recurses once per entry
 * on ordered input (see cache_sort). */
static void sort_bases(const char **p, long n)
{
    long gap, i, j;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            const char *t = p[i];
            for (j = i; j >= gap && strcmp(p[j - gap], t) > 0; j -= gap)
                p[j] = p[j - gap];
            p[j] = t;
        }
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
    sort_bases(present, n);
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

int engine_unwanted(const Engine *e, const Game *g)
{
    return g->path && g->pack && variants_unwanted(g->pack->vbits, e->prefs.allowed);
}

int engine_remove(Engine *e, long i, int keep, BPTR log)
{
    Game *g = e->games[i];
    struct ScanCache *c = e->cache;
    CacheEntry *ce = NULL;
    long k;

    if (!g->path || remove_game(log, g->path, keep))
        return -1;
    if (c) {                                  /* not read again from the cache */
        ce = cache_search(c->e, c->sorted, g->path);
        for (k = c->sorted; !ce && k < c->n; k++)
            if (!strcmp(c->e[k].path, g->path))
                ce = &c->e[k];
        if (ce) {                             /* saved by the next scan or at the end */
            ce->slave[0] = 0;
            c->dirty = 1;
        }
    }
    free(e->dupord);                          /* engine_dupes again before it is used */
    e->dupord = NULL;
    e->ndup = 0;
    e->st[g->status]--;
    memmove(&e->games[i], &e->games[i + 1], (e->n - i - 1) * sizeof(Game *));
    e->n--;
    free(g);
    return 0;
}

/* ---- Won't run here ------------------------------------------------------ */

/* "$VER: WHDLoad 18.9 (..)" in C:WHDLoad -> 18 */
static int whdload_version(void)
{
    unsigned long len, i;
    unsigned char *b = load_file("C:WHDLoad", &len, 400000UL);
    int v = 0;
    if (!b)
        return 0;
    for (i = 0; i + 15 < len; i++)
        if (b[i] == '$' && !memcmp(b + i, "$VER: WHDLoad ", 14) && b[i + 14] >= '0' && b[i + 14] <= '9') {
            const unsigned char *q = b + i + 14;
            while (*q >= '0' && *q <= '9')
                v = v * 10 + *q++ - '0';
            break;
        }
    free(b);
    return v;
}

void engine_machine(Engine *e)
{
    struct GfxBase *gb;
    if (e->mach_known)
        return;
    /* the system keeps a little of it (2 MB shows as 2,093,056): rounded up
     * to the 512 KB the machine was built with */
    e->mach.chip = (AvailMem(MEMF_CHIP | MEMF_TOTAL) + 0x7ffffUL) & ~0x7ffffUL;
    e->mach.fast = AvailMem(MEMF_FAST | MEMF_TOTAL);
    e->mach.cpu020 = (SysBase->AttnFlags & AFF_68020) != 0;
    e->mach.aga = 0;
    if ((gb = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39)) != NULL) {
        e->mach.aga = (gb->ChipRevBits0 & GFXF_AA_ALICE) != 0;
        CloseLibrary((struct Library *)gb);
    }
    e->mach.whdload = whdload_version();
    e->mach_known = 1;
}

/* Is one of the '|'-separated Kickstart images in DEVS:Kickstarts? The
 * answers are kept: a few names serve every game. */
static struct { char name[48]; int have; } known[32];
static int nknown;                    /* names looked up in DEVS:Kickstarts */

static int kick_present(const char *list)
{
    char one[48];
    const char *p = list;
    while (*p) {
        int n = 0, k, have = -1;
        while (*p && *p != '|' && n < (int)sizeof(one) - 1)
            one[n++] = *p++;
        one[n] = 0;
        if (*p == '|')
            p++;
        for (k = 0; k < nknown; k++)
            if (!strcmp(known[k].name, one))
                have = known[k].have;
        if (have < 0) {
            char path[80];
            BPTR l;
            /* the slave names "34005.a500"; WHDLoad loads kick34005.a500 */
            snprintf(path, sizeof(path), "DEVS:Kickstarts/kick%s", one);
            l = Lock((STRPTR)path, ACCESS_READ);
            if (l)
                UnLock(l);
            have = l != 0;
            if (nknown < 32) {
                strcpy(known[nknown].name, one);
                known[nknown++].have = have;
            }
        }
        if (have)
            return 1;
    }
    return 0;
}

static void why_add(char *why, int size, const char *s)
{
    int l = (int)strlen(why);
    if (l + 3 + (int)strlen(s) < size)
        snprintf(why + l, size - l, "%s%s", l ? ", " : "", s);
}

int engine_cantrun(Engine *e, const Game *g, char *why, int size)
{
    int r = 0;
    char t[64];
    if (why)
        why[0] = 0;
    if (!g->path || !g->wsver)
        return 0;
    engine_machine(e);
    if ((g->wsflags & WHDLF_REQAGA) && !e->mach.aga) {
        r |= CANT_AGA;
        if (why) why_add(why, size, "AGA");
    }
    if ((g->wsflags & WHDLF_REQ68020) && !e->mach.cpu020) {
        r |= CANT_CPU;
        if (why) why_add(why, size, "68020");
    }
    /* the game's chip memory comes out of the machine's; WHDLoad takes the
     * whole of it over, so all of it counts */
    if (g->basemem > e->mach.chip) {
        r |= CANT_CHIP;
        snprintf(t, sizeof(t), "%lu KB chip", g->basemem / 1024);
        if (why) why_add(why, size, t);
    }
    /* expansion memory it can't do without: fast RAM, else what chip RAM
     * is left over */
    if (g->expmem > 0 && (unsigned long)g->expmem >
        e->mach.fast + (e->mach.chip > g->basemem ? e->mach.chip - g->basemem : 0)) {
        r |= CANT_MEM;
        snprintf(t, sizeof(t), "%ld KB fast", g->expmem / 1024);
        if (why) why_add(why, size, t);
    }
    if (g->kick[0] && !kick_present(g->kick)) {
        const char *bar = strchr(g->kick, '|');
        r |= CANT_KICK;
        snprintf(t, sizeof(t), "kick%.*s%s", bar ? (int)(bar - g->kick) : 40, g->kick, bar ? " (or another)" : "");
        if (why) why_add(why, size, t);
    }
    if (e->mach.whdload && g->wsver > e->mach.whdload) {
        r |= CANT_WHDL;
        snprintf(t, sizeof(t), "WHDLoad %u", (unsigned)g->wsver);
        if (why) why_add(why, size, t);
    }
    return r;
}

/* ---- Duplicates ------------------------------------------------------------ */

typedef struct {
    Game *g;
    char key[72];
} DupKey;

static int dup_cmp(const DupKey *a, const DupKey *b)
{
    int c = strcmp(a->key, b->key);
    return c ? c : strcmp(a->g->path, b->g->path);
}

long engine_dupes(Engine *e)
{
    DupKey *k;
    long i, n = 0, gap, j, group = 0;
    free(e->dupord);
    e->dupord = NULL;
    e->ndup = 0;
    for (i = 0; i < e->n; i++)
        e->games[i]->dupe = 0;
    k = (DupKey *)malloc((e->n + 1) * sizeof(DupKey));
    if (!k)
        return 0;
    for (i = 0; i < e->n; i++) {
        Game *g = e->games[i];
        if (!g->path)
            continue;
        k[n].g = g;
        /* a copy has the same drawer name somewhere else (letters and
         * digits, any case); the pack is no guide - when no pack has a
         * drawer's own name, near names match the same one
         * (AfterBurnerActivision and AfterBurnerSega) */
        norm_name(g->name, k[n].key, (int)sizeof(k[n].key));
        n++;
    }
    for (gap = n / 2; gap > 0; gap /= 2)        /* Shell sort: no recursion */
        for (i = gap; i < n; i++) {
            DupKey t = k[i];
            for (j = i; j >= gap && dup_cmp(&k[j - gap], &t) > 0; j -= gap)
                k[j] = k[j - gap];
            k[j] = t;
        }
    e->dupord = (Game **)malloc((n + 1) * sizeof(Game *));
    for (i = 0; i < n; ) {
        for (j = i + 1; j < n && !strcmp(k[j].key, k[i].key); j++)
            ;
        if (j - i > 1) {
            group++;
            for (; i < j; i++) {
                k[i].g->dupe = group;
                if (e->dupord)
                    e->dupord[e->ndup++] = k[i].g;
            }
        }
        i = j;
    }
    free(k);
    return e->ndup;
}

void engine_count_saves(Game *g)
{
    if (g->path && g->saves < 0)
        g->saves = count_saves(g->path);
}

/* ---- Export ----------------------------------------------------------------- */

/* One CSV field: quoted when it has a comma, a quote or a space at an end. */
static void csv(BPTR fh, const char *s, int last)
{
    const char *p;
    int q = 0;
    for (p = s; *p; p++)
        if (*p == ',' || *p == '"' || *p == '\n')
            q = 1;
    if (q) {
        FPutC(fh, '"');
        for (p = s; *p; p++) {
            if (*p == '"')
                FPutC(fh, '"');
            FPutC(fh, *p);
        }
        FPutC(fh, '"');
    } else
        FPuts(fh, (STRPTR)s);
    FPutC(fh, last ? '\n' : ',');
}

/* The language (sys 0) or system (sys 1) words of a pack, '+' between. */
static void variant_labels(const Pack *p, int sys, char *out, int size)
{
    int i, o = 0;
    const char *prev = "";
    out[0] = 0;
    if (!p)
        return;
    for (i = 0; variant_words[i].word; i++)
        if (variant_words[i].system == sys && (p->vbits >> i & 1) && strcmp(prev, variant_words[i].label)) {
            o += snprintf(out + o, size - o, "%s%s", o ? "+" : "", variant_words[i].label);
            prev = variant_words[i].label;
            if (o >= size - 1)
                break;
        }
}

int engine_export(Engine *e, BPTR fh, int (*want)(const Game *g))
{
    long i;
    FPuts(fh, (STRPTR)"Name,Set,Status,Installed,Newest,Pack,Languages,Systems,Drawer,"
                      "PackSize,PackCRC,WHDLoad,ChipKB,ExpKB,CPU,Chipset,Kickstart,WontRunHere,Saves\n");
    for (i = 0; i < e->n; i++) {
        const Game *g = e->games[i];
        char t[200];
        if (want && !want(g))
            continue;
        csv(fh, g->name, 0);
        csv(fh, g->pack ? pack_sets[g->pack->set].label :
                g->family >= 0 ? pack_sets[g->family == FAM_GAMES ? SET_GAMES :
                                           g->family == FAM_DEMOS ? SET_DEMOS : SET_MAGS].label : "", 0);
        csv(fh, status_word(g->status), 0);
        csv(fh, g->status == M_MISSING ? "" : g->installed, 0);
        csv(fh, g->pack ? g->pack->ver : "", 0);
        csv(fh, g->pack ? g->pack->file : "", 0);
        variant_labels(g->pack, 0, t, sizeof(t));
        csv(fh, t, 0);
        variant_labels(g->pack, 1, t, sizeof(t));
        csv(fh, t, 0);
        csv(fh, g->path ? g->path : "", 0);
        snprintf(t, sizeof(t), "%lu", g->pack ? g->pack->size : 0UL);
        csv(fh, g->pack ? t : "", 0);
        snprintf(t, sizeof(t), "%08lx", g->pack ? g->pack->crc : 0UL);
        csv(fh, g->pack ? t : "", 0);
        if (g->wsver) {
            snprintf(t, sizeof(t), "%u", (unsigned)g->wsver);
            csv(fh, t, 0);
            snprintf(t, sizeof(t), "%lu", g->basemem / 1024);
            csv(fh, t, 0);
            snprintf(t, sizeof(t), "%ld", g->expmem / 1024);
            csv(fh, t, 0);
            csv(fh, g->wsflags & WHDLF_REQ68020 ? "68020" : "68000", 0);
            csv(fh, g->wsflags & WHDLF_REQAGA ? "AGA" : "", 0);
            csv(fh, g->kick, 0);
            engine_cantrun(e, g, t, sizeof(t));
            csv(fh, t, 0);
        } else
            FPuts(fh, (STRPTR)",,,,,,,");
        snprintf(t, sizeof(t), "%d", g->saves);
        csv(fh, g->saves >= 0 ? t : "", 1);
    }
    return 0;
}

/* ---- Install missing games ------------------------------------------------ */

void engine_family_dir(const Engine *e, int fam, char *out, int size)
{
    const char *s = e->prefs.dirs[fam < 0 ? FAM_GAMES : fam];
    int n = 0;
    while (*s == ' ')
        s++;
    while (*s && *s != ';' && n < size - 1)
        out[n++] = *s++;
    while (n && out[n - 1] == ' ')
        n--;
    out[n] = 0;
}

int engine_install(Engine *e, long i, const char *dest, BPTR log)
{
    Game *g = e->games[i], *ng;
    UpdateConf c;
    char path[600];
    int rc;
    if (g->status != M_MISSING || !g->pack)
        return -1;
    if (!SocketBase) {
        say("Installing needs the network (no bsdsocket.library).");
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
    rc = install_game(&c, dest, g->name, g->pack, path, sizeof(path));
    if (rc)
        return rc;
    /* now an installed game: a new entry with its drawer */
    ng = (Game *)malloc(sizeof(Game) + strlen(path) + 1);
    if (!ng)
        return 0;                             /* installed; the next check lists it */
    memset(ng, 0, sizeof(Game));
    ng->path = (char *)(ng + 1);
    strcpy(ng->path, path);
    {
        const char *leaf = ng->path + strlen(ng->path);
        while (leaf > ng->path && leaf[-1] != '/' && leaf[-1] != ':')
            leaf--;
        ng->name = leaf;
    }
    ng->status = M_CURRENT;
    strncpy(ng->installed, g->pack->ver, sizeof(ng->installed) - 1);
    ng->pack = g->pack;
    ng->family = g->family;
    ng->saves = -1;
    e->st[M_MISSING]--;
    e->st[M_CURRENT]++;
    e->games[i] = ng;
    free(g);
    return 0;
}

/* ---- Kickstart images under other names ------------------------------------- */

/* WHDLoad's CRC16 (resload_CRC16): CRC-16/ARC, polynomial $8005 reflected. */
static unsigned short crc16(const unsigned char *b, unsigned long n)
{
    unsigned short c = 0;
    while (n--) {
        int k;
        c ^= *b++;
        for (k = 0; k < 8; k++)
            c = (unsigned short)(c & 1 ? (c >> 1) ^ 0xa001 : c >> 1);
    }
    return c;
}

int engine_kicks_needed(Engine *e, KickNeed *need, int max)
{
    long i;
    int n = 0;
    nknown = 0;                           /* DEVS:Kickstarts may have changed */
    for (i = 0; i < e->n; i++) {
        const Game *g = e->games[i];
        const char *p = g->kick;
        int k = 0;
        if (!g->path || !g->kick[0] || kick_present(g->kick))
            continue;
        while (*p) {                          /* each of its images (any one will do) */
            char one[48];
            int l = 0, j;
            while (*p && *p != '|' && l < (int)sizeof(one) - 1)
                one[l++] = *p++;
            one[l] = 0;
            if (*p == '|')
                p++;
            for (j = 0; j < n && strcmp(need[j].name, one); j++)
                ;
            if (j < n)
                need[j].games++;
            else if (n < max) {
                memset(&need[n], 0, sizeof(need[n]));
                strcpy(need[n].name, one);
                need[n].crc = k < 4 ? g->kickcrc[k] : 0;
                need[n++].games = 1;
            }
            k++;
        }
    }
    return n;
}

static int find_in(const char *dir, int depth, KickNeed *need, int n, unsigned char *buf, int *found)
{
    BPTR lock = Lock((STRPTR)dir, ACCESS_READ);
    struct FileInfoBlock *fib;
    int rc = 0;
    if (!lock)
        return 0;
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib))
            while (rc == 0 && *found < n && ExNext(lock, fib)) {
                char path[300];
                int l = (int)strlen(dir);
                if (l + (int)strlen((char *)fib->fib_FileName) + 2 > (int)sizeof(path))
                    continue;
                strcpy(path, dir);
                if (l && dir[l - 1] != ':' && dir[l - 1] != '/')
                    strcat(path, "/");
                strcat(path, (char *)fib->fib_FileName);
                if (break_pressed())
                    rc = -2;
                else if (fib->fib_DirEntryType > 0) {
                    if (depth > 0)
                        rc = find_in(path, depth - 1, need, n, buf, found);
                } else if (fib->fib_Size == 262144 || fib->fib_Size == 524288) {
                    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
                    if (fh) {
                        long got = Read(fh, buf, fib->fib_Size);
                        Close(fh);
                        if (got == fib->fib_Size) {
                            unsigned short c = crc16(buf, (unsigned long)got);
                            int j;
                            for (j = 0; j < n; j++)
                                if (!need[j].found[0] && need[j].crc && need[j].crc == c) {
                                    strncpy(need[j].found, path, sizeof(need[j].found) - 1);
                                    (*found)++;
                                }
                        }
                    }
                    busy("Looking for Kickstart images... %s", path);
                }
            }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return rc;
}

int engine_find_kicks(const char *dir, KickNeed *need, int n)
{
    unsigned char *buf = (unsigned char *)malloc(524288);
    int found = 0, j, rc;
    if (!buf)
        return 0;
    for (j = 0; j < n; j++)
        if (need[j].found[0])
            found++;
    rc = find_in(dir, 3, need, n, buf, &found);
    free(buf);
    return rc == -2 ? -2 : found;
}

int engine_copy_kick(const KickNeed *k, int *rtb)
{
    static unsigned char buf[16384];
    char to[120];
    BPTR in, out, l;
    long got;
    int ok = 1;
    if ((l = Lock((STRPTR)"DEVS:Kickstarts", ACCESS_READ)) != 0)
        UnLock(l);
    else if ((l = CreateDir((STRPTR)"DEVS:Kickstarts")) != 0)
        UnLock(l);
    snprintf(to, sizeof(to), "DEVS:Kickstarts/kick%s", k->name);
    if (!(in = Open((STRPTR)k->found, MODE_OLDFILE)))
        return -1;
    if (!(out = Open((STRPTR)to, MODE_NEWFILE))) {
        Close(in);
        return -1;
    }
    while ((got = Read(in, buf, sizeof(buf))) > 0)
        if (Write(out, buf, got) != got) {
            ok = 0;
            break;
        }
    if (got < 0)
        ok = 0;
    Close(in);
    Close(out);
    if (!ok) {
        DeleteFile((STRPTR)to);
        return -1;
    }
    nknown = 0;
    snprintf(to, sizeof(to), "DEVS:Kickstarts/kick%s.RTB", k->name);
    l = Lock((STRPTR)to, ACCESS_READ);
    if (l)
        UnLock(l);
    *rtb = l != 0;
    return 0;
}
