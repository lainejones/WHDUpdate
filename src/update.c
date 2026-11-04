/* update.c - replace an installed game with its newer pack.
 *
 * Everything happens on the game's own volume, so the swap at the end is a
 * pair of Rename()s and nothing is copied twice:
 *
 *   1. download the pack into <volume>:WHDUpdate-Old/.work/, check its
 *      size and CRC against the DAT
 *   2. unpack it there with LhA (UNLZX for the few .lzx packs)
 *   3. copy the saves and high scores from the old drawer into the new one
 *   4. give the new icons the old ones' places and the tooltypes you added
 *   5. move the old drawer + icon to <volume>:WHDUpdate-Old/, move the new
 *      ones into its place (under the old name), put it back on any failure
 *
 * Until step 5 the old game is untouched; the old copy stays in
 * WHDUpdate-Old until you delete it. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/datetime.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "slave.h"
#include "update.h"
#include "amiga.h"

#define P 600                    /* path buffers */
#define OLDDIR "WHDUpdate-Old"

static void cat_path(char *out, const char *dir, const char *name)
{
    int l = (int)strlen(dir);
    if (l + (int)strlen(name) + 2 > P) {
        out[0] = 0;
        return;
    }
    if (out != dir)
        strcpy(out, dir);
    if (l && dir[l - 1] != ':' && dir[l - 1] != '/')
        strcat(out, "/");
    strcat(out, name);
}

static const char *leaf_of(const char *path)
{
    const char *p = path + strlen(path);
    while (p > path && p[-1] != '/' && p[-1] != ':')
        p--;
    return p;
}

static int exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l)
        UnLock(l);
    return l != 0;
}

static int make_dir(const char *path)
{
    BPTR l;
    if (exists(path))
        return 1;
    l = CreateDir((STRPTR)path);
    if (l)
        UnLock(l);
    return l != 0;
}

/* WHDUpdate-Old, with a drawer icon so Workbench shows it: the default
 * drawer icon (ENV:Sys/def_drawer.info when there is one, on OS 3.5+),
 * placed by Workbench. A drawer made by an older version gets one too. */
static int make_old_dir(const char *path)
{
    char info[P];
    struct DiskObject *dob;
    if (!make_dir(path))
        return 0;
    snprintf(info, sizeof(info), "%s.info", path);
    if (IconBase && !exists(info) && (dob = GetDefDiskObject(WBDRAWER)) != NULL) {
        dob->do_CurrentX = NO_ICON_POSITION;
        dob->do_CurrentY = NO_ICON_POSITION;
        PutDiskObject((STRPTR)path, dob);
        FreeDiskObject(dob);
    }
    return 1;
}

/* Run an AmigaDOS command quietly, in drawer dir (NULL: here); its return code. */
static LONG run_in(const char *dir, const char *cmd)
{
    BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE), cd = 0;
    LONG rc;
    if (dir && !(cd = Lock((STRPTR)dir, ACCESS_READ))) {
        if (nil)
            Close(nil);
        return -1;
    }
    if (cd)
        rc = SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, 0, NP_CurrentDir, cd, TAG_END);
    else
        rc = SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, 0, TAG_END);
    if (rc == -1 && cd)
        UnLock(cd);                        /* the process never took it over */
    if (nil)
        Close(nil);
    return rc;
}

static LONG run(const char *cmd)
{
    return run_in(NULL, cmd);
}

static void delete_tree(const char *path)
{
    char cmd[P + 40];
    if (!exists(path))
        return;
    snprintf(cmd, sizeof(cmd), "Delete \"%s\" ALL QUIET FORCE", path);
    run(cmd);
}

static unsigned long free_bytes(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    struct InfoData *id;
    unsigned long free = 0;
    if (!l)
        return 0;
    id = (struct InfoData *)AllocMem(sizeof(*id), MEMF_PUBLIC | MEMF_CLEAR);  /* longword aligned */
    if (id && Info(l, id))
        free = (unsigned long)(id->id_NumBlocks - id->id_NumBlocksUsed) * (unsigned long)id->id_BytesPerBlock;
    if (id)
        FreeMem(id, sizeof(*id));
    UnLock(l);
    return free;
}

