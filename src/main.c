/* main.c - WHDUpdate: keeps your WHDLoad games up to date.
 *
 * It reads Retroplay's list of WHDLoad packs (the DAT, fetched from the pack
 * server and kept in PROGDIR:Cache), finds the game drawers in the drawers
 * you name (or on every volume), reads each game's slave for its version and
 * says which games have a newer pack. Only with UPDATE does it change
 * anything, and then only the games you say yes to (update.c).
 *
 * Started from its icon (or with GUI) it opens a window instead (gui.c).
 *
 *   WHDUpdate Games:            check the games under Games:
 *   WHDUpdate ALL               check every volume
 *   WHDUpdate Games: SHOWALL    list every game, not just the ones to look at
 *   WHDUpdate Games: REPORT=RAM:whd.txt   also write a full tab-separated list
 *   WHDUpdate Games: OFFLINE    use the DAT already in PROGDIR:Cache
 *   WHDUpdate Games: FTP        fetch it by FTP (it falls back to FTP anyway
 *                               when HTTP fails)
 *   WHDUpdate Games: UPDATE     update the games with a newer pack, asking
 *                               for each one (YES: without asking)
 *   WHDUpdate Games: GAME=Lotus#?   only the games whose drawer name fits
 *   WHDUpdate GUI               the window
 *
 * Return code 5 (WARN) when there are updates, 0 when there are none. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "match.h"
#include "amiga.h"
#include "engine.h"

static const char verstag[] __attribute__((used)) =
    "$VER: WHDUpdate " WHDU_VERSION " (31.10.2026) WHDLoad game updater";

struct Library *SocketBase = NULL;
struct Library *IconBase = NULL;

#define TEMPLATE "DIR/M,ALL/S,UPDATE/S,YES/S,GAME/K,SHOWALL/S,REPORT/K,OFFLINE/S,FTP/S,DEPTH/K/N,GUI/S"
enum { A_DIR, A_ALL, A_UPDATE, A_YES, A_GAME, A_SHOWALL, A_REPORT, A_OFFLINE, A_FTP, A_DEPTH,
       A_GUI, A_COUNT };

static int gui_on;

/* ---- messages: printed in a Shell, shown on the status line in the window */

void say(const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (gui_on) {
        gui_status(buf);
    } else {
        printf("%s\n", buf);
        fflush(stdout);
    }
}

/* progress that only matters while it happens */
void busy(const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (gui_on)
        gui_status(buf);
}

int break_pressed(void)
{
    if (gui_on && gui_poll())
        return 1;
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
}

/* ---- the Shell ------------------------------------------------------- */

typedef struct {
    int showall;
    BPTR report;
} Out;

static void cli_seen(void *ctx, const Game *g)
{
    Out *o = (Out *)ctx;
    char line[700];
    if (o->showall || g->status == M_UPDATE || g->status == M_CHECK) {
        const char *iv = g->installed[0] ? g->installed : "?";
        if (g->pack && g->status != M_UNKNOWN)
            snprintf(line, sizeof(line), "%-8s %s  %s -> %s  (%s)\n", status_word(g->status), g->path,
                     iv, g->pack->ver, g->pack->file);
        else if (g->pack)
            snprintf(line, sizeof(line), "%-8s %s  newest pack %s\n", status_word(g->status), g->path,
                     g->pack->file);
        else
            snprintf(line, sizeof(line), "%-8s %s\n", status_word(g->status), g->path);
        fputs(line, stdout);
        fflush(stdout);
    }
    if (o->report) {
        snprintf(line, sizeof(line), "%s\t%s\t%s\t%s\t%s\t%s\n", status_word(g->status), g->path,
                 g->installed, g->pack ? g->pack->ver : "", g->pack ? g->pack->file : "",
                 g->pack ? g->pack->folder : "");
        FPuts(o->report, (STRPTR)line);
    }
}

/* y / n / a(ll) / q(uit); no input (not a console) counts as q */
static int ask(const char *question)
{
    char buf[16];
    LONG n;
    printf("%s [y]es [n]o [a]ll [q]uit: ", question);
    fflush(stdout);
    n = Read(Input(), buf, sizeof(buf) - 1);
    if (n <= 0) {
        printf("(no answer - use YES to update without asking)\n");
        return 'q';
    }
    buf[n] = 0;
    switch (buf[0] | 32) {
    case 'y': return 'y';
    case 'a': return 'a';
    case 'q': return 'q';
    default:  return 'n';
    }
}

