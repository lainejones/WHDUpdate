/* guiprefs.h - the Settings pages of the window (guiprefs.c). */
#ifndef WHDU_GUIPREFS_H
#define WHDU_GUIPREFS_H

#include <intuition/intuition.h>
#include <graphics/text.h>
#include "engine.h"

typedef struct {
    struct Screen *scr;
    APTR vi;
    struct TextAttr *ta;        /* the main window's font */
    struct Window *parent;
} PrefsUI;

/* Each returns 2 for Save, 1 for Use (p changed either way), 0 for Cancel. */
int prefs_server(const PrefsUI *ui, Prefs *p);
int prefs_sets(const PrefsUI *ui, Prefs *p);
int prefs_variants(const PrefsUI *ui, Prefs *p, int system);   /* 0 Languages, 1 Systems */

#endif