/* Copy one file, keeping its protection bits, date and comment. */
static int copy_file(const char *from, const char *to)
{
    static UBYTE buf[16384];
    BPTR in, out, l;
    struct FileInfoBlock *fib;
    LONG n;
    int ok = 1;
    in = Open((STRPTR)from, MODE_OLDFILE);
    if (!in)
        return 0;
    out = Open((STRPTR)to, MODE_NEWFILE);
    if (!out) {
        Close(in);
        return 0;
    }
    while ((n = Read(in, buf, sizeof(buf))) > 0)
        if (Write(out, buf, n) != n) {
            ok = 0;
            break;
        }
    if (n < 0)
        ok = 0;
    Close(in);
    Close(out);
    if (!ok) {
        DeleteFile((STRPTR)to);
        return 0;
    }
    if ((l = Lock((STRPTR)from, ACCESS_READ)) != 0) {
        fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (fib && Examine(l, fib)) {
            SetProtection((STRPTR)to, fib->fib_Protection);
            SetFileDate((STRPTR)to, &fib->fib_Date);
            if (fib->fib_Comment[0])
                SetComment((STRPTR)to, fib->fib_Comment);
        }
        if (fib)
            FreeDosObject(DOS_FIB, fib);
        UnLock(l);
    }
    return 1;
}

/* ---- saves and high scores -------------------------------------------- */

static int has_nocase(const char *s, const char *w)
{
    int wl = (int)strlen(w);
    for (; *s; s++) {
        int i;
        for (i = 0; i < wl; i++) {
            char c = s[i];
            if (c >= 'A' && c <= 'Z')
                c = (char)(c | 32);
            if (c != w[i])
                break;
        }
        if (i == wl)
            return 1;
    }
    return 0;
}

static int ends_nocase(const char *s, const char *w)
{
    int sl = (int)strlen(s), wl = (int)strlen(w);
    return sl >= wl && has_nocase(s + sl - wl, w);
}

/* A file a game writes: save games, high scores, WHDLoad's save disks
 * ("Disk.1" made by the slave). rel = its path inside the game drawer. */
static int is_save(const char *rel)
{
    const char *name = leaf_of(rel), *p;
    if (has_nocase(rel, "save") || has_nocase(name, "high") || has_nocase(name, "score") ||
        ends_nocase(name, ".hi") || ends_nocase(name, ".sav"))
        return 1;
    if ((name[0] | 32) == 'd' && (name[1] | 32) == 'i' && (name[2] | 32) == 's' &&
        (name[3] | 32) == 'k' && name[4] == '.' && name[5] >= '0' && name[5] <= '9') {
        for (p = name + 5; *p >= '0' && *p <= '9'; p++)
            ;
        return *p == 0;
    }
    return 0;
}

/* Are the two files byte for byte the same? */
static int same_content(const char *a, const char *b)
{
    static UBYTE ba[4096], bb[4096];
    BPTR fa = Open((STRPTR)a, MODE_OLDFILE), fb = Open((STRPTR)b, MODE_OLDFILE);
    int same = fa && fb;
    while (same) {
        LONG na = Read(fa, ba, sizeof(ba)), nb = Read(fb, bb, sizeof(bb));
        if (na != nb || na < 0 || memcmp(ba, bb, na))
            same = 0;
        else if (na == 0)
            break;
    }
    if (fa)
        Close(fa);
    if (fb)
        Close(fb);
    return same;
}

/* Copy the saves in old/rel into new/rel: the ones the new drawer does not
 * have, and the ones it has but different (the pack's are fresh, empty
 * ones). Returns how many. */
