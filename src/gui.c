/* gui.c - the WHDUpdate window (GadTools, Kickstart 2.04+).
 *
 * Laid out from the font: every row is the font's height plus a little, so
 * on a plain PAL or NTSC screen with Topaz 8 the whole window needs about
 * 140 lines - it fits a 640x200 screen. When the screen font is so big that
 * even the smallest layout would not fit, the window uses Topaz 8 instead.
 * The game list always uses a fixed-width font, so its columns line up
 * when the screen font is proportional.
 *
 *   Drawers [Games1:Games;Games2:              ] [Check]
 *   Find    [lemm             ]       Show <Updates>
 *   [2594 games: 1116 updates, 9 to check, 2 marked     ]
 *   +----------------------------------------------------+
 *   |* update   Cabal                       1.2a > 1.3   |
 *   |  update   CastleVania                 1.1 > 1.2    |
 *   +----------------------------------------------------+
 *   [Update marked] [Mark all] [Mark none] [Stop]
 *   [status line                                        ]
 *
 * Long jobs (the check, the updates) run in this task; break_pressed() calls
 * gui_poll() between steps, which keeps the window alive and sees Stop. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/text.h>
#include <graphics/gfxbase.h>
#include <devices/inputevent.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "match.h"
#include "amiga.h"
#include "engine.h"
#include "guiprefs.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Library *GadToolsBase = NULL;

#define M 6                       /* margin */
#define GAP 4

enum { G_DIRS, G_CHECK, G_FIND, G_SUMMARY, G_SET, G_SHOW, G_LIST, G_UPDATE, G_MARKALL, G_MARKNONE,
       G_STOP, G_STATUS, G_COUNT };

static const char *show_labels[] = {"Updates", "All games", "To check", "Not found", "No version",
                                    "Missing", "Not wanted", NULL};
#define SHOW_UNWANTED 6           /* games of a language or system turned off: to remove */
/* which pack set the list shows: 0 = all of them, else pack_sets[n - 1] */
static const char *set_labels[] = {"All sets", "Games", "Games Beta", "Demos", "Demos Beta",
                                   "Magazines", NULL};

enum { MN_CHECK = 1, MN_UPDATE, MN_ABOUT, MN_QUIT, MN_SERVER, MN_SETS, MN_LANG, MN_SYS, MN_FTPONLY,
       MN_REMOVE };

static struct NewMenu menu_def[] = {
    {NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Check", (STRPTR)"C", 0, 0, (APTR)MN_CHECK},
    {NM_ITEM, (STRPTR)"Update marked", (STRPTR)"U", 0, 0, (APTR)MN_UPDATE},
    {NM_ITEM, (STRPTR)"Remove marked...", (STRPTR)"R", 0, 0, (APTR)MN_REMOVE},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"About...", NULL, 0, 0, (APTR)MN_ABOUT},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)MN_QUIT},
    {NM_TITLE, (STRPTR)"Settings", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Server...", (STRPTR)"S", 0, 0, (APTR)MN_SERVER},
    {NM_ITEM, (STRPTR)"Pack sets...", (STRPTR)"P", 0, 0, (APTR)MN_SETS},
    {NM_ITEM, (STRPTR)"Languages...", (STRPTR)"L", 0, 0, (APTR)MN_LANG},
    {NM_ITEM, (STRPTR)"Systems...", (STRPTR)"Y", 0, 0, (APTR)MN_SYS},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Use FTP only", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)MN_FTPONLY},
    {NM_END, NULL, NULL, 0, 0, NULL}};

