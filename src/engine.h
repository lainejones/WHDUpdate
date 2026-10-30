/* engine.h - what the Shell and the window front ends share: the pack
 * list, the games found, and updating them. */
#ifndef WHDU_ENGINE_H
#define WHDU_ENGINE_H

#include <exec/types.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include "match.h"

typedef struct {
    struct Node node;       /* for the window's list; ln_Name = label */
    char *path;             /* the game drawer */
    const char *name;       /* its last part (inside path) */
    int status;             /* M_... */
    char installed[16];
    const Pack *pack;       /* the newest fitting pack, or NULL */
    int marked;             /* picked for updating (window) */
    char label[100];
} Game;

typedef struct Engine {
    PackIndex ix;
    int have_dat;
    char datdate[16];
    unsigned char *slavebuf;
    int ftp_only;
    char *pattern;          /* parsed GAME= pattern, or NULL */
    Game **games;
    long n, cap;
    long st[6];             /* games per M_ status */
    /* called for every game found (the Shell prints them); may be NULL */
    void (*seen)(void *ctx, const Game *g);
    void *seen_ctx;
} Engine;

int  engine_init(Engine *e);
void engine_free(Engine *e);

/* Get the pack list: the newest from the server unless offline, else the
 * one saved in PROGDIR:Cache. 0 = there is one. */
int  engine_dat(Engine *e, int offline);

void engine_clear(Engine *e);
/* Add the games under root (or every volume for "ALL"). 0 = done, -1 = no
 * such drawer, -2 = stopped. */
int  engine_scan(Engine *e, const char *root, int depth);

/* Update one game; it then counts as current. 0 = updated, -1 = failed,
 * -2 = stopped. log = an open WHDUpdate.log or 0. */
int  engine_update(Engine *e, Game *g, BPTR log);
BPTR engine_open_log(void);

#define LOGFILE "PROGDIR:WHDUpdate.log"

#endif
