/* match.h - which Retroplay pack is an installed game, and is it current?
 *
 * Pack names look like Base_vVersion[_Token...][_NNNN].lha, e.g.
 * 1869_v1.2_De_0417.lha. The drawer inside such a pack is Base plus its
 * tokens ("1869De"); the 3-4 digit number is not part of it. So an installed
 * drawer is looked up by name+tokens first, then by the base name, then by
 * the slave's file name and the game name stored in the slave. */
#ifndef WHDU_MATCH_H
#define WHDU_MATCH_H

#include "slave.h"

typedef struct {
    const char *file;      /* "1869_v1.2_De_0417.lha" */
    const char *folder;    /* "0" - the folder on the server */
    const char *ver;       /* "1.2" */
    const char *full;      /* "1869de" - base + tokens, letters/digits only */
    const char *base;      /* "1869" */
    const char *tokens;    /* "de" */
    int foreign;           /* has a language / machine / TV-system variant */
    unsigned long size, crc;
} Pack;

typedef struct {
    Pack *packs;
    int n, cap;
    int *byfull, *bybase;  /* indexes into packs, sorted by full / base */
} PackIndex;

enum { M_NOMATCH, M_UNKNOWN, M_CURRENT, M_UPDATE, M_NEWER, M_CHECK };
enum { HOW_NONE, HOW_NAME, HOW_BASE, HOW_PREFIX, HOW_SLAVEFILE, HOW_SLAVENAME, HOW_ABBREV };

typedef struct {
    int status;            /* M_... */
    int how;               /* HOW_... */
    int variants;          /* >1: several variants fit, the newest is shown */
    const Pack *newest;
    char installed[16];    /* installed version, "" if the slave doesn't say */
} Match;

void packs_init(PackIndex *ix);
int  packs_add(PackIndex *ix, const char *folder, const char *file,
               unsigned long size, unsigned long crc);   /* 0 ok, -1 no memory */
int  packs_finish(PackIndex *ix);                        /* 0 ok, -1 no memory */
void packs_free(PackIndex *ix);

/* drawer = the game's drawer name, slavefile = its slave's file name. */
void match_game(const PackIndex *ix, const char *drawer, const char *slavefile,
                const SlaveInfo *si, Match *m);

/* letters and digits only, lower case: "Bubble&Squeak!" -> "bubblesqueak" */
void norm_name(const char *in, char *out, int size);

const char *status_word(int status);

#endif