static struct TextAttr topaz8 = {(STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT};

static struct {
    Engine *e;
    struct Screen *scr;
    APTR vi;
    struct Window *win;
    struct Gadget *glist, *g[G_COUNT];
    struct TextAttr ta, lvta;     /* gadgets / list */
    struct TextFont *font, *lvfont;
    struct RastPort mrp;          /* for measuring text */
    struct List list;
    int filter, set, busy, stop, quit, cols;
    long marked, doomed;          /* counted by set_summary */
    Game *sel;                    /* the game clicked last: stays highlighted */
    long top;                     /* the list's scroll position */
    struct IBox lvbox;            /* where the list is in the window */
    long anchor;                  /* the row clicked last: Shift-click marks from it */
    long press;                   /* the row the button went down on (-1 none, -2 not the list) */
    long dragrow;                 /* the row a drag is over, as the status line says */
    struct Hook lvhook;           /* draws the list's rows (list_draw) */
    UWORD pens[4];                /* text, background, fill, fill text: for paint_rows */
    int painted;                  /* paint_rows has drawn a drag's run over the list */
    long pvtop;                   /* the list's top when it did */
    char dirs[400], status[200], summary[120];
    char find[40];                /* part of a name: only those games are shown */
    struct Menu *menu;
} G;

/* ---- small helpers ------------------------------------------------------ */

static void new_list(struct List *l)      /* NewList() is in amiga.lib */
{
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_Tail = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}

static int tlen(const char *s)
{
    return TextLength(&G.mrp, (STRPTR)s, strlen(s));
}

static int bh(void) { return G.font->tf_YSize + 6; }

/* The Drawers field is the games' drawers in the settings. */
static void load_dirs(void)
{
    strncpy(G.dirs, G.e->prefs.dirs[FAM_GAMES], sizeof(G.dirs) - 1);
}

static void save_dirs(void)
{
    strncpy(G.e->prefs.dirs[FAM_GAMES], G.dirs, sizeof(G.e->prefs.dirs[0]) - 1);
    prefs_save(&G.e->prefs);
}

/* ---- the list ------------------------------------------------------------ */

/* Does name contain part, ignoring case? */
static int contains(const char *name, const char *part)
{
    for (; *name; name++) {
        const char *a = name, *b = part;
        while (*b && *a && ((*a >= 'A' && *a <= 'Z' ? *a | 32 : *a) ==
                            (*b >= 'A' && *b <= 'Z' ? *b | 32 : *b)))
            a++, b++;
        if (!*b)
            return 1;
    }
    return !*part;
}

/* Is g in the set the Set cycle picks? A game goes by the set its pack is
 * from; one with no pack by the family of the drawer it is in (shown with
 * the family's main set). */
static int in_set(const Game *g)
{
    int s = G.set - 1;
    if (s < 0)
        return 1;
    if (g->pack)
        return g->pack->set == s;
    return g->family == pack_sets[s].family &&
           (s == SET_GAMES || s == SET_DEMOS || s == SET_MAGS);
}

static int shown(const Game *g)
{
    if (G.find[0] && !contains(g->name, G.find))
        return 0;
    if (!in_set(g))
        return 0;
    if (g->status == M_MISSING)               /* not installed: only under Missing */
        return G.filter == 5;
    switch (G.filter) {
    case 0: return g->status == M_UPDATE;
    case 2: return g->status == M_CHECK || g->status == M_NEWER;
    case 3: return g->status == M_NOMATCH;
    case 4: return g->status == M_UNKNOWN;
    case 5: return 0;
    case SHOW_UNWANTED: return engine_unwanted(G.e, g);
    default: return 1;
    }
}

/* Not wanted lists the games to remove: any of them can be marked there,
 * and its marks are apart from the update marks. */
static int markable(const Game *g)
{
    if (G.filter == SHOW_UNWANTED)
        return g->path != NULL;
    return g->pack && (g->status == M_UPDATE || g->status == M_CHECK);
}

static int is_marked(const Game *g)
{
    return G.filter == SHOW_UNWANTED ? g->doomed : g->marked;
}

static void make_label(Game *g)
{
    static const char *words[] = {"not found", "no version", "current", "update", "newer", "check",
                                  "missing"};
    char vers[40];
    int namew;
    if (!g->pack)
        vers[0] = 0;
    else if (g->status == M_MISSING)
        snprintf(vers, sizeof(vers), "%s", g->pack->ver);
    else
        snprintf(vers, sizeof(vers), "%s > %s", g->installed[0] ? g->installed : "?", g->pack->ver);
    namew = G.cols - 2 - 11 - (int)strlen(vers) - 1;
    if (namew < 8)
        namew = 8;
    snprintf(g->label, sizeof(g->label), "%c %-10s %-*.*s %s", is_marked(g) ? '*' : ' ',
             words[g->status], namew, namew, g->name, vers);
    g->node.ln_Name = g->label;
}

/* One row of the list, for GadTools (GTLV_CallBack): a marked game is
 * drawn in the highlight colours, like the clicked one, so a run marked by
 * a drag or a Shift-click shows as one block. */
/* The rows paint_rows drew, and how: GadTools redraws the row under the
 * pointer during a drag, and draws it the same way from here, or the two
 * would flicker against each other. */
#define PV_ROWS 64
static Game *pvgame[PV_ROWS];
static UBYTE pvlit[PV_ROWS];
static int pvn;

static void draw_row(struct RastPort *rp, UWORD bg, UWORD fg, WORD x0, WORD y0, WORD x1, WORD y1,
                     const char *t)
{
    struct TextExtent te;
    ULONG fit;
    SetDrMd(rp, JAM1);
    SetAPen(rp, bg);
    RectFill(rp, x0, y0, x1, y1);
    /* 2 pixels in, where GadTools' own rows start */
    fit = TextFit(rp, (STRPTR)t, strlen(t), &te, NULL, 1, x1 - x0 - 1, 32767);
    SetAPen(rp, fg);
    Move(rp, x0 + 2, y0 + rp->TxBaseline);
    Text(rp, (STRPTR)t, fit);
}

static ULONG list_draw(struct Hook *h __asm("a0"), struct Node *nd __asm("a2"),
                       struct LVDrawMsg *m __asm("a1"))
{
    UWORD *pens;
    int hi;
    (void)h;
    if (m->lvdm_MethodID != LV_DRAW)
        return LVCB_UNKNOWN;
    pens = m->lvdm_DrawInfo->dri_Pens;
    hi = is_marked((Game *)nd) || m->lvdm_State == LVR_SELECTED;
    if (G.painted) {
        int k;
        for (k = 0; k < pvn; k++)
            if (pvgame[k] == (Game *)nd) {
                hi = pvlit[k];
                break;
            }
    }
    draw_row(m->lvdm_RastPort, pens[hi ? FILLPEN : BACKGROUNDPEN], pens[hi ? FILLTEXTPEN : TEXTPEN],
             m->lvdm_Bounds.MinX, m->lvdm_Bounds.MinY, m->lvdm_Bounds.MaxX, m->lvdm_Bounds.MaxY,
             nd->ln_Name ? nd->ln_Name : "");
    return LVCB_OK;
}

/* Where the list is scrolled to: kept across a redraw (GadTools 39+ can be
 * asked; 2.04 starts at the top again). */
static void remember_top(void)
{
    ULONG top = 0;
    if (G.win && G.g[G_LIST] && GadToolsBase->lib_Version >= 39 &&
        GT_GetGadgetAttrs(G.g[G_LIST], G.win, NULL, GTLV_Top, (ULONG)&top, TAG_END) == 1)
        G.top = (long)top;
}

/* The clicked game's place in the list shown, or ~0 when it isn't in it. */
static ULONG sel_index(void)
{
    struct Node *nd;
    ULONG i = 0;
    if (!G.sel)
        return ~0UL;
    for (nd = G.list.lh_Head; nd->ln_Succ; nd = nd->ln_Succ, i++)
        if (nd == &G.sel->node)
            return i;
    return ~0UL;
}

static void detach_list(void)
{
    if (G.win && G.g[G_LIST]) {
        remember_top();
        GT_SetGadgetAttrs(G.g[G_LIST], G.win, NULL, GTLV_Labels, ~0UL, TAG_END);
    }
}

static void attach_list(void)
{
    G.painted = 0;                /* GadTools draws every row again */
    pvn = 0;
    if (G.win && G.g[G_LIST])
        GT_SetGadgetAttrs(G.g[G_LIST], G.win, NULL, GTLV_Labels, (ULONG)&G.list,
                          GTLV_Top, G.top, GTLV_Selected, sel_index(), TAG_END);
}

static void rebuild_list(void)
{
    long i;
    detach_list();
    new_list(&G.list);
    for (i = 0; i < G.e->n; i++) {
        Game *g = G.e->games[i];
        if (shown(g)) {
            make_label(g);
            AddTail(&G.list, &g->node);
        }
    }
    attach_list();
}

static Game *nth_shown(long n)
{
    struct Node *nd;
    for (nd = G.list.lh_Head; nd->ln_Succ; nd = nd->ln_Succ)
        if (!n--)
            return (Game *)nd;    /* the Node is the Game's first member */
    return NULL;
}

/* The row of the list at window position x,y, or -1 off the rows (the
 * scroller, the border). The rows start 2 pixels into the box. */
static long row_at(WORD x, WORD y)
{
    long r;
    if (x < G.lvbox.Left + 2 || x >= G.lvbox.Left + G.lvbox.Width - 20 ||
        y < G.lvbox.Top + 2 || y >= G.lvbox.Top + G.lvbox.Height - 2)
        return -1;
    remember_top();
    r = G.top + (y - G.lvbox.Top - 2) / G.lvfont->tf_YSize;
    return nth_shown(r) ? r : -1;
}

/* Rows a..b (either order) marked or unmarked; what can't be marked is
 * passed over. How many changed. */
static long mark_rows(long a, long b, int on)
{
    struct Node *nd;
    long i = 0, n = 0;
    if (a > b) {
        i = a; a = b; b = i; i = 0;
    }
    for (nd = G.list.lh_Head; nd->ln_Succ && i <= b; nd = nd->ln_Succ, i++) {
        Game *g = (Game *)nd;
        if (i < a || !markable(g) || !is_marked(g) == !on)
            continue;
        if (G.filter == SHOW_UNWANTED)
            g->doomed = on;
        else
            g->marked = on;
        n++;
    }
    return n;
}

/* Rows a..b written again (their marks changed): only those labels, not
 * the whole list - that takes a second on a 68030 with 3,000 games. */
static void relabel_rows(long a, long b)
{
    struct Node *nd;
    long i = 0;
    if (a > b) {
        i = a; a = b; b = i; i = 0;
    }
    detach_list();
    for (nd = G.list.lh_Head; nd->ln_Succ && i <= b; nd = nd->ln_Succ, i++)
        if (i >= a)
            make_label((Game *)nd);
    attach_list();
}

/* The button is held over row r after going down on another: say what
 * letting go there will do. */
/* The rows the list shows, drawn straight into the window while the
 * button is held: with preview, a drag's run (press..dragrow) as letting
 * go will leave it; without, as the marks are. GadTools only redraws its
 * rows when it is given the list again, which it can't be mid-drag. */
static void paint_rows(int preview)
{
    struct RastPort *rp = G.win->RPort;
    struct TextFont *was = rp->Font;
    struct Node *nd;
    Game *f = preview ? nth_shown(G.press) : NULL;
    long lo = G.press < G.dragrow ? G.press : G.dragrow;
    long hi = G.press < G.dragrow ? G.dragrow : G.press;
    long i = 0, rows = (G.lvbox.Height - 4) / G.lvfont->tf_YSize;
    int on = f && !is_marked(f), k;
    /* only the rows that change: the same top as last time, each row is
     * redrawn only when its colour is not what it was */
    int same = G.painted && pvn;
    WORD x0 = G.lvbox.Left + 2, x1 = G.lvbox.Left + G.lvbox.Width - 19;
    remember_top();
    if (G.top != G.pvtop)
        same = 0;
    if (rows > PV_ROWS)
        rows = PV_ROWS;
    SetFont(rp, G.lvfont);
    for (nd = G.list.lh_Head; nd->ln_Succ && i < G.top + rows; nd = nd->ln_Succ, i++) {
        Game *g = (Game *)nd;
        WORD y0;
        int lit;
        if (i < G.top)
            continue;
        k = (int)(i - G.top);
        lit = f && i >= lo && i <= hi && markable(g) ? on : is_marked(g);
        if (same && k < pvn && pvgame[k] == g && pvlit[k] == lit)
            continue;
        pvgame[k] = g;
        pvlit[k] = (UBYTE)lit;
        y0 = G.lvbox.Top + 2 + (WORD)k * G.lvfont->tf_YSize;
        draw_row(rp, G.pens[lit ? 2 : 1], G.pens[lit ? 3 : 0], x0, y0, x1, y0 + G.lvfont->tf_YSize - 1,
                 g->label);
    }
    if (!same)
        pvn = (int)(i - G.top) > 0 ? (int)(i - G.top) : 0;
    SetFont(rp, was);
    G.painted = preview;
    G.pvtop = G.top;
}

static void drag_preview(long r)
{
    char txt[160];
    Game *f, *a, *b;
    long n = 0, i;
    if (r < 0 || !(f = nth_shown(G.press)))
        return;
    remember_top();
    if (r == G.dragrow && G.top == G.pvtop && G.painted)
        return;                   /* the same row, not scrolled: nothing new */
    G.dragrow = r;
    paint_rows(1);
    if (r == G.press) {
        gui_status("Drag over the games to mark them all");
        return;
    }
    {                             /* one walk down the list: it can be long */
        long lo = G.press < r ? G.press : r, hi = G.press < r ? r : G.press;
        struct Node *nd;
        a = b = NULL;
        for (nd = G.list.lh_Head, i = 0; nd->ln_Succ && i <= hi; nd = nd->ln_Succ, i++) {
            if (i < lo)
                continue;
            if (i == lo)
                a = (Game *)nd;
            b = (Game *)nd;
            n += markable((Game *)nd);
        }
    }
    snprintf(txt, sizeof(txt), "%s %ld: %s to %s - let go to %s", is_marked(f) ? "Unmarking" : "Marking",
             n, a ? a->name : "?", b ? b->name : "?", is_marked(f) ? "unmark" : "mark");
    gui_status(txt);
}

static void set_summary(void)
{
    Engine *e = G.e;
    long marked = 0, n = 0, upd = 0, chk = 0, miss = 0, unw = 0, doomed = 0, i;
    for (i = 0; i < e->n; i++) {          /* counted in the set the Set cycle picks */
        const Game *g = e->games[i];
        if (!in_set(g))
            continue;
        marked += g->marked;
        doomed += g->doomed;
        unw += engine_unwanted(e, g);
        if (g->status == M_MISSING)
            miss++;
        else {
            n++;
            upd += g->status == M_UPDATE;
            chk += g->status == M_CHECK || g->status == M_NEWER;
        }
    }
    G.marked = marked;
    G.doomed = doomed;
    if (!e->n)
        snprintf(G.summary, sizeof(G.summary), "No games checked yet");
    else if (G.filter == SHOW_UNWANTED)
        snprintf(G.summary, sizeof(G.summary), "%ld of %ld games not wanted, %ld marked to remove",
                 unw, n, doomed);
    else
        snprintf(G.summary, sizeof(G.summary), "%ld games: %ld updates, %ld to check, %ld missing, %ld marked",
                 n, upd, chk, miss, marked);
    if (G.win && G.g[G_SUMMARY])
        GT_SetGadgetAttrs(G.g[G_SUMMARY], G.win, NULL, GTTX_Text, (ULONG)G.summary, TAG_END);
}

/* After the marks changed: the counts, and what changed, if anything to
 * say (a range), go in the status line over what the last run left there. */
static void marks_changed(const char *what)
{
    char txt[120];
    long n;
    set_summary();
    n = G.filter == SHOW_UNWANTED ? G.doomed : G.marked;
    snprintf(txt, sizeof(txt), "%s%s%ld marked to %s", what ? what : "", what ? ", " : "", n,
             G.filter == SHOW_UNWANTED ? "remove" : "update");
    gui_status(txt);
}

/* ---- gadgets ------------------------------------------------------------- */

static int min_width(void)
{
    int b = tlen("Update marked") + 16;
    return 2 * M + 4 * b + 3 * GAP;
}

static int min_height(void)
{
    /* five rows of gadgets + a list of at least four lines */
    return 4 + 5 * bh() + 5 * GAP + 4 * G.lvfont->tf_YSize + 4 + 4;
}

static int make_gadgets(void)
{
    struct NewGadget ng;
    struct Gadget *gad;
    struct Window *w = G.win;
    int left = w->BorderLeft + M, right = w->Width - w->BorderRight - M;
    int top = w->BorderTop + 4, bottom = w->Height - w->BorderBottom - 4;
    int h = bh(), lw, cw, sw, y, bw, i, lvh;
    static const char *btn[] = {"Update marked", "Mark all", "Mark none", "Stop"};
    btn[0] = G.filter == SHOW_UNWANTED ? "Remove marked" : "Update marked";

    memset(G.g, 0, sizeof(G.g));
    gad = CreateContext(&G.glist);
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = G.vi;
    ng.ng_TextAttr = &G.ta;

    /* Drawers [....] [Check] */
    lw = tlen("Drawers") + 8;
    cw = tlen("Check") + 24;
    ng.ng_LeftEdge = left + lw;
    ng.ng_TopEdge = top;
    ng.ng_Width = right - left - lw - GAP - cw;
    ng.ng_Height = h;
    ng.ng_GadgetText = (UBYTE *)"Drawers";
    ng.ng_GadgetID = G_DIRS;
    ng.ng_Flags = PLACETEXT_LEFT;
    G.g[G_DIRS] = gad = CreateGadget(STRING_KIND, gad, &ng, GTST_String, (ULONG)G.dirs,
                                     GTST_MaxChars, sizeof(G.dirs) - 1, GA_Disabled, G.busy, TAG_END);
    ng.ng_LeftEdge = right - cw;
    ng.ng_Width = cw;
    ng.ng_GadgetText = (UBYTE *)"Check";
    ng.ng_GadgetID = G_CHECK;
    ng.ng_Flags = PLACETEXT_IN;
    G.g[G_CHECK] = gad = CreateGadget(BUTTON_KIND, gad, &ng, GA_Disabled, G.busy, TAG_END);

    /* Find [....] Set <cycle> Show <cycle> */
    y = top + h + GAP;
    lw = tlen("Drawers") + 8;                 /* lined up with the Drawers field */
    cw = tlen("No version") + 32;
    sw = tlen("Games Beta") + 32;
    ng.ng_LeftEdge = left + lw;
    ng.ng_TopEdge = y;
    ng.ng_Width = right - left - lw - GAP - (tlen("Set") + 8) - sw - GAP - (tlen("Show") + 8) - cw;
    ng.ng_GadgetText = (UBYTE *)"Find";
    ng.ng_GadgetID = G_FIND;
    ng.ng_Flags = PLACETEXT_LEFT;
    G.g[G_FIND] = gad = CreateGadget(STRING_KIND, gad, &ng, GTST_String, (ULONG)G.find,
                                     GTST_MaxChars, sizeof(G.find) - 1, TAG_END);
    ng.ng_LeftEdge = right - cw - GAP - (tlen("Show") + 8) - sw;
    ng.ng_Width = sw;
    ng.ng_GadgetText = (UBYTE *)"Set";
    ng.ng_GadgetID = G_SET;
    G.g[G_SET] = gad = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, (ULONG)set_labels,
                                    GTCY_Active, G.set, GA_Disabled, G.busy, TAG_END);
    ng.ng_LeftEdge = right - cw;
    ng.ng_Width = cw;
    ng.ng_GadgetText = (UBYTE *)"Show";
    ng.ng_GadgetID = G_SHOW;
    G.g[G_SHOW] = gad = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, (ULONG)show_labels,
                                     GTCY_Active, G.filter, GA_Disabled, G.busy, TAG_END);

    /* [summary] */
    y += h + GAP;
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = y;
    ng.ng_Width = right - left;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = G_SUMMARY;
    G.g[G_SUMMARY] = gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)G.summary,
                                        GTTX_CopyText, TRUE, GTTX_Border, TRUE, TAG_END);

    /* the list */
    y += h + GAP;
    lvh = bottom - 2 * h - 2 * GAP - y;
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = y;
    ng.ng_Width = right - left;
    ng.ng_Height = lvh;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = G_LIST;
    ng.ng_TextAttr = &G.lvta;
    G.lvbox.Left = ng.ng_LeftEdge;
    G.lvbox.Top = ng.ng_TopEdge;
    G.lvbox.Width = ng.ng_Width;
    G.lvbox.Height = ng.ng_Height;
    G.cols = (ng.ng_Width - 24) / G.lvfont->tf_XSize;
    if (G.cols > (int)sizeof(((Game *)0)->label) - 1)
        G.cols = sizeof(((Game *)0)->label) - 1;
    /* ShowSelected NULL: the clicked game stays highlighted; marked games
     * are drawn highlighted too (list_draw, GadTools 39+) */
    {
        struct DrawInfo *dri = GetScreenDrawInfo(G.scr);
        ULONG maxpen = 0;
        if (dri) {
            UWORD pens[4], i;
            pens[0] = dri->dri_Pens[TEXTPEN];
            pens[1] = dri->dri_Pens[BACKGROUNDPEN];
            pens[2] = dri->dri_Pens[FILLPEN];
            pens[3] = dri->dri_Pens[FILLTEXTPEN];
            for (i = 0; i < 4; i++) {
                G.pens[i] = pens[i];
                if (pens[i] > maxpen)
                    maxpen = pens[i];
            }
            FreeScreenDrawInfo(G.scr, dri);
        }
        G.lvhook.h_Entry = (ULONG (*)())list_draw;
        G.g[G_LIST] = gad = CreateGadget(LISTVIEW_KIND, gad, &ng, GTLV_Labels, (ULONG)&G.list,
                                         GTLV_ShowSelected, 0, GTLV_Selected, ~0UL,
                                         GTLV_Top, G.top, GTLV_CallBack, (ULONG)&G.lvhook,
                                         GTLV_MaxPen, maxpen, TAG_END);
    }
    ng.ng_TextAttr = &G.ta;

    /* the buttons */
    y = bottom - 2 * h - GAP;
    bw = (right - left - 3 * GAP) / 4;
    ng.ng_TopEdge = y;
    ng.ng_Height = h;
    ng.ng_Width = bw;
    ng.ng_Flags = PLACETEXT_IN;
    for (i = 0; i < 4; i++) {
        ng.ng_LeftEdge = left + i * (bw + GAP);
        ng.ng_GadgetText = (UBYTE *)btn[i];
        ng.ng_GadgetID = G_UPDATE + i;
        G.g[G_UPDATE + i] = gad = CreateGadget(BUTTON_KIND, gad, &ng, GA_Disabled,
                                               i == 3 ? !G.busy : G.busy, TAG_END);
    }

    /* the status line */
    ng.ng_LeftEdge = left;
    ng.ng_TopEdge = bottom - h;
    ng.ng_Width = right - left;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = G_STATUS;
    G.g[G_STATUS] = gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)G.status,
                                       GTTX_CopyText, TRUE, GTTX_Border, TRUE, TAG_END);
    return gad != NULL;
}

