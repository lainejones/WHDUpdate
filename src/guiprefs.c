/* guiprefs.c - the Settings pages of the window: Server, Pack sets,
 * Languages and Systems. Each is a small window of its own over the main
 * one, with Use and Cancel; laid out from the font like the main window, so
 * they fit a 640x200 screen with Topaz 8 too. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <string.h>
#include "match.h"
#include "engine.h"
#include "guiprefs.h"

#define M 8
#define GAP 4
#define MAXG 48

enum { ID_USE = 1, ID_CANCEL, ID_DEFAULTS, ID_PRESET, ID_FIRST = 10 };

typedef struct {
    struct Screen *scr;
    APTR vi;
    struct TextAttr *ta;
    struct TextFont *font;
    struct RastPort rp;
    struct Window *win;
    struct Gadget *glist, *g[MAXG];
    int n;
} Page;

static int tlen(Page *pg, const char *s)
{
    return TextLength(&pg->rp, (STRPTR)s, strlen(s));
}

static int bh(Page *pg) { return pg->font->tf_YSize + 6; }

static int page_open(Page *pg, const PrefsUI *ui, const char *title, int w, int h)
{
    memset(pg, 0, sizeof(*pg));
    pg->scr = ui->scr;
    pg->vi = ui->vi;
    pg->ta = ui->ta;
    pg->font = OpenFont(ui->ta);
    if (!pg->font)
        return 0;
    InitRastPort(&pg->rp);
    SetFont(&pg->rp, pg->font);
    if (w > pg->scr->Width)
        w = pg->scr->Width;
    if (h > pg->scr->Height - pg->scr->BarHeight - 1)
        h = pg->scr->Height - pg->scr->BarHeight - 1;
    pg->win = OpenWindowTags(NULL,
        WA_Left, ui->parent->LeftEdge + 20 < pg->scr->Width - w ? ui->parent->LeftEdge + 20 :
                 pg->scr->Width - w,
        WA_Top, pg->scr->BarHeight + 1,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_Title, (ULONG)title, WA_PubScreen, (ULONG)pg->scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, BUTTONIDCMP | CHECKBOXIDCMP | STRINGIDCMP | CYCLEIDCMP | IDCMP_CLOSEWINDOW |
                  IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY,
        TAG_END);
    if (!pg->win) {
        CloseFont(pg->font);
        return 0;
    }
    return 1;
}

static void page_show(Page *pg)
{
    AddGList(pg->win, pg->glist, ~0, -1, NULL);
    RefreshGList(pg->glist, pg->win, NULL, -1);
    GT_RefreshWindow(pg->win, NULL);
}

static void page_close(Page *pg)
{
    if (pg->win)
        CloseWindow(pg->win);
    if (pg->glist)
        FreeGadgets(pg->glist);
    if (pg->font)
        CloseFont(pg->font);
}

/* Use / Cancel (and Defaults) along the bottom, at y. */
static struct Gadget *buttons(Page *pg, struct Gadget *gad, int y, int width, int defaults)
{
    struct NewGadget ng;
    int bw = tlen(pg, "Defaults") + 24, x = pg->win->BorderLeft + M;
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = pg->vi;
    ng.ng_TextAttr = pg->ta;
    ng.ng_TopEdge = pg->win->BorderTop + y;
    ng.ng_Width = bw;
    ng.ng_Height = bh(pg);
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_LeftEdge = x;
    ng.ng_GadgetText = (UBYTE *)"Use";
    ng.ng_GadgetID = ID_USE;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    if (defaults) {
        ng.ng_LeftEdge = x + (width - 2 * M - bw) / 2;
        ng.ng_GadgetText = (UBYTE *)"Defaults";
        ng.ng_GadgetID = ID_DEFAULTS;
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    }
    ng.ng_LeftEdge = x + width - 2 * M - bw;
    ng.ng_GadgetText = (UBYTE *)"Cancel";
    ng.ng_GadgetID = ID_CANCEL;
    return CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
}

/* Wait for Use (1), Cancel / close (0). on_gadget handles the others. */
static int page_run(Page *pg, void (*on_gadget)(Page *, int id, UWORD code, void *ctx), void *ctx)
{
    for (;;) {
        struct IntuiMessage *msg;
        WaitPort(pg->win->UserPort);
        while ((msg = GT_GetIMsg(pg->win->UserPort)) != NULL) {
            ULONG cl = msg->Class;
            UWORD code = msg->Code;
            struct Gadget *gad = (struct Gadget *)msg->IAddress;
            GT_ReplyIMsg(msg);
            switch (cl) {
            case IDCMP_CLOSEWINDOW:
                return 0;
            case IDCMP_VANILLAKEY:
                if (code == 27)
                    return 0;
                break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(pg->win);
                GT_EndRefresh(pg->win, TRUE);
                break;
            case IDCMP_GADGETUP:
                if (gad->GadgetID == ID_USE)
                    return 1;
                if (gad->GadgetID == ID_CANCEL)
                    return 0;
                if (on_gadget)
                    on_gadget(pg, gad->GadgetID, code, ctx);
                break;
            }
        }
    }
}

