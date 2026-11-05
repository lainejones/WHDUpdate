/* engine.h - what the Shell and the window front ends share: the settings,
 * the pack lists, the games found, and updating them. */
#ifndef WHDU_ENGINE_H
#define WHDU_ENGINE_H

#include <exec/types.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include "match.h"
#include "update.h"

/* The pack sets on the server. A beta set belongs to its family: its packs
 * are matched against the same drawers as the main set's. */
enum { SET_GAMES, SET_GAMESBETA, SET_DEMOS, SET_DEMOSBETA, SET_MAGS, NSETS };
enum { FAM_GAMES, FAM_DEMOS, FAM_MAGS, NFAMS };
typedef struct {
    const char *label;      /* "Games - Beta & Unofficial" */
    const char *zipname;    /* the DAT zip's name up to the date */
    const char *dir;        /* its folder on the server */
    const char *key;        /* its saved pack list: PROGDIR:Cache/<key>.zip */
    const char *store;      /* its drawer in the pack folder: <packdir>/<store>/<pack> */
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
    char packdir[160];      /* the pack folder the machines share, "" = none (update.h) */
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
    int marked;             /* picked for updating, or under Missing for installing (window) */
    int doomed;             /* picked for removing (window) */
    /* what its slave asks of the machine (all 0 when it couldn't be read) */
    unsigned short wsver, wsflags;
    unsigned long basemem;
    long expmem;
    char kick[48];
    unsigned short kickcrc[4];
    int saves;              /* save and high score files in the drawer, -1 = not counted */
    long dupe;              /* 1.. = the group of copies of one game it is in, 0 = none */
    char label[100];
} Game;

/* The machine WHDUpdate runs on, for Won't run here (engine_machine). */
typedef struct {
    unsigned long chip, fast;   /* total memory */
    int cpu020;                 /* a 68020 or better */
    int aga;                    /* the AGA chipset */
    int whdload;                /* C:WHDLoad's version, 0 = not found */
} Machine;

/* Why a game won't run here (engine_cantrun) */
enum { CANT_CHIP = 1, CANT_MEM = 2, CANT_CPU = 4, CANT_AGA = 8, CANT_KICK = 16, CANT_WHDL = 32 };

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
    Machine mach;
    int mach_known;
    Game **dupord;          /* the games with a twin, a group after the other (engine_dupes) */
    /* asked when an update would need a Kickstart image you lack (UpdateConf) */
    int (*kick_ask)(const char *path, const char *kick);
    long ndup;
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
/* Forget the scan cache (and delete PROGDIR:Cache/games.txt): the next scan
 * lists every drawer and reads every slave again. */
void engine_forget_cache(Engine *e);
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
 * -2 = stopped, 1 = left as it was (the new pack needs a Kickstart image
 * you lack, and kick_ask said keep it). log = an open WHDUpdate.log or 0. */
int  engine_update(Engine *e, Game *g, BPTR log);
BPTR engine_open_log(void);
/* Download g's newest pack into the pack folder only, for the machines
 * that share it. 0 = downloaded, 1 = there already, -1 = failed, -2 = stopped. */
int  engine_fetch(Engine *e, const Game *g, BPTR log);
/* The pack g is installed from: the one its ArchiveName.txt names, else
 * its newest pack when it is current. NULL = not known. */
const Pack *engine_installed_pack(Engine *e, const Game *g);
/* Compare g's drawer with the pack it is installed from (verify_game).
 * 0 = all there, 1 = files missing or different (*r), -1 = failed or no
 * pack known, -2 = stopped. */
int  engine_verify(Engine *e, const Game *g, BPTR log, VerifyResult *r);

/* Is g installed from a pack whose language or system the settings turn off? */
int  engine_unwanted(const Engine *e, const Game *g);
/* Delete game i's drawer and icon, or (keep) move them to WHDUpdate-Old;
 * then it leaves the list and the scan cache (and is freed). 0 = removed,
 * -1 = failed. */
int  engine_remove(Engine *e, long i, int keep, BPTR log);

/* What this machine has (memory, CPU, chipset, WHDLoad); read once. */
void engine_machine(Engine *e);
/* The reasons (CANT_...) g won't run on this machine, 0 = none or not
 * known; why = a few words for each ("AGA, 2 MB chip"), may be NULL. A
 * missing Kickstart image (DEVS:Kickstarts/kick<name>) and an old WHDLoad can be put
 * right; the others are the machine's. */
int  engine_cantrun(Engine *e, const Game *g, char *why, int size);
/* Find the games installed more than once (the same drawer name in two
 * places): their dupe numbers and e->dupord. How many games. */
long engine_dupes(Engine *e);
/* Count g's save and high score files (g->saves). */
void engine_count_saves(Game *g);
/* The games want() picks (NULL: all), as CSV with a header line. 0 = written. */
int  engine_export(Engine *e, BPTR fh, int (*want)(const Game *g));
/* Install missing game i (M_MISSING) into drawer dest - into its letter
 * drawer there (A, 0-9 ...) when dest has them. It then counts as
 * current. 0 = installed, -1 = failed, -2 = stopped. */
int  engine_install(Engine *e, long i, const char *dest, BPTR log);
/* A Kickstart image some game needs that is not in DEVS:Kickstarts. */
typedef struct {
    char name[48];          /* "34005.a500": WHDLoad loads DEVS:Kickstarts/kick34005.a500 */
    unsigned short crc;     /* what WHDLoad checks it by */
    long games;             /* how many games ask for it */
    char found[300];        /* a file with that CRC (engine_find_kicks), or "" */
} KickNeed;
/* The images the installed games need and DEVS:Kickstarts lacks, at most
 * max into need. How many. */
int  engine_kicks_needed(Engine *e, KickNeed *need, int max);
/* Look through dir (and drawers in it, 3 levels) for files the size of a
 * Kickstart whose CRC is one of need[]'s; their paths into found. How many
 * found; -2 = stopped. */
int  engine_find_kicks(const char *dir, KickNeed *need, int n);
/* Copy k->found to DEVS:Kickstarts/kick<name>. 0 = done (*rtb = 0 when
 * the .RTB file WHDLoad also needs is not there), -1 = failed. */
int  engine_copy_kick(const KickNeed *k, int *rtb);

/* Put back game i's copy from before its last update (undo_update); its
 * version and status are read again. The old copy's path into from.
 * 0 = done, 1 = no old copy, -1 = failed. */
int  engine_undo(Engine *e, long i, BPTR log, char *from, int size);
/* Copy the saves of the games want() picks (NULL: all installed ones) into
 * dest/<game name>/... How many files; *games = how many games had some. */
long engine_backup_saves(Engine *e, const char *dest, int (*want)(const Game *g), long *games);
/* The room the marked games need (install = the ones under Missing, else
 * the updates), as "about N MB on Vol: (M MB free)" per volume. 1 = some
 * volume looks too full. */
int  engine_space(Engine *e, int install, char *out, int size);

/* The first drawer of family fam in the settings, "" if none. */
void engine_family_dir(const Engine *e, int fam, char *out, int size);

#define LOGFILE "PROGDIR:WHDUpdate.log"

#endif