static void relayout(void)
{
    struct Window *w = G.win;
    if (G.glist) {
        if (G.g[G_DIRS])
            strcpy(G.dirs, (char *)((struct StringInfo *)G.g[G_DIRS]->SpecialInfo)->Buffer);
        if (G.g[G_FIND])
            strcpy(G.find, (char *)((struct StringInfo *)G.g[G_FIND]->SpecialInfo)->Buffer);
        detach_list();
        RemoveGList(w, G.glist, -1);
        FreeGadgets(G.glist);
        G.glist = NULL;
    }
    SetAPen(w->RPort, 0);
    RectFill(w->RPort, w->BorderLeft, w->BorderTop, w->Width - w->BorderRight - 1,
             w->Height - w->BorderBottom - 1);
    if (!make_gadgets())
        return;
    AddGList(w, G.glist, ~0, -1, NULL);
    RefreshGList(G.glist, w, NULL, -1);
    GT_RefreshWindow(w, NULL);
    RefreshWindowFrame(w);
    rebuild_list();
}

static void set_busy(int on)
{
    int i;
    G.busy = on;
    G.stop = 0;
    for (i = 0; i < G_COUNT; i++) {
        if (!G.g[i] || i == G_LIST || i == G_SUMMARY || i == G_STATUS)
            continue;
        GT_SetGadgetAttrs(G.g[i], G.win, NULL, GA_Disabled, i == G_STOP ? !on : on, TAG_END);
    }
    if (IntuitionBase->LibNode.lib_Version >= 39) {
        if (on)
            SetWindowPointer(G.win, WA_BusyPointer, TRUE, WA_PointerDelay, TRUE, TAG_END);
        else
            SetWindowPointer(G.win, TAG_END);
    }
}