static int carry_saves(const char *olddir, const char *newdir, const char *rel)
{
    char od[P], nd[P], o[P], n[P], r[P];
    BPTR lock;
    struct FileInfoBlock *fib;
    char *subs = NULL;
    long sl = 0, sc = 0;
    int count = 0;

    if (rel[0]) {
        cat_path(od, olddir, rel);
        cat_path(nd, newdir, rel);
    } else {
        strcpy(od, olddir);
        strcpy(nd, newdir);
    }
    if (!(lock = Lock((STRPTR)od, ACCESS_READ)))
        return 0;
    if (!(fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL))) {
        UnLock(lock);
        return 0;
    }
    if (Examine(lock, fib))
        while (ExNext(lock, fib)) {
            const char *name = (const char *)fib->fib_FileName;
            if (rel[0])
                cat_path(r, rel, name);
            else
                strcpy(r, name);
            if (fib->fib_DirEntryType == ST_USERDIR) {
                long l = (long)strlen(name) + 1;
                if (sl + l > sc) {
                    char *ns = (char *)realloc(subs, sc = sc ? sc * 2 : 1024);
                    if (!ns)
                        break;
                    subs = ns;
                }
                memcpy(subs + sl, name, l);
                sl += l;
            } else if (fib->fib_DirEntryType < 0 && is_save(r)) {
                cat_path(o, olddir, r);
                cat_path(n, newdir, r);
                if (!exists(n)) {
                    char parent[P];
                    strcpy(parent, n);
                    *(char *)leaf_of(parent) = 0;
                    if (exists(parent) && copy_file(o, n)) {
                        say("  kept %s", r);
                        count++;
                    }
                } else if (!same_content(o, n) && copy_file(o, n)) {
                    /* the pack brings a fresh one (empty high scores): yours wins */
                    say("  kept your %s", r);
                    count++;
                }
            }
        }
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    if (subs) {
        char *s;
        for (s = subs; s < subs + sl; s += strlen(s) + 1) {
            if (rel[0])
                cat_path(r, rel, s);
            else
                strcpy(r, s);
            cat_path(n, newdir, r);
            if (exists(n) || has_nocase(s, "save")) {
                make_dir(n);
                count += carry_saves(olddir, newdir, r);
            }
        }
        free(subs);
    }
    return count;
}

/* ---- icons -------------------------------------------------------------- */

static int same_key(const char *a, const char *b)
{
    for (; *a && *a != '=' && *b && *b != '='; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x & ~32);
        if (y >= 'a' && y <= 'z') y = (char)(y & ~32);
        if (x != y)
            return 0;
    }
    return (*a == 0 || *a == '=') && (*b == 0 || *b == '=');
}

/* NewIcons keep their image in tooltypes: never carry those over. */
static int image_tooltype(const char *t)
{
    return !strncmp(t, "*** ", 4) || ((t[0] == 'I' || t[0] == 'i') && (t[1] == 'M' || t[1] == 'm') &&
                                       t[2] >= '1' && t[2] <= '9' && t[3] == '=');
}

static int user_choice(const char *t)
{
    /* the settings people change: CUSTOM1..5, CUSTOM, PRELOAD and friends */
    return same_key(t, "CUSTOM") || same_key(t, "CUSTOM1") || same_key(t, "CUSTOM2") ||
           same_key(t, "CUSTOM3") || same_key(t, "CUSTOM4") || same_key(t, "CUSTOM5") ||
           same_key(t, "PRELOAD") || same_key(t, "NOVBRMOVE") || same_key(t, "BUTTONWAIT") ||
           same_key(t, "NOCACHE") || same_key(t, "NOAUTOVEC") || same_key(t, "QUITKEY");
}

/* Give the icon newbase(.info) the place - and for a project icon, the
 * tooltypes - of oldbase(.info). Only with icon.library 44+ (OS 3.5): an
 * older one would drop a GlowIcon's colour image when writing it back. */
