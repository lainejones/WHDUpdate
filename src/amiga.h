/* amiga.h - the AmigaOS-only parts of WHDUpdate. */
#ifndef WHDU_AMIGA_H
#define WHDU_AMIGA_H

/* net.c --------------------------------------------------------------- */

/* GET http://host/path (path already URL-encoded). Returns the body in a
 * malloc()ed buffer with a NUL after it, its length in *len; NULL (with a
 * message printed) on failure. maxlen caps the size. Ctrl-C stops it. */
unsigned char *http_get(const char *host, const char *path,
                        unsigned long *len, unsigned long maxlen);

/* The same file or folder listing (names, one per line) by FTP, user
 * "ftp" / password "amiga"; path NOT encoded. */
unsigned char *ftp_get(const char *host, const char *path,
                       unsigned long *len, unsigned long maxlen);
unsigned char *ftp_list(const char *host, const char *dir,
                        unsigned long *len, unsigned long maxlen);

/* Percent-encode a path ("Retroplay WHDLoad Packs/A&B" -> "...%20...%26B"),
 * and back (also "&amp;" from HTML). */
void url_encode(const char *in, char *out, int size);
void url_decode(const char *in, char *out, int size);

/* scan.c -------------------------------------------------------------- */

/* Called for each game drawer: its full path, its name and its slave's
 * file name. Return nonzero to stop the scan. */
typedef int (*game_fn)(void *ctx, const char *path, const char *name,
                       const char *slave);

/* Look through root (and its drawers, down to depth levels) for game
 * drawers: drawers with a WHDLoad slave in them. 0 = done, -1 = root not
 * found, -2 = stopped (Ctrl-C or the callback). */
int scan_tree(const char *root, int depth, game_fn fn, void *ctx);

/* Every mounted volume except RAM:, as "Name:" strings in one block
 * separated by NULs and ended by an empty one; free() it. */
char *volume_list(void);

/* main.c -------------------------------------------------------------- */
int break_pressed(void);

#endif