/* ---- called by the engine while it works -------------------------------- */

void gui_status(const char *text)
{
    strncpy(G.status, text, sizeof(G.status) - 1);
    G.status[sizeof(G.status) - 1] = 0;
    if (G.win && G.g[G_STATUS])
        GT_SetGadgetAttrs(G.g[G_STATUS], G.win, NULL, GTTX_Text, (ULONG)G.status, TAG_END);
}

int gui_poll(void)
{
    struct IntuiMessage *msg;
    if (!G.win)
        return 0;
    while ((msg = GT_GetIMsg(G.win->UserPort)) != NULL) {
        ULONG cl = msg->Class;
        struct Gadget *gad = (struct Gadget *)msg->IAddress;
        GT_ReplyIMsg(msg);
        switch (cl) {
        case IDCMP_GADGETUP:
            if (gad->GadgetID == G_STOP)
                G.stop = 1;
            break;
        case IDCMP_CLOSEWINDOW:
            G.stop = G.quit = 1;
            break;
        case IDCMP_NEWSIZE:
            relayout();
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(G.win);
            GT_EndRefresh(G.win, TRUE);
            break;
        }
    }
    return G.stop;
}

/* ---- the jobs ---------------------------------------------------------- */

static int cmp_games(const void *a, const void *b)
{
    const unsigned char *x = (const unsigned char *)(*(Game *const *)a)->name;
    const unsigned char *y = (const unsigned char *)(*(Game *const *)b)->name;
    for (;; x++, y++) {
        int cx = *x >= 'A' && *x <= 'Z' ? *x | 32 : *x, cy = *y >= 'A' && *y <= 'Z' ? *y | 32 : *y;
        if (cx != cy || !cx)
            return cx - cy;
    }
}