static int checked(Page *pg, struct Gadget *g)
{
    (void)pg;
    return (g->Flags & GFLG_SELECTED) != 0;
}

static const char *str_of(struct Gadget *g)
{
    return (const char *)((struct StringInfo *)g->SpecialInfo)->Buffer;
}

/* ---- Languages / Systems: tick boxes ------------------------------------- */

typedef struct {
    const char *label[32];
    unsigned long bits[32];
    int n;
} Ticks;

static void ticks_of(Ticks *t, int system)
{
    int i, j;
    t->n = 0;
    for (i = 0; variant_words[i].word; i++) {
        if (variant_words[i].system != system)
            continue;
        for (j = 0; j < t->n && strcmp(t->label[j], variant_words[i].label); j++)
            ;
        if (j == t->n) {
            t->label[t->n] = variant_words[i].label;
            t->bits[t->n++] = 0;
        }
        t->bits[j] |= 1UL << i;           /* Swedish: "se" and "sv" */
    }
}

static void ticks_defaults(Page *pg, int id, UWORD code, void *ctx)
{
    Ticks *t = (Ticks *)ctx;
    unsigned long def = variants_default();
    int i;
    (void)code;
    if (id != ID_DEFAULTS)
        return;
    for (i = 0; i < t->n; i++)
        GT_SetGadgetAttrs(pg->g[i], pg->win, NULL, GTCB_Checked, (def & t->bits[i]) != 0, TAG_END);
}

int prefs_variants(const PrefsUI *ui, Prefs *p, int system)
{
    Page pg;
    Ticks t;
    struct NewGadget ng;
    struct Gadget *gad;
    int i, cols, rows, colw = 0, rowh, w, h, ok = 0;
    const char *title = system ? "WHDUpdate: Systems" : "WHDUpdate: Languages";

    ticks_of(&t, system);
    {
        struct TextFont *f = OpenFont(ui->ta);
        struct RastPort rp;
        if (!f)
            return 0;
        InitRastPort(&rp);
        SetFont(&rp, f);
        for (i = 0; i < t.n; i++) {
            int l = TextLength(&rp, (STRPTR)t.label[i], strlen(t.label[i]));
            if (l > colw)
                colw = l;
        }
        rowh = f->tf_YSize + 5 > 13 ? f->tf_YSize + 5 : 13;
        CloseFont(f);
    }
    colw += 26 + 8 + 16;
    cols = system ? 2 : 3;
    while (cols > 1 && 2 * M + cols * colw > ui->scr->Width - 20)
        cols--;
    rows = (t.n + cols - 1) / cols;
    w = 2 * M + cols * colw;
    if (w < 300)
        w = 300;
    h = 4 + rows * rowh + GAP + 6 + (ui->ta->ta_YSize + 6) + 4;
    if (!page_open(&pg, ui, title, w, h))
        return 0;
    gad = CreateContext(&pg.glist);
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = pg.vi;
    ng.ng_TextAttr = pg.ta;
    ng.ng_Width = 26;
    ng.ng_Height = 11;
    ng.ng_Flags = PLACETEXT_RIGHT;
    for (i = 0; i < t.n; i++) {
        ng.ng_LeftEdge = pg.win->BorderLeft + M + (i / rows) * colw;
        ng.ng_TopEdge = pg.win->BorderTop + 4 + (i % rows) * rowh;
        ng.ng_GadgetText = (UBYTE *)t.label[i];
        ng.ng_GadgetID = ID_FIRST + i;
        pg.g[i] = gad = CreateGadget(CHECKBOX_KIND, gad, &ng,
                                     GTCB_Checked, (p->allowed & t.bits[i]) != 0, TAG_END);
    }
    gad = buttons(&pg, gad, 4 + rows * rowh + GAP + 6, w, 1);
    if (gad) {
        page_show(&pg);
        if (page_run(&pg, ticks_defaults, &t)) {
            for (i = 0; i < t.n; i++) {
                if (checked(&pg, pg.g[i]))
                    p->allowed |= t.bits[i];
                else
                    p->allowed &= ~t.bits[i];
            }
            ok = 1;
        }
    }
    page_close(&pg);
    return ok;
}

