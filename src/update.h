/* update.h - replace an installed game with its newer pack. */
#ifndef WHDU_UPDATE_H
#define WHDU_UPDATE_H

#include "match.h"

typedef struct {
    const char *host, *webdir, *ftpdir, *setdir;  /* where the packs are */
    int ftp_only;
    BPTR log;                                     /* WHDUpdate.log, or 0 */
} UpdateConf;

/* Update the game in drawer path (its name is the last part) to pack p.
 * The old drawer and its icon go to <volume>:WHDUpdate-Old/; saves and
 * high scores are copied into the new one, and the icons keep their
 * places and the tooltypes you added. 0 = updated, -1 = failed and
 * nothing changed, -2 = stopped by Ctrl-C (nothing changed). */
int update_game(const UpdateConf *c, const char *path, const Pack *p, const char *installed);

/* Remove the game in drawer path and its icon: deleted for good, or (keep)
 * moved to <volume>:WHDUpdate-Old/ like an updated game's old copy.
 * 0 = done, -1 = failed (said why). */
int remove_game(BPTR log, const char *path, int keep);

/* Install pack p into drawer dest - into its letter drawer there (A, B ...
 * 0-9, by the first letter of name) when dest has those. The new drawer's
 * path into path. 0 = installed, -1 = failed (said why), -2 = stopped. */
int install_game(const UpdateConf *c, const char *dest, const char *name, const Pack *p,
                 char *path, int size);

/* How many save and high score files the drawer at path has. */
int count_saves(const char *path);

#endif