static void do_check(void)
{
    Engine *e = G.e;
    int stopped = 0;

    strcpy(G.dirs, (char *)((struct StringInfo *)G.g[G_DIRS]->SpecialInfo)->Buffer);
    if (!G.dirs[0] && !e->prefs.dirs[FAM_DEMOS][0] && !e->prefs.dirs[FAM_MAGS][0]) {
        gui_status("Type the drawers your games are in (Games: or Games:;Games2:), then Check");
        return;
    }
    save_dirs();
    set_busy(1);
    detach_list();
    new_list(&G.list);
    G.sel = NULL;                 /* the games are about to be freed */
    G.top = 0;
    G.anchor = -1;            /* another list: rows mean other games */
    engine_clear(e);
    attach_list();
    set_summary();
    if (engine_dat(e, 0) == 0) {
        stopped = engine_scan_all(e, 3) == -2;
        if (!stopped)
            engine_missing(e);
        qsort(e->games, e->n, sizeof(Game *), cmp_games);
        if (stopped)
            say("Stopped: %ld games looked at", e->n);
        else if (e->st[M_UPDATE])
            say("Click the games to update (* = marked), then Update marked");
        else
            say("Everything is up to date (pack list of %s)", e->datdate);
    }
    set_busy(0);
    rebuild_list();
    set_summary();
}

