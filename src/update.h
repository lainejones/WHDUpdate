/* update.h - replace an installed game with its newer pack. */
#ifndef WHDU_UPDATE_H
#define WHDU_UPDATE_H

#include "match.h"

typedef struct {
    const char *host, *webdir, *ftpdir, *setdir;  /* where the packs are */
    int ftp_only;
    /* The pack folder ("" or NULL = none): packs are taken from there, and
     * the downloaded ones kept there, as <packdir>/<setkey>/<pack>
     * (setkey = the set's drawer there: Games, Beta-Game ...). */
    const char *packdir, *setkey;
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

/* Download pack p into the pack folder only (nothing installed): for the
 * other machines that share it. 0 = downloaded, 1 = it was there already,
 * -1 = failed (said why), -2 = stopped. */
int fetch_to_folder(const UpdateConf *c, const Pack *p);

/* What Verify found. */
typedef struct {
    int files;              /* in the pack */
    int missing;            /* not in the game's drawer */
    int differ;             /* there but not the same (icons and saves aside) */
    int disks;              /* disk images that differ: saved games, most likely */
    char missing_list[160], differ_list[160];   /* the first few of them */
} VerifyResult;

/* Compare the game in drawer path with pack p, file by file (the pack from
 * the pack folder, or downloaded): icons, saves and ArchiveName.txt aside.
 * 0 = it is all there, 1 = files missing or different (in *r), -1 = failed,
 * -2 = stopped. */
int verify_game(const UpdateConf *c, const char *path, const Pack *p, VerifyResult *r);

/* Free bytes on the volume path is on. */
unsigned long volume_free(const char *path);

#endif