static void keep_icon(const char *oldbase, const char *newbase)
{
    struct DiskObject *o, *n;
    if (!IconBase || IconBase->lib_Version < 44)
        return;
    if (!(o = GetDiskObject((STRPTR)oldbase)))
        return;
    if (!(n = GetDiskObject((STRPTR)newbase))) {
        FreeDiskObject(o);
        return;
    }
    n->do_CurrentX = o->do_CurrentX;
    n->do_CurrentY = o->do_CurrentY;
    if (n->do_DrawerData && o->do_DrawerData) {
        n->do_DrawerData->dd_NewWindow.LeftEdge = o->do_DrawerData->dd_NewWindow.LeftEdge;
        n->do_DrawerData->dd_NewWindow.TopEdge = o->do_DrawerData->dd_NewWindow.TopEdge;
        n->do_DrawerData->dd_NewWindow.Width = o->do_DrawerData->dd_NewWindow.Width;
        n->do_DrawerData->dd_NewWindow.Height = o->do_DrawerData->dd_NewWindow.Height;
    }
    if (n->do_Type == WBPROJECT && o->do_ToolTypes) {
        STRPTR *nt = n->do_ToolTypes, *ot = o->do_ToolTypes, *merged, *saved = n->do_ToolTypes;
        int nn = 0, on = 0, m = 0, i, j;
        while (nt && nt[nn])
            nn++;
        while (ot[on])
            on++;
        merged = (STRPTR *)AllocVec((nn + on + 1) * sizeof(STRPTR), MEMF_CLEAR);
        if (merged) {
            int img = nn;                       /* a NewIcon's image lines come last */
            for (i = 0; i < nn; i++)
                if (image_tooltype((char *)nt[i])) {
                    img = i;
                    break;
                }
            for (i = 0; i < img; i++) {
                STRPTR t = nt[i];
                if (user_choice((char *)t))      /* your setting wins over the new default */
                    for (j = 0; j < on; j++)
                        if (same_key((char *)ot[j], (char *)t))
                            t = ot[j];
                merged[m++] = t;
            }
            for (j = 0; j < on; j++) {          /* the ones you added, before the image */
                int found = 0;
                if (image_tooltype((char *)ot[j]))
                    break;
                for (i = 0; i < nn && !found; i++)
                    found = same_key((char *)ot[j], (char *)nt[i]);
                if (!found)
                    merged[m++] = ot[j];
            }
            for (i = img; i < nn; i++)
                merged[m++] = nt[i];
            n->do_ToolTypes = merged;
            PutDiskObject((STRPTR)newbase, n);
            n->do_ToolTypes = saved;
            FreeVec(merged);
        }
    } else {
        PutDiskObject((STRPTR)newbase, n);
    }
    FreeDiskObject(n);
    FreeDiskObject(o);
}

/* keep_icon() for every icon in newdir that olddir has too */
static void keep_icons_in(const char *olddir, const char *newdir)
{
    BPTR lock = Lock((STRPTR)newdir, ACCESS_READ);
    struct FileInfoBlock *fib;
    char o[P], n[P];
    if (!lock)
        return;
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib))
            while (ExNext(lock, fib)) {
                const char *name = (const char *)fib->fib_FileName;
                int l = (int)strlen(name);
                if (fib->fib_DirEntryType >= 0 || l < 6 || !ends_nocase(name, ".info"))
                    continue;
                cat_path(o, olddir, name);
                cat_path(n, newdir, name);
                if (!exists(o))
                    continue;
                o[strlen(o) - 5] = 0;          /* GetDiskObject wants the name without .info */
                n[strlen(n) - 5] = 0;
                keep_icon(o, n);
            }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
}

/* ---- the new drawer ---------------------------------------------------- */

/* The one drawer a pack unpacks to (its name into name). */
static int unpacked_drawer(const char *dir, char *name)
{
    BPTR lock = Lock((STRPTR)dir, ACCESS_READ);
    struct FileInfoBlock *fib;
    int n = 0;
    name[0] = 0;
    if (!lock)
        return 0;
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib))
            while (ExNext(lock, fib))
                if (fib->fib_DirEntryType == ST_USERDIR) {
                    if (!n++)
                        strcpy(name, (const char *)fib->fib_FileName);
                }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return n == 1;
}

/* What the slave in dir says its version is ("" if it doesn't). */
static void slave_version(const char *dir, char *ver, int size)
{
    BPTR lock = Lock((STRPTR)dir, ACCESS_READ);
    struct FileInfoBlock *fib;
    char slave[P];
    ver[0] = slave[0] = 0;
    if (!lock)
        return;
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib))
            while (ExNext(lock, fib)) {
                const char *nm = (const char *)fib->fib_FileName;
                if (fib->fib_DirEntryType < 0 &&
                    (ends_nocase(nm, ".slave") || ends_nocase(nm, ".slav") || ends_nocase(nm, ".sla")))
                    cat_path(slave, dir, nm);
            }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    if (slave[0]) {
        BPTR fh = Open((STRPTR)slave, MODE_OLDFILE);
        unsigned char *buf = (unsigned char *)malloc(65536);
        SlaveInfo si;
        LONG n;
        if (fh && buf && (n = Read(fh, buf, 65536)) > 0 && slave_parse(buf, n, &si)) {
            strncpy(ver, si.version, size - 1);
            ver[size - 1] = 0;
        }
        if (fh)
            Close(fh);
        free(buf);
    }
}