static void do_update(void)
{
    Engine *e = G.e;
    struct EasyStruct es;
    char body[200];
    long i, n = 0, k = 0, done = 0, failed = 0;
    BPTR log;

    for (i = 0; i < e->n; i++)
        n += e->games[i]->marked;
    if (!n) {
        gui_status("Mark the games to update first: click them in the list");
        return;
    }
    snprintf(body, sizeof(body), "Update %ld game%s?\nThe old copies go to WHDUpdate-Old\n"
             "on the same volume.", n, n == 1 ? "" : "s");
    memset(&es, 0, sizeof(es));
    es.es_StructSize = sizeof(es);
    es.es_Title = (UBYTE *)"WHDUpdate";
    es.es_TextFormat = (UBYTE *)body;
    es.es_GadgetFormat = (UBYTE *)"Update|Cancel";
    if (!EasyRequestArgs(G.win, &es, NULL, NULL))
        return;
    set_busy(1);
    log = engine_open_log();
    for (i = 0; i < e->n && !G.stop; i++) {
        Game *g = e->games[i];
        if (!g->marked)
            continue;
        if (k++)
            Delay(100);               /* go easy on the server */
        say("(%ld/%ld) %s %s > %s", k, n, g->name, g->installed, g->pack->ver);
        switch (engine_update(e, g, log)) {
        case 0: done++; break;
        case -2: break;
        default: failed++; g->marked = 0;
        }
        rebuild_list();
        set_summary();
    }
    if (log)
        Close(log);
    set_busy(0);
    rebuild_list();
    set_summary();
    say("%ld updated%s%ld failed. Old copies: WHDUpdate-Old; log: %s", done,
        G.stop ? " (stopped), " : ", ", failed, LOGFILE);
}

/* Remove the games marked under Not wanted: deleted, or moved to
 * WHDUpdate-Old (a Rename, so it is quick, but frees no room until that
 * drawer is emptied). */
static void do_remove(void)
{
    Engine *e = G.e;
    struct EasyStruct es;
    char body[300];
    long i, n = 0, k = 0, done = 0, failed = 0;
    LONG how;
    BPTR log;

    for (i = 0; i < e->n; i++)
        n += e->games[i]->doomed;
    if (!n) {
        gui_status("Show Not wanted, mark the games to remove there, then Remove marked");
        return;
    }
    snprintf(body, sizeof(body), "Remove %ld game%s?\n\nDelete: the drawers and their icons are\n"
             "deleted for good.\nMove: they go to WHDUpdate-Old on the same\n"
             "volume, to delete when you are sure.", n, n == 1 ? "" : "s");
    memset(&es, 0, sizeof(es));
    es.es_StructSize = sizeof(es);
    es.es_Title = (UBYTE *)"WHDUpdate";
    es.es_TextFormat = (UBYTE *)body;
    es.es_GadgetFormat = (UBYTE *)"Delete|Move|Cancel";
    if (!(how = EasyRequestArgs(G.win, &es, NULL, NULL)))
        return;
    set_busy(1);
    detach_list();
    new_list(&G.list);            /* games leave the list as they go */
    G.sel = NULL;
    log = engine_open_log();
    for (i = 0; i < e->n && !gui_poll(); ) {
        Game *g = e->games[i];
        if (!g->doomed) {
            i++;
            continue;
        }
        say("(%ld/%ld) %s %s", ++k, n, how == 1 ? "Deleting" : "Moving", g->name);
        if (engine_remove(e, i, how == 2, log) == 0)
            done++;                   /* the next game is at i now */
        else {
            g->doomed = 0;
            failed++;
            i++;
        }
    }
    if (log)
        Close(log);
    set_busy(0);
    rebuild_list();
    set_summary();
    say("%ld %s%s%ld failed. Log: %s", done, how == 1 ? "deleted" : "moved to WHDUpdate-Old",
        G.stop ? " (stopped), " : ", ", failed, LOGFILE);
}

/* Mark all / Mark none: only the games the list shows (Find and Show), so a
 * search picks the ones to update (or, under Not wanted, to remove); games
 * marked earlier stay marked. */
static void mark_all(int on)
{
    long i;
    for (i = 0; i < G.e->n; i++) {
        Game *g = G.e->games[i];
        if (!shown(g))
            continue;
        if (G.filter == SHOW_UNWANTED)
            g->doomed = on;
        else
            g->marked = on && g->status == M_UPDATE;
    }
    rebuild_list();
    marks_changed(NULL);
}

/* The Find field changed (typed into, checked ten times a second): show
 * only the games whose names contain it. */
static void find_changed(void)
{
    const char *now;
    if (!G.g[G_FIND])
        return;
    now = (const char *)((struct StringInfo *)G.g[G_FIND]->SpecialInfo)->Buffer;
    if (!strcmp(now, G.find))
        return;
    strncpy(G.find, now, sizeof(G.find) - 1);
    detach_list();
    G.top = 0;
    G.anchor = -1;            /* another list: rows mean other games */
    rebuild_list();
}

/* ---- the window ---------------------------------------------------------- */

static int choose_fonts(void)
{
    struct TextAttr *sa = G.scr->Font;
    struct TextFont *def = GfxBase->DefaultFont;
    int need_w, need_h, bt, bb;

    G.ta = *sa;
    G.font = OpenFont(&G.ta);
    if (!G.font) {
        G.ta = topaz8;
        G.font = OpenFont(&G.ta);
    }
    /* the list: the system's fixed-width default font */
    G.lvta.ta_Name = (STRPTR)def->tf_Message.mn_Node.ln_Name;
    G.lvta.ta_YSize = def->tf_YSize;
    G.lvta.ta_Style = def->tf_Style;
    G.lvta.ta_Flags = def->tf_Flags;
    G.lvfont = OpenFont(&G.lvta);
    if (!G.font || !G.lvfont)
        return 0;
    InitRastPort(&G.mrp);
    SetFont(&G.mrp, G.font);

    /* would the smallest layout fit the screen? if not, Topaz 8 for all */
    bt = G.scr->WBorTop + G.scr->Font->ta_YSize + 1;
    bb = 11;
    need_w = min_width() + G.scr->WBorLeft + G.scr->WBorRight + 18;
    need_h = min_height() + bt + bb;
    if (need_w > G.scr->Width || need_h > G.scr->Height - G.scr->BarHeight - 1) {
        CloseFont(G.font);
        CloseFont(G.lvfont);
        G.ta = G.lvta = topaz8;
        G.font = OpenFont(&G.ta);
        G.lvfont = OpenFont(&G.lvta);
        if (!G.font || !G.lvfont)
            return 0;
        SetFont(&G.mrp, G.font);
    }
    return 1;
}

/* The tick of "Use FTP only" follows the setting. */
static void sync_ftp_tick(void)
{
    struct MenuItem *it = ItemAddress(G.menu, FULLMENUNUM(1, 5, NOSUB));
    if (!it)
        return;
    ClearMenuStrip(G.win);
    if (G.e->prefs.ftp_only)
        it->Flags |= CHECKED;
    else
        it->Flags &= ~CHECKED;
    ResetMenuStrip(G.win, G.menu);
}

