/* engine.h - what the Shell and the window front ends share: the settings,
 * the pack lists, the games found, and updating them. */
#ifndef WHDU_ENGINE_H
#define WHDU_ENGINE_H

#include <exec/types.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include "match.h"

/* The pack sets on the server. A beta set belongs to its family: its packs
 * are matched against the same drawers as the main set's. */
enum { SET_GAMES, SET_GAMESBETA, SET_DEMOS, SET_DEMOSBETA, SET_MAGS, NSETS };
enum { FAM_GAMES, FAM_DEMOS, FAM_MAGS, NFAMS };
typedef struct {
    const char *label;      /* "Games - Beta & Unofficial" */
    const char *zipname;    /* the DAT zip's name up to the date */
    const char *dir;        /* its folder on the server */
    const char *key;        /* its saved pack list: PROGDIR:Cache/<key>.zip */
    int family;
} PackSet;
extern const PackSet pack_sets[NSETS];

/* The settings, kept in ENVARC:WHDUpdate.prefs (one KEY=value a line). */
typedef struct {
    char host[80];          /* ftp2.grandis.nu */
    char webdir[160];       /* the pack folder by HTTP */
    char ftpdir[160];       /* the same by FTP */
    char user[40], pass[40];/* the FTP login */
    int ftp_only;
    unsigned long sets;     /* bit per pack set in use */
    unsigned long allowed;  /* variant words an unlabelled drawer may get */
    char dirs[NFAMS][400];  /* drawers per family, ';' between them */
} Prefs;

void prefs_default(Prefs *p);
void prefs_load(Prefs *p);              /* the defaults, then what is saved */
int  prefs_save(const Prefs *p);        /* ENV: and ENVARC: - kept */
int  prefs_use(const Prefs *p);         /* ENV: only - until the next reboot */

typedef struct {
    struct Node node;       /* for the window's list; ln_Name = label */
    char *path;             /* the game drawer */
    const char *name;       /* its last part (inside path) */
    int status;             /* M_... */
    char installed[16];
    const Pack *pack;       /* the newest fitting pack, or NULL */
    int family;             /* FAM_... of the drawer it was found in, -1 = any */
    int marked;             /* picked for updating (window) */
    int doomed;             /* picked for removing (window) */
    char label[100];
} Game;

typedef struct Engine {
    Prefs prefs;
    PackIndex ix;
    int have_dat;
    unsigned long dat_sets;  /* the sets ix holds */
    char datdate[16];        /* the newest of their dates */
    unsigned char *slavebuf;
    struct ScanCache *cache; /* what earlier scans read from the drawers (engine.c) */
    char *pattern;          /* parsed GAME= pattern, or NULL */
    Game **games;
    long n, cap;
    long st[M_COUNT];       /* games per M_ status */
    int cur_family;         /* the family engine_scan() is looking through */
    /* called for every game found (the Shell prints them); may be NULL */
    void (*seen)(void *ctx, const Game *g);
    void *seen_ctx;
} Engine;

int  engine_init(Engine *e);            /* also loads the settings */
void engine_free(Engine *e);

/* Get the pack lists of the sets in use: the newest from the server unless
 * offline, else the ones saved in PROGDIR:Cache. 0 = there is at least one. */
int  engine_dat(Engine *e, int offline);

void engine_clear(Engine *e);
/* Add the games under root (or every volume for "ALL"), matched against
 * the sets in use of family fam (-1: all of them). 0 = done, -1 = no such
 * drawer, -2 = stopped. */
int  engine_scan(Engine *e, const char *root, int depth, int fam);
/* engine_scan() for every drawer in the settings, each family against its
 * own sets. 0 = done, -2 = stopped. */
int  engine_scan_all(Engine *e, int depth);
/* After a scan: add an M_MISSING entry (path NULL) for every game of the sets
 * in use that none of the games found is. */
void engine_missing(Engine *e);

/* Update one game; it then counts as current. 0 = updated, -1 = failed,
 * -2 = stopped. log = an open WHDUpdate.log or 0. */
int  engine_update(Engine *e, Game *g, BPTR log);
BPTR engine_open_log(void);

/* Is g installed from a pack whose language or system the settings turn off? */
int  engine_unwanted(const Engine *e, const Game *g);
/* Delete game i's drawer and icon, or (keep) move them to WHDUpdate-Old;
 * then it leaves the list and the scan cache (and is freed). 0 = removed,
 * -1 = failed. */
int  engine_remove(Engine *e, long i, int keep, BPTR log);

#define LOGFILE "PROGDIR:WHDUpdate.log"

#endif