/* ---- Pack sets ----------------------------------------------------------- */

int prefs_sets(const PrefsUI *ui, Prefs *p)
{
    Page pg;
    struct NewGadget ng;
    struct Gadget *gad;
    int i, labw = 0, rowh, w, h, ok = 0, lw, any;
    struct TextFont *f = OpenFont(ui->ta);
    struct RastPort rp;

    if (!f)
        return 0;
    InitRastPort(&rp);
    SetFont(&rp, f);
    for (i = 0; i < NSETS; i++) {
        int l = TextLength(&rp, (STRPTR)pack_sets[i].label, strlen(pack_sets[i].label));
        if (l > labw)
            labw = l;
    }
    lw = TextLength(&rp, (STRPTR)"Drawers", 7) + 8;
    rowh = f->tf_YSize + 6 + 3;
    CloseFont(f);
    w = 2 * M + 26 + 8 + labw + 16 + lw + 200;
    if (w > ui->scr->Width - 20)
        w = ui->scr->Width - 20;
    h = 4 + NSETS * rowh + GAP + 6 + (ui->ta->ta_YSize + 6) + 4;
    if (!page_open(&pg, ui, "WHDUpdate: Pack sets (Games use the main window's drawers)", w, h))
        return 0;
    gad = CreateContext(&pg.glist);
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = pg.vi;
    ng.ng_TextAttr = pg.ta;
    for (i = 0; i < NSETS; i++) {
        int y = pg.win->BorderTop + 4 + i * rowh;
        ng.ng_LeftEdge = pg.win->BorderLeft + M;
        ng.ng_TopEdge = y + (bh(&pg) - 11) / 2;
        ng.ng_Width = 26;
        ng.ng_Height = 11;
        ng.ng_Flags = PLACETEXT_RIGHT;
        ng.ng_GadgetText = (UBYTE *)pack_sets[i].label;
        ng.ng_GadgetID = ID_FIRST + i;
        pg.g[i] = gad = CreateGadget(CHECKBOX_KIND, gad, &ng,
                                     GTCB_Checked, (p->sets >> i) & 1, TAG_END);
        /* the families with their own drawers: on the main set's row */
        if (i == SET_DEMOS || i == SET_MAGS) {
            int fam = pack_sets[i].family;
            ng.ng_LeftEdge = pg.win->BorderLeft + M + 26 + 8 + labw + 16 + lw;
            ng.ng_TopEdge = y;
            ng.ng_Width = w - (ng.ng_LeftEdge - pg.win->BorderLeft) - M;
            ng.ng_Height = bh(&pg);
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_GadgetText = (UBYTE *)"Drawers";
            ng.ng_GadgetID = ID_FIRST + NSETS + fam;
            pg.g[NSETS + fam] = gad = CreateGadget(STRING_KIND, gad, &ng,
                                                   GTST_String, (ULONG)p->dirs[fam],
                                                   GTST_MaxChars, sizeof(p->dirs[0]) - 1, TAG_END);
        }
    }
    gad = buttons(&pg, gad, 4 + NSETS * rowh + GAP + 6, w, 0);
    if (gad) {
        page_show(&pg);
        if (page_run(&pg, NULL, NULL)) {
            unsigned long sets = 0;
            for (i = 0; i < NSETS; i++)
                if (checked(&pg, pg.g[i]))
                    sets |= 1UL << i;
            any = sets != 0;
            if (any)
                p->sets = sets;
            for (i = FAM_DEMOS; i < NFAMS; i++)
                if (pg.g[NSETS + i])
                    strncpy(p->dirs[i], str_of(pg.g[NSETS + i]), sizeof(p->dirs[0]) - 1);
            ok = 1;
        }
    }
    page_close(&pg);
    return ok;
}

/* ---- Server --------------------------------------------------------------- */

static const char *preset_labels[] = {"Turran (ftp2.grandis.nu)", "Turran (ftp.grandis.nu)",
                                      "Other", NULL};
static const char *preset_hosts[] = {"ftp2.grandis.nu", "ftp.grandis.nu"};
enum { S_HOST, S_WEB, S_FTP, S_USER, S_PASS, S_FTPONLY, S_COUNT };

