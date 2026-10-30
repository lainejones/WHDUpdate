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

#endif
