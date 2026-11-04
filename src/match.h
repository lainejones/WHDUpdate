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
    unsigned long vbits;   /* the variant words in its name (variant_words[]) */
    int set;               /* which pack set it is from (engine.c) */
    unsigned long size, crc;
} Pack;

typedef struct {
    Pack *packs;
    int n, cap;
    int *byfull, *bybase;  /* indexes into packs, sorted by full / base */
} PackIndex;

/* M_MISSING: a pack no installed game has (the window lists these too) */
enum { M_NOMATCH, M_UNKNOWN, M_CURRENT, M_UPDATE, M_NEWER, M_CHECK, M_MISSING, M_COUNT };
enum { HOW_NONE, HOW_NAME, HOW_BASE, HOW_PREFIX, HOW_SLAVEFILE, HOW_SLAVENAME, HOW_ABBREV };

typedef struct {
    int status;            /* M_... */
    int how;               /* HOW_... */
    int variants;          /* >1: several variants fit, the newest is shown */
    const Pack *newest;
    char installed[16];    /* installed version, "" if the slave doesn't say */
} Match;

/* The variant words pack names carry (De, AGA, CD32, ...), each a bit in
 * Pack.vbits. The Languages and Systems pages of the window set which of
 * them a drawer that names no variant may be given (match_allowed); a
 * drawer that names one ("1869De") always gets that one. */
typedef struct {
    const char *word;      /* as in the name, lower case: "de" */
    const char *label;     /* for the settings page: "German" */
    int system;            /* 0 = a language, 1 = a machine / chipset / TV system */
} VariantWord;
extern const VariantWord variant_words[];       /* ends with word NULL */
extern unsigned long match_allowed;
/* Which pack sets (bits by Pack.set) a drawer is matched against: games
 * drawers against the games sets, demo drawers against the demo sets. */
extern unsigned long match_sets;
unsigned long variants_default(void);            /* English; AGA, ECS, OCS, NTSC, PAL */
/* Is a pack with these variant words one the settings turn off (allowed =
 * the Languages and Systems pages)? Its languages are a choice - an En+De
 * pack is wanted while English is on - its systems are all needed - an
 * AGA+NTSC pack is not wanted while AGA is off. */
int variants_unwanted(unsigned long vbits, unsigned long allowed);

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

/* Every game in the sets of match_sets that none of the installed games is:
 * for each pack base not in present (sorted, strcmp), the newest pack of
 * each set whose variants are all allowed (match_allowed). */
void packs_missing(const PackIndex *ix, const char *const *present, long npresent,
                   void (*found)(void *ctx, const Pack *p), void *ctx);

#endif
