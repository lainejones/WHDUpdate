/* scan.c - find the WHDLoad game drawers under a drawer or on the volumes.
 *
 * A game drawer is a drawer with a slave in it. Its sub-drawers (data/,
 * saves) are not searched further. Slave names can be cut short by the old
 * 30-character file name limit ("HorrorZombiesFromTheCrypt.Slav"), so
 * ".slave", ".slav" and ".sla" all count. The FileInfoBlock comes from
 * AllocDosObject(): DOS needs it longword aligned. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>
#include "amiga.h"

#define PATHMAX 512

static int ends_with_nocase(const char *s, const char *tail)
{
    int sl = (int)strlen(s), tl = (int)strlen(tail), i;
    if (sl <= tl)
        return 0;
    for (i = 0; i < tl; i++) {
        char a = s[sl - tl + i], b = tail[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a | 32);
        if (a != b)
            return 0;
    }
    return 1;
}

static int eq_nocase(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x | 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y | 32);
        if (x != y || !x)
            return x - y;
    }
}

static int is_slave_name(const char *n)
{
    return ends_with_nocase(n, ".slave") || ends_with_nocase(n, ".slav") || ends_with_nocase(n, ".sla");
}

static int same_nocase_prefix(const char *a, const char *b)
{
    while (*a && *b && *a != '.' && *b != '.') {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x | 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y | 32);
        if (x != y)
            return 0;
    }
    return (*a == 0 || *a == '.') && (*b == 0 || *b == '.');
}

static void join(char *out, const char *dir, const char *name)
{
    int l = (int)strlen(dir);
    if (l + (int)strlen(name) + 2 > PATHMAX) {
        out[0] = 0;
        return;
    }
    strcpy(out, dir);
    if (l && dir[l - 1] != ':' && dir[l - 1] != '/')
        strcat(out, "/");
    strcat(out, name);
}

static const char *leaf(const char *path)
{
    const char *p = path + strlen(path);
    while (p > path && p[-1] != '/' && p[-1] != ':')
        p--;
    return p;
}

static int walk(const char *dir, int depth, game_fn fn, void *ctx)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    char *subs = NULL, slave[108];
    long subslen = 0, subscap = 0;
    GameFiles gf;
    int rc = 0;

    if (break_pressed())
        return -2;
    lock = Lock((STRPTR)dir, ACCESS_READ);
    if (!lock)
        return -1;
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) {
        UnLock(lock);
        return -1;
    }
    slave[0] = 0;
    memset(&gf, 0, sizeof(gf));
    gf.arc_size = -1;
    if (Examine(lock, fib) && fib->fib_DirEntryType > 0) {
        while (ExNext(lock, fib)) {
            const char *n = (const char *)fib->fib_FileName;
            if (fib->fib_DirEntryType < 0) {
                /* the slave named like the drawer wins over the others */
                if (is_slave_name(n) && (!slave[0] || same_nocase_prefix(n, leaf(dir)))) {
                    strcpy(slave, n);
                    gf.slave_size = fib->fib_Size;
                    gf.slave_date = fib->fib_Date;
                } else if (!eq_nocase(n, "ArchiveName.txt")) {
                    gf.arc_size = fib->fib_Size;
                    gf.arc_date = fib->fib_Date;
                }
            } else if (fib->fib_DirEntryType == ST_USERDIR && depth > 0) {
                long l = (long)strlen(n) + 1;
                if (subslen + l + 1 > subscap) {
                    long nc = subscap ? subscap * 2 : 4096;
                    char *ns;
                    while (nc < subslen + l + 1)
                        nc *= 2;
                    ns = (char *)realloc(subs, nc);
                    if (!ns)
                        break;
                    subs = ns;
                    subscap = nc;
                }
                memcpy(subs + subslen, n, l);
                subslen += l;
            }
        }
    }
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    if (slave[0]) {
        if (fn(ctx, dir, leaf(dir), slave, &gf))
            rc = -2;
    } else if (subs) {
        char *path = (char *)malloc(PATHMAX), *s;
        for (s = subs; path && s < subs + subslen && rc != -2; s += strlen(s) + 1) {
            join(path, dir, s);
            if (path[0] && walk(path, depth - 1, fn, ctx) == -2)
                rc = -2;
        }
        free(path);
    }
    free(subs);
    return rc;
}

int scan_tree(const char *root, int depth, game_fn fn, void *ctx)
{
    BPTR l = Lock((STRPTR)root, ACCESS_READ);
    if (!l)
        return -1;
    UnLock(l);
    return walk(root, depth, fn, ctx);
}

char *volume_list(void)
{
    struct DosList *dl;
    char *out = (char *)malloc(4096);
    int n = 0;
    if (!out)
        return NULL;
    dl = LockDosList(LDF_VOLUMES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_VOLUMES)) != NULL) {
        const UBYTE *b = (const UBYTE *)BADDR(dl->dol_Name);
        int len = b[0];
        if (len == 3 && !strncmp((const char *)b + 1, "RAM", 3))
            continue;
        if (n + len + 3 >= 4096)
            break;
        memcpy(out + n, b + 1, len);
        out[n + len] = ':';
        out[n + len + 1] = 0;
        n += len + 2;
    }
    UnLockDosList(LDF_VOLUMES | LDF_READ);
    out[n] = 0;
    return out;
}