static void about(void)
{
    struct EasyStruct es;
    memset(&es, 0, sizeof(es));
    es.es_StructSize = sizeof(es);
    es.es_Title = (UBYTE *)"About WHDUpdate";
    es.es_TextFormat = (UBYTE *)"WHDUpdate " WHDU_VERSION "\n"
        "Keeps your WHDLoad games up to date.\n\n"
        "The packs are Retroplay's, on %s.\n"
        "Free software under the GPL-3.0.";
    es.es_GadgetFormat = (UBYTE *)"OK";
    EasyRequest(G.win, &es, NULL, (ULONG)G.e->prefs.host);
}

/* A settings page; after Save (ENV: + ENVARC:) or Use (ENV: only) the list
 * is checked again with the new settings when there are results to redo. */
static void settings(int which)
{
    PrefsUI ui;
    int ok = 0;
    ui.scr = G.scr;
    ui.vi = G.vi;
    ui.ta = &G.ta;
    ui.parent = G.win;
    if (IntuitionBase->LibNode.lib_Version >= 39)
        SetWindowPointer(G.win, WA_BusyPointer, TRUE, TAG_END);
    switch (which) {
    case MN_SERVER: ok = prefs_server(&ui, &G.e->prefs); break;
    case MN_SETS:   ok = prefs_sets(&ui, &G.e->prefs); break;
    case MN_LANG:   ok = prefs_variants(&ui, &G.e->prefs, 0); break;
    case MN_SYS:    ok = prefs_variants(&ui, &G.e->prefs, 1); break;
    }
    if (IntuitionBase->LibNode.lib_Version >= 39)
        SetWindowPointer(G.win, TAG_END);
    if (!ok)
        return;
    if (ok == 2)
        prefs_save(&G.e->prefs);      /* Save: kept */
    else
        prefs_use(&G.e->prefs);       /* Use: until the next reboot */
    sync_ftp_tick();
    if (G.e->n || which == MN_SETS)
        do_check();                   /* the list with the new settings */
    else
        gui_status(ok == 2 ? "Settings saved" : "Settings in use until the next reboot");
}

static void do_menu(ULONG id, struct MenuItem *it)
{
    switch (id) {
    case MN_CHECK:  do_check(); break;
    case MN_UPDATE: do_update(); break;
    case MN_REMOVE: do_remove(); break;
    case MN_ABOUT:  about(); break;
    case MN_QUIT:   G.quit = 1; break;
    case MN_FTPONLY:
        G.e->prefs.ftp_only = (it->Flags & CHECKED) != 0;
        prefs_save(&G.e->prefs);
        gui_status(G.e->prefs.ftp_only ? "Pack lists and packs come by FTP" :
                   "Pack lists and packs come by HTTP, FTP if that fails");
        break;
    default:
        settings((int)id);
    }
}

