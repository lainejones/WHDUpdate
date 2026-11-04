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

/* The FTP user and password for the calls below. */
void ftp_login(const char *user, const char *pass);

/* Download into the open file fh (expect = the size the DAT gives, for the
 * progress line). 0 = done; *got = bytes, *crc = their CRC-32. */
int http_download(const char *host, const char *path, BPTR fh,
                  unsigned long expect, unsigned long *got, unsigned long *crc);
int ftp_download(const char *host, const char *path, BPTR fh,
                 unsigned long expect, unsigned long *got, unsigned long *crc);

/* Percent-encode a path ("Retroplay WHDLoad Packs/A&B" -> "...%20...%26B"),
 * and back (also "&amp;" from HTML). */
void url_encode(const char *in, char *out, int size);
void url_decode(const char *in, char *out, int size);

/* scan.c -------------------------------------------------------------- */

/* What the directory listing said about the files the engine would read:
 * the slave, and ArchiveName.txt if there is one (arc_size -1 = none). With
 * these the scan cache can tell an unchanged drawer without opening a file. */
typedef struct {
    long slave_size;
    struct DateStamp slave_date;
    long arc_size;
    struct DateStamp arc_date;
} GameFiles;

/* Called for each game drawer: its full path, its name, its slave's file
 * name and the files' sizes and dates. Return nonzero to stop the scan. */
typedef int (*game_fn)(void *ctx, const char *path, const char *name,
                       const char *slave, const GameFiles *files);

/* Look through root (and its drawers, down to depth levels) for game
 * drawers: drawers with a WHDLoad slave in them. 0 = done, -1 = root not
 * found, -2 = stopped (Ctrl-C or the callback). */
int scan_tree(const char *root, int depth, game_fn fn, void *ctx);

/* Every mounted volume except RAM:, as "Name:" strings in one block
 * separated by NULs and ended by an empty one; free() it. */
char *volume_list(void);

/* main.c -------------------------------------------------------------- */
extern struct Library *SocketBase, *IconBase;
int break_pressed(void);                 /* Ctrl-C, or Stop in the window */
void say(const char *fmt, ...);          /* a message: printed, or the status line */
void busy(const char *fmt, ...);         /* progress: only on the status line */

/* gui.c --------------------------------------------------------------- */
struct Engine;
int  gui_main(struct Engine *e, const char *dirs);
void gui_status(const char *text);
int  gui_poll(void);                     /* handle the window; 1 = Stop was pressed */


#endif