static int cli_updates(Engine *e, int yes)
{
    BPTR log;
    long i;
    int done = 0, failed = 0, n = 0, total = 0, first = 1, stop = 0;
    char q[700];

    for (i = 0; i < e->n; i++)
        total += e->games[i]->status == M_UPDATE;
    if (!total) {
        printf("Nothing to update.\n");
        return 0;
    }
    log = engine_open_log();
    printf("\n%d game(s) to update. The old copies go to WHDUpdate-Old on the same volume.\n", total);
    for (i = 0; i < e->n && !stop; i++) {
        Game *g = e->games[i];
        if (g->status != M_UPDATE)
            continue;
        n++;
        if (!yes) {
            int a;
            snprintf(q, sizeof(q), "(%d/%d) %s %s -> %s (%lu KB)?", n, total, g->path,
                     g->installed, g->pack->ver, (g->pack->size + 1023) / 1024);
            a = ask(q);
            if (a == 'q')
                break;
            if (a == 'n')
                continue;
            if (a == 'a')
                yes = 1;
        } else {
            printf("(%d/%d) %s %s -> %s\n", n, total, g->path, g->installed, g->pack->ver);
        }
        if (!first)
            Delay(100);                   /* two seconds between downloads: go easy on the server */
        first = 0;
        switch (engine_update(e, g, log)) {
        case 0: done++; break;
        case -2: printf("*** Stopped.\n"); stop = 1; break;
        default: failed++;
        }
    }
    if (log)
        Close(log);
    printf("\n%d updated, %d failed. Log: %s\n", done, failed, LOGFILE);
    return failed ? -1 : 0;
}

static int cli(Engine *e, LONG *args)
{
    Out out;
    int rc = RETURN_OK, depth = 3, stopped = 0;

    memset(&out, 0, sizeof(out));
    if (args[A_DEPTH])
        depth = (int)*(LONG *)args[A_DEPTH];
    out.showall = args[A_SHOWALL] != 0;
    e->seen = cli_seen;
    e->seen_ctx = &out;
    if (engine_dat(e, args[A_OFFLINE] != 0))
        return RETURN_FAIL;
    if (args[A_REPORT]) {
        out.report = Open((STRPTR)args[A_REPORT], MODE_NEWFILE);
        if (!out.report)
            printf("Cannot write %s\n", (char *)args[A_REPORT]);
        else
            FPuts(out.report, (STRPTR)"status\tdrawer\tinstalled\tnewest\tpack\tfolder\n");
    }
    if (args[A_ALL]) {
        stopped = engine_scan(e, "ALL", depth, -1) == -2;
    } else {
        STRPTR *d;
        for (d = (STRPTR *)args[A_DIR]; *d && !stopped; d++) {
            int s = engine_scan(e, (char *)*d, depth, -1);
            if (s == -1)
                printf("Cannot find %s\n", (char *)*d);
            else if (s == -2)
                stopped = 1;
        }
    }
    if (out.report)
        Close(out.report);
    if (stopped)
        printf("*** Stopped.\n");
    printf("\n%ld games: %ld up to date, %ld with an update, %ld to check by hand,\n"
           "%ld without a version in the slave, %ld not in the pack list, %ld newer than the pack.\n",
           e->n, e->st[M_CURRENT], e->st[M_UPDATE], e->st[M_CHECK],
           e->st[M_UNKNOWN], e->st[M_NOMATCH], e->st[M_NEWER]);
    if (e->st[M_CHECK])
        printf("\"check\": the two versions can be read either way (1.51 vs 1.6).\n");
    if (e->st[M_UPDATE])
        rc = RETURN_WARN;
    if (args[A_UPDATE] && !stopped)
        rc = cli_updates(e, args[A_YES] != 0) == 0 ? RETURN_OK : RETURN_WARN;
    return rc;
}

/* Started from Workbench, or by Run, a program gets the default 4 KB stack:
 * too little for the window plus the pack list decoder (it overran it and
 * hung on a 3.1 A1200). So it runs on its own 32 KB, through StackSwap() -
 * as PrismRTG's update tool does; across the swap only globals are used.
 * The swapped function must take NO arguments: gcc pushes arguments on the
 * new stack and, with its deferred pops, takes them off again only after the
 * swap back - on the old stack, which is then four bytes short, and the
 * program returns to its caller with every saved register wrong (the
 * machine died at exit on the A500; 0.6.x, found 2026-11-03). */