int gui_main(struct Engine *e, const char *dirs)
{
    int width, height, minw, minh, maxh;
    char title[40];

    memset(&G, 0, sizeof(G));
    G.e = e;
    new_list(&G.list);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 37);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 37);
    if (!IntuitionBase || !GfxBase || !GadToolsBase)
        goto done;
    if (dirs)
        strncpy(e->prefs.dirs[FAM_GAMES], dirs, sizeof(e->prefs.dirs[0]) - 1);
    load_dirs();
    if (!(G.scr = LockPubScreen(NULL)) || !(G.vi = GetVisualInfo(G.scr, TAG_END)) || !choose_fonts())
        goto done;

    minw = min_width() + G.scr->WBorLeft + G.scr->WBorRight + 18;
    minh = min_height() + G.scr->WBorTop + G.scr->Font->ta_YSize + 1 + 11;
    maxh = G.scr->Height - G.scr->BarHeight - 1;
    width = 2 * M + 72 * G.lvfont->tf_XSize + 24 + G.scr->WBorLeft + G.scr->WBorRight + 18;
    if (width > G.scr->Width)
        width = G.scr->Width;
    if (width < minw)
        width = minw;
    height = minh + 14 * G.lvfont->tf_YSize;      /* room for ~18 games */
    if (height > maxh)
        height = maxh;
    if (minh > maxh)
        minh = maxh;
    snprintf(title, sizeof(title), "WHDUpdate " WHDU_VERSION);
    G.win = OpenWindowTags(NULL,
        WA_Left, (G.scr->Width - width) / 2, WA_Top, G.scr->BarHeight + 1,
        WA_Width, width, WA_Height, height,
        WA_MinWidth, minw, WA_MinHeight, minh, WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL,
        WA_Title, (ULONG)title, WA_PubScreen, (ULONG)G.scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_SizeBBottom, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_NewLookMenus, TRUE,
        WA_IDCMP, BUTTONIDCMP | LISTVIEWIDCMP | STRINGIDCMP | CYCLEIDCMP | IDCMP_CLOSEWINDOW |
                  IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_INTUITICKS | IDCMP_MENUPICK,
        TAG_END);
    if (!G.win)
        goto done;
    if ((G.menu = CreateMenus(menu_def, TAG_END)) != NULL) {
        if (LayoutMenus(G.menu, G.vi, GTMN_NewLookMenus, TRUE, TAG_END))
            SetMenuStrip(G.win, G.menu);
        sync_ftp_tick();
    }
    set_summary();
    gui_status(G.dirs[0] ? "Checking..." :
               "Type the drawers your games are in (Games: or Games:;Games2:), then Check");
    relayout();
    G.anchor = G.press = -1;
    if (G.dirs[0] || e->prefs.dirs[FAM_DEMOS][0] || e->prefs.dirs[FAM_MAGS][0])
        do_check();                   /* the drawers are known: check right away */

    while (!G.quit) {
        struct IntuiMessage *raw, *msg;
        Wait(1UL << G.win->UserPort->mp_SigBit);
        while (!G.quit && (raw = (struct IntuiMessage *)GetMsg(G.win->UserPort)) != NULL) {
            ULONG cl;
            UWORD code, qual;
            struct Gadget *gad;
            /* where the left button went down, for a drag over the list: the
             * list itself only tells where it came up, and GadTools keeps its
             * own press and moves to itself - so seen before its filter */
            if (raw->Class == IDCMP_GADGETDOWN) {
                G.press = row_at(raw->MouseX, raw->MouseY) >= 0 ? row_at(raw->MouseX, raw->MouseY) : -2;
                G.dragrow = -1;
                drag_preview(G.press);            /* the row pressed shows at once */
            } else if (raw->Class == IDCMP_INTUITICKS && !(raw->Qualifier & IEQUALIFIER_LEFTBUTTON) &&
                       G.painted) {
                paint_rows(0);                    /* let go outside the list: as it was */
            } else if (raw->Class == IDCMP_MOUSEMOVE && (raw->Qualifier & IEQUALIFIER_LEFTBUTTON) &&
                       G.press == -1) {
                G.press = row_at(raw->MouseX, raw->MouseY) >= 0 ? row_at(raw->MouseX, raw->MouseY) : -2;
                G.dragrow = G.press;
            } else if ((raw->Class == IDCMP_MOUSEMOVE || raw->Class == IDCMP_INTUITICKS) &&
                       (raw->Qualifier & IEQUALIFIER_LEFTBUTTON) && G.press >= 0)
                drag_preview(row_at(raw->MouseX, raw->MouseY));   /* what letting go will do */
            else if (raw->Class == IDCMP_GADGETUP && (!raw->IAddress ||
                     ((struct Gadget *)raw->IAddress)->GadgetID != G_LIST))
                G.press = -1;
            if (!(msg = GT_FilterIMsg(raw))) {      /* one GadTools handled itself */
                ReplyMsg(&raw->ExecMessage);
                continue;
            }
            cl = msg->Class;
            code = msg->Code;
            qual = msg->Qualifier;
            gad = (struct Gadget *)msg->IAddress;
            ReplyMsg(&GT_PostFilterIMsg(msg)->ExecMessage);
            switch (cl) {
            case IDCMP_CLOSEWINDOW:
                G.quit = 1;
                break;
            case IDCMP_MENUPICK:
                while (code != MENUNULL && !G.quit) {
                    struct MenuItem *it = ItemAddress(G.menu, code);
                    if (!it)
                        break;
                    do_menu((ULONG)GTMENUITEM_USERDATA(it), it);
                    code = it->NextSelect;
                }
                break;
            case IDCMP_INTUITICKS:            /* the list follows the Find field as you type */
                if (!G.busy && !(qual & IEQUALIFIER_LEFTBUTTON))
                    find_changed();
                break;
            case IDCMP_NEWSIZE:
                relayout();
                break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(G.win);
                GT_EndRefresh(G.win, TRUE);
                break;
            case IDCMP_GADGETUP:
                switch (gad->GadgetID) {
                case G_FIND:
                    find_changed();
                    break;
                case G_DIRS:              /* Return in the string: check */
                case G_CHECK:
                    do_check();
                    break;
                case G_SET:
                    G.set = code;
                    detach_list();
                    G.top = 0;
                    G.anchor = -1;            /* another list: rows mean other games */
                    rebuild_list();
                    set_summary();
                    if (G.set && !((G.e->prefs.sets >> (G.set - 1)) & 1)) {
                        char msg[120];
                        snprintf(msg, sizeof(msg), "%s is off: turn it on in Settings > Pack sets, then Check",
                                 set_labels[G.set]);
                        gui_status(msg);
                    }
                    break;
                case G_SHOW: {
                    int was = G.filter == SHOW_UNWANTED;
                    G.filter = code;
                    detach_list();
                    G.top = 0;                /* another list: from its top */
                    G.anchor = -1;
                    if (was != (G.filter == SHOW_UNWANTED))
                        relayout();           /* Update marked <-> Remove marked */
                    else
                        rebuild_list();
                    set_summary();
                    if (G.filter == SHOW_UNWANTED)
                        gui_status("Games of a language or system turned off in Settings: mark, then Remove");
                    break;
                }
                case G_LIST: {
                    Game *g = nth_shown(code);
                    long from = -1;
                    G.sel = g;                /* stays highlighted, marked or not */
                    /* Shift-click: from the row clicked before, as that one is;
                     * a drag: from the row it started on, the opposite of how
                     * that one was */
                    if ((qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) && G.anchor >= 0 &&
                        nth_shown(G.anchor))
                        from = G.anchor;
                    else if (G.press >= 0 && G.press != (long)code)
                        from = G.press;
                    if (g && from >= 0) {
                        Game *f = nth_shown(from);
                        int on = from == G.anchor && (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ?
                                 (markable(f) ? is_marked(f) : 1) : !is_marked(f);
                        char txt[80];
                        long n = mark_rows(from, code, on);
                        G.anchor = from;
                        G.press = -1;
                        snprintf(txt, sizeof(txt), "%ld game%s %s", n, n == 1 ? "" : "s",
                                 on ? "marked" : "unmarked");
                        relabel_rows(from, code);
                        marks_changed(txt);
                        break;
                    }
                    G.anchor = code;
                    G.press = -1;
                    if (g && markable(g)) {
                        if (G.filter == SHOW_UNWANTED)
                            g->doomed = !g->doomed;
                        else
                            g->marked = !g->marked;
                        relabel_rows(code, code);
                        marks_changed(NULL);
                    } else if (g) {
                        gui_status(g->status == M_CURRENT ? "That one is up to date" :
                                   g->status == M_MISSING ? "Not on your drives: only in the pack list" :
                                   "No pack to update that one from");
                        relabel_rows(code, code);
                    }
                    break;
                }
                case G_UPDATE:
                    if (G.filter == SHOW_UNWANTED)
                        do_remove();
                    else
                        do_update();
                    break;
                case G_MARKALL:
                    mark_all(1);
                    break;
                case G_MARKNONE:
                    mark_all(0);
                    break;
                }
                break;
            }
        }
    }

done:
    /* the Drawers field is kept even when Check was never pressed */
    if (G.win && G.g[G_DIRS]) {
        strcpy(G.dirs, (char *)((struct StringInfo *)G.g[G_DIRS]->SpecialInfo)->Buffer);
        if (strcmp(G.dirs, G.e->prefs.dirs[FAM_GAMES]))
            save_dirs();
    }
    if (G.win) {
        detach_list();
        if (G.menu)
            ClearMenuStrip(G.win);
        CloseWindow(G.win);
    }
    if (G.menu)
        FreeMenus(G.menu);
    if (G.glist)
        FreeGadgets(G.glist);
    if (G.font)
        CloseFont(G.font);
    if (G.lvfont)
        CloseFont(G.lvfont);
    if (G.vi)
        FreeVisualInfo(G.vi);
    if (G.scr)
        UnlockPubScreen(NULL, G.scr);
    if (GadToolsBase)
        CloseLibrary(GadToolsBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    return RETURN_OK;
}