static void log_line(const UpdateConf *c, const char *s)
{
    struct DateStamp ds;
    struct DateTime dt;
    char date[LEN_DATSTRING], tim[LEN_DATSTRING], line[P + 200];
    if (!c->log)
        return;
    DateStamp(&ds);
    memset(&dt, 0, sizeof(dt));
    dt.dat_Stamp = ds;
    dt.dat_Format = FORMAT_INT;
    dt.dat_StrDate = (STRPTR)date;
    dt.dat_StrTime = (STRPTR)tim;
    DateToStr(&dt);
    snprintf(line, sizeof(line), "%s %s %s\n", date, tim, s);
    FPuts(c->log, (STRPTR)line);
}

/* Where the old copy of path goes in olddir: WHDUpdate-Old/<name>, or
 * <name>.2, .3 ... when an older copy is there already. */
static void backup_name(const char *olddir, const char *path, char *backup)
{
    int i;
    cat_path(backup, olddir, leaf_of(path));
    for (i = 2; exists(backup) && i < 100; i++)
        snprintf(backup + strlen(olddir) + 1, P - strlen(olddir) - 1, "%s.%d", leaf_of(path), i);
}

int remove_game(BPTR log, const char *path, int keep)
{
    UpdateConf c;
    char vol[P], olddir[P], backup[P], info[P], backinfo[P], msg[P * 2 + 40];
    const char *colon = strchr(path, ':');

    memset(&c, 0, sizeof(c));
    c.log = log;
    if (!colon || !colon[1] || strlen(path) > P - 40)
        return -1;                             /* never a whole volume */
    snprintf(info, sizeof(info), "%s.info", path);
    if (keep) {
        memcpy(vol, path, colon + 1 - path);
        vol[colon + 1 - path] = 0;
        cat_path(olddir, vol, OLDDIR);
        if (!make_old_dir(olddir)) {
            say("  cannot make %s", olddir);
            return -1;
        }
        backup_name(olddir, path, backup);
        snprintf(backinfo, sizeof(backinfo), "%s.info", backup);
        if (!Rename((STRPTR)path, (STRPTR)backup)) {
            say("  cannot move %s (in use?)", path);
            return -1;
        }
        if (exists(info))
            Rename((STRPTR)info, (STRPTR)backinfo);
        snprintf(msg, sizeof(msg), "removed %s, moved to %s", path, backup);
    } else {
        delete_tree(path);
        if (exists(path)) {
            say("  could not delete all of %s (in use or protected?)", path);
            snprintf(msg, sizeof(msg), "FAILED to delete all of %s", path);
            log_line(&c, msg);
            return -1;
        }
        if (exists(info))
            DeleteFile((STRPTR)info);
        snprintf(msg, sizeof(msg), "deleted %s", path);
    }
    log_line(&c, msg);
    return 0;
}

