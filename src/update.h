/* update.h - replace an installed game with its newer pack. */
#ifndef WHDU_UPDATE_H
#define WHDU_UPDATE_H

#include "match.h"

typedef struct {
    const char *host, *webdir, *ftpdir, *setdir;  /* where the packs are */
    int ftp_only;
    BPTR log;                                     /* WHDUpdate.log, or 0 */
    /* The new pack needs a Kickstart image (kick = its name, "31034.a1000")
     * that is not in DEVS:Kickstarts, and the copy installed now didn't:
     * nonzero = update anyway, 0 = keep the old one. NULL = update anyway. */
    int (*kick_ask)(const char *path, const char *kick);
} UpdateConf;

/* Update the game in drawer path (its name is the last part) to pack p.
 * (1 = not updated: kick_ask kept the old one.)
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

/* Put back the old copy of the game in drawer path that an update moved
 * to <volume>:WHDUpdate-Old (the newest one there), carrying over the
 * saves made since; the newer copy goes to WHDUpdate-Old as <name>.undone.
 * The old copy's path into from. 0 = done, 1 = there is no old copy,
 * -1 = failed (said why; nothing changed). */
int undo_update(BPTR log, const char *path, char *from, int size);

/* Copy the save and high score files of the game drawer gamedir into
 * dest, in the same drawers. How many, -1 = dest can't be made. */
int copy_saves(const char *gamedir, const char *dest);

/* What the slave in drawer dir says (found by listing the drawer).
 * 1 = there is one. */
int drawer_slave(const char *dir, SlaveInfo *si);

/* Free bytes on the volume path is on. */
unsigned long volume_free(const char *path);

#endif