/* WHDU_TRACE builds write progress marks to T:whd.trace through dos.library
 * alone (open, write, close each time), to place a crash that stdio cannot
 * report. */
#ifdef WHDU_TRACE
static void tmark(const char *m)
{
    BPTR f = Open((STRPTR)"T:whd.trace", MODE_READWRITE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, (APTR)m, strlen(m));
        Write(f, (APTR)"\n", 1);
        Close(f);
    }
}
#define TMARK(m) tmark(m)
#else
#define TMARK(m)
#endif
#define NEED_STACK 32768UL
static struct StackSwapStruct swap;
static APTR swapMem;
static int swapRc, g_argc;
static char **g_argv;

static int whdu_main(void);

int main(int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    TMARK("main");
    g_argc = argc;
    g_argv = argv;
    if ((ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower >= NEED_STACK - 4096)
        return whdu_main();
    if (!(swapMem = AllocVec(NEED_STACK, MEMF_ANY))) {
        printf("WHDUpdate: not enough memory for its stack\n");
        return RETURN_FAIL;
    }
    swap.stk_Lower = swapMem;
    swap.stk_Upper = (ULONG)swapMem + NEED_STACK;
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    StackSwap(&swap);
    swapRc = whdu_main();
    StackSwap(&swap);
    TMARK("swapped back");
    FreeVec(swapMem);
    TMARK("stack freed, returning");
    return swapRc;
}

static int whdu_main(void)
{
    LONG args[A_COUNT] = {0};
    struct RDArgs *rda = NULL;
    Engine e;
    int rc, argc = g_argc;

    TMARK("whdu_main");
    if (engine_init(&e)) {
        printf("WHDUpdate: out of memory\n");
        return RETURN_FAIL;
    }
    TMARK("engine_init done");
    if (argc) {                           /* from a Shell */
        rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
        TMARK("ReadArgs done");
        if (!rda) {
            PrintFault(IoErr(), (STRPTR)"WHDUpdate");
            engine_free(&e);
            return RETURN_ERROR;
        }
        if (!args[A_DIR] && !args[A_ALL] && !args[A_GUI]) {
            printf("WHDUpdate " WHDU_VERSION " - keeps your WHDLoad games up to date.\n"
                   "Name the drawers your games are in, or ALL for every volume:\n"
                   "  WHDUpdate Games:  [UPDATE [YES]] [GAME=pattern] [SHOWALL] [REPORT=file]\n"
                   "                    [OFFLINE] [FTP] [DEPTH=n]\n"
                   "  WHDUpdate GUI     the window (also from the icon)\n");
            TMARK("usage printed");
            FreeArgs(rda);
            TMARK("FreeArgs done");
            engine_free(&e);
            TMARK("engine_free done");
            return RETURN_WARN;
        }
        if (args[A_GAME]) {
            LONG plen = (LONG)strlen((char *)args[A_GAME]) * 2 + 2;
            e.pattern = (char *)malloc(plen);
            if (!e.pattern || ParsePatternNoCase((STRPTR)args[A_GAME], (STRPTR)e.pattern, plen) < 0) {
                printf("GAME=%s is not a pattern I can use\n", (char *)args[A_GAME]);
                FreeArgs(rda);
                engine_free(&e);
                return RETURN_ERROR;
            }
        }
        if (args[A_FTP])
            e.prefs.ftp_only = 1;
    }
    IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    if (!args[A_OFFLINE]) {
        SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
        if (!SocketBase && argc && !args[A_GUI])
            printf("No TCP/IP stack running; using the saved pack list.\n");
    }

    if (!argc || args[A_GUI]) {
        char dirs[400];
        dirs[0] = 0;
        if (args[A_DIR]) {
            STRPTR *d;
            for (d = (STRPTR *)args[A_DIR]; *d; d++) {
                if (dirs[0])
                    strncat(dirs, ";", sizeof(dirs) - strlen(dirs) - 1);
                strncat(dirs, (char *)*d, sizeof(dirs) - strlen(dirs) - 1);
            }
        }
        gui_on = 1;
        rc = gui_main(&e, dirs[0] ? dirs : NULL);
        gui_on = 0;
    } else {
        rc = cli(&e, args);
    }

    TMARK("run done");
    engine_free(&e);
    TMARK("engine_free done");
    if (IconBase)
        CloseLibrary(IconBase);
    if (SocketBase)
        CloseLibrary(SocketBase);
    if (rda)
        FreeArgs(rda);
    return rc;
}