int update_game(const UpdateConf *c, const char *path, const Pack *p, const char *installed)
{
    char vol[P], olddir[P], work[P], pack[P], unp[P], newname[108], newdir[P], newinfo[P];
    char backup[P], oldinfo[P], backinfo[P], url[P], enc[P], cmd[P * 2], ver[16], msg[P + 200];
    const char *colon = strchr(path, ':');
    unsigned long got = 0, crc = 0;
    BPTR fh;
    int rc, kept;

    if (!colon || strlen(path) > P - 40)
        return -1;
    memcpy(vol, path, colon + 1 - path);
    vol[colon + 1 - path] = 0;
    cat_path(olddir, vol, OLDDIR);
    cat_path(work, olddir, ".work");
    if (!make_old_dir(olddir)) {
        say("  cannot make %s", olddir);
        return -1;
    }
    delete_tree(work);
    if (!make_dir(work)) {
        say("  cannot make %s", work);
        return -1;
    }
    /* room for the pack and its unpacked files, with some to spare */
    if (free_bytes(vol) < p->size * 4 + 512 * 1024) {
        say("  not enough free space on %s for %s", vol, p->file);
        goto fail;
    }

    /* 1. download */
    cat_path(pack, work, p->file);
    if (!(fh = Open((STRPTR)pack, MODE_NEWFILE))) {
        say("  cannot write %s", pack);
        goto fail;
    }
    rc = -1;
    if (!c->ftp_only) {
        snprintf(url, sizeof(url), "%s%s/%s/%s", c->webdir, c->setdir, p->folder, p->file);
        url_encode(url, enc, sizeof(enc));
        rc = http_download(c->host, enc, fh, p->size, &got, &crc);
    }
    if (rc) {
        Seek(fh, 0, OFFSET_BEGINNING);
        SetFileSize(fh, 0, OFFSET_BEGINNING);
        snprintf(url, sizeof(url), "%s%s/%s/%s", c->ftpdir, c->setdir, p->folder, p->file);
        rc = ftp_download(c->host, url, fh, p->size, &got, &crc);
    }
    Close(fh);
    if (break_pressed()) {
        delete_tree(work);
        return -2;
    }
    if (rc || got != p->size || crc != p->crc) {
        say("  the download of %s failed or is damaged (%lu of %lu bytes)", p->file, got, p->size);
        goto fail;
    }

    /* 2. unpack */
    cat_path(unp, work, "x");
    if (!make_dir(unp))
        goto fail;
    if (ends_nocase(p->file, ".lzx")) {
        snprintf(cmd, sizeof(cmd), "UNLZX -x \"%s\"", pack);      /* unpacks into its current drawer */
        rc = run_in(unp, cmd);
    } else {
        snprintf(cmd, sizeof(cmd), "LhA -q -m x \"%s\" \"%s/\"", pack, unp);
        rc = run(cmd);
    }
    if (rc != 0 || !unpacked_drawer(unp, newname)) {
        say("  could not unpack %s%s", p->file,
               ends_nocase(p->file, ".lzx") ? " (needs C:UNLZX)" : " (needs C:LhA)");
        goto fail;
    }
    cat_path(newdir, unp, newname);
    snprintf(newinfo, sizeof(newinfo), "%s.info", newdir);

    /* 3. saves  4. icons inside the drawer */
    kept = carry_saves(path, newdir, "");
    keep_icons_in(path, newdir);
    if (break_pressed()) {
        delete_tree(work);
        return -2;
    }

    /* 5. swap: old -> WHDUpdate-Old/<name>[.n], new -> the old place */
    backup_name(olddir, path, backup);
    snprintf(oldinfo, sizeof(oldinfo), "%s.info", path);
    snprintf(backinfo, sizeof(backinfo), "%s.info", backup);
    if (!Rename((STRPTR)path, (STRPTR)backup)) {
        say("  cannot move the old %s aside (in use?)", path);
        goto fail;
    }
    if (exists(oldinfo))
        Rename((STRPTR)oldinfo, (STRPTR)backinfo);
    if (!Rename((STRPTR)newdir, (STRPTR)path)) {
        say("  cannot move the new drawer into place - the old one is back");
        Rename((STRPTR)backup, (STRPTR)path);
        if (exists(backinfo))
            Rename((STRPTR)backinfo, (STRPTR)oldinfo);
        goto fail;
    }
    if (exists(newinfo)) {
        char nb[P], ob[P];
        Rename((STRPTR)newinfo, (STRPTR)oldinfo);
        strcpy(nb, oldinfo);
        nb[strlen(nb) - 5] = 0;
        strcpy(ob, backinfo);
        ob[strlen(ob) - 5] = 0;
        if (exists(backinfo))
            keep_icon(ob, nb);
    } else if (exists(backinfo)) {
        copy_file(backinfo, oldinfo);            /* the pack has no drawer icon: keep yours */
    }
    delete_tree(work);

    /* Which pack this drawer now holds, for the next check: the slave does
     * not always carry the pack's whole version (2.1 in SevenCitiesOfGold
     * v2.1b). The same file and name WHDFetch writes. */
    {
        char mark[P];
        BPTR fh;
        cat_path(mark, path, "ArchiveName.txt");
        if ((fh = Open((STRPTR)mark, MODE_NEWFILE)) != 0) {
            FPuts(fh, (STRPTR)p->file);
            FPuts(fh, (STRPTR)"\n");
            Close(fh);
        }
    }
    slave_version(path, ver, sizeof(ver));
    say("  updated to %s%s; the old one is in %s", ver[0] ? ver : p->ver,
           kept ? ", saves kept" : "", backup);
    snprintf(msg, sizeof(msg), "updated %s %s -> %s (%s), %d save(s) kept, old copy %s",
             path, installed[0] ? installed : "?", ver[0] ? ver : p->ver, p->file, kept, backup);
    log_line(c, msg);
    return 0;

fail:
    delete_tree(work);
    snprintf(msg, sizeof(msg), "FAILED %s (%s), nothing changed", path, p->file);
    log_line(c, msg);
    return -1;
}