static void server_gadget(Page *pg, int id, UWORD code, void *ctx)
{
    Prefs def;
    (void)ctx;
    if (id != ID_PRESET || code > 1)
        return;
    prefs_default(&def);
    GT_SetGadgetAttrs(pg->g[S_HOST], pg->win, NULL, GTST_String, (ULONG)preset_hosts[code], TAG_END);
    GT_SetGadgetAttrs(pg->g[S_WEB], pg->win, NULL, GTST_String, (ULONG)def.webdir, TAG_END);
    GT_SetGadgetAttrs(pg->g[S_FTP], pg->win, NULL, GTST_String, (ULONG)def.ftpdir, TAG_END);
    GT_SetGadgetAttrs(pg->g[S_USER], pg->win, NULL, GTST_String, (ULONG)def.user, TAG_END);
    GT_SetGadgetAttrs(pg->g[S_PASS], pg->win, NULL, GTST_String, (ULONG)def.pass, TAG_END);
}

int prefs_server(const PrefsUI *ui, Prefs *p)
{
    static const char *labels[] = {"Server", "HTTP folder", "FTP folder", "FTP user", "FTP password"};
    Page pg;
    struct NewGadget ng;
    struct Gadget *gad;
    char *vals[5];
    int i, lw = 0, w, h, rowh, ok = 0, preset = 2, y;
    struct TextFont *f = OpenFont(ui->ta);
    struct RastPort rp;

    if (!f)
        return 0;
    InitRastPort(&rp);
    SetFont(&rp, f);
    for (i = 0; i < 5; i++) {
        int l = TextLength(&rp, (STRPTR)labels[i], strlen(labels[i]));
        if (l > lw)
            lw = l;
    }
    lw += 8;
    rowh = f->tf_YSize + 6 + GAP;
    CloseFont(f);
    for (i = 0; i < 2; i++)
        if (!strcmp(p->host, preset_hosts[i]))
            preset = i;
    vals[0] = p->host;
    vals[1] = p->webdir;
    vals[2] = p->ftpdir;
    vals[3] = p->user;
    vals[4] = p->pass;
    w = 2 * M + lw + 360;
    if (w > ui->scr->Width - 20)
        w = ui->scr->Width - 20;
    h = 4 + 7 * rowh + 6 + (ui->ta->ta_YSize + 6) + 4;
    if (!page_open(&pg, ui, "WHDUpdate: Server", w, h))
        return 0;
    gad = CreateContext(&pg.glist);
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = pg.vi;
    ng.ng_TextAttr = pg.ta;
    ng.ng_LeftEdge = pg.win->BorderLeft + M + lw;
    ng.ng_Width = w - 2 * M - lw;
    ng.ng_Height = bh(&pg);
    ng.ng_Flags = PLACETEXT_LEFT;
    y = pg.win->BorderTop + 4;
    ng.ng_TopEdge = y;
    ng.ng_GadgetText = (UBYTE *)"Preset";
    ng.ng_GadgetID = ID_PRESET;
    gad = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, (ULONG)preset_labels,
                       GTCY_Active, preset, TAG_END);
    for (i = 0; i < 5; i++) {
        ng.ng_TopEdge = y + (i + 1) * rowh;
        ng.ng_GadgetText = (UBYTE *)labels[i];
        ng.ng_GadgetID = ID_FIRST + i;
        pg.g[i] = gad = CreateGadget(STRING_KIND, gad, &ng, GTST_String, (ULONG)vals[i],
                                     GTST_MaxChars, i < 3 ? (i ? 159 : 79) : 39, TAG_END);
    }
    ng.ng_TopEdge = y + 6 * rowh + (bh(&pg) - 11) / 2;
    ng.ng_Width = 26;
    ng.ng_Height = 11;
    ng.ng_GadgetText = (UBYTE *)"Use FTP only";
    ng.ng_GadgetID = ID_FIRST + S_FTPONLY;
    ng.ng_Flags = PLACETEXT_RIGHT;
    pg.g[S_FTPONLY] = gad = CreateGadget(CHECKBOX_KIND, gad, &ng, GTCB_Checked, p->ftp_only, TAG_END);
    gad = buttons(&pg, gad, 4 + 7 * rowh + 6, w, 0);
    if (gad) {
        page_show(&pg);
        if (page_run(&pg, server_gadget, NULL)) {
            strncpy(p->host, str_of(pg.g[S_HOST]), sizeof(p->host) - 1);
            strncpy(p->webdir, str_of(pg.g[S_WEB]), sizeof(p->webdir) - 1);
            strncpy(p->ftpdir, str_of(pg.g[S_FTP]), sizeof(p->ftpdir) - 1);
            strncpy(p->user, str_of(pg.g[S_USER]), sizeof(p->user) - 1);
            strncpy(p->pass, str_of(pg.g[S_PASS]), sizeof(p->pass) - 1);
            p->ftp_only = checked(&pg, pg.g[S_FTPONLY]);
            ok = 1;
        }
    }
    page_close(&pg);
    return ok;
}
