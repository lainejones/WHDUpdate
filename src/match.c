/* match.c - index the packs and match installed games against them. */
#include <stdlib.h>
#include <string.h>
#include "match.h"

static const PackIndex *sort_ix;

void norm_name(const char *in, char *out, int size)
{
    int n = 0;
    for (; *in && n < size - 1; in++) {
        int c = (unsigned char)*in;
        if (c >= 'A' && c <= 'Z')
            out[n++] = (char)(c | 32);
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out[n++] = (char)c;
    }
    out[n] = 0;
}

static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* "v1.2", "v2.06", "v1.3b", "v1.1-B": the length of the version itself
 * (without the "v" and a "-..." tail), or 0 if t is not a version. */
static int is_version_tok(const char *t, int len)
{
    int i = 1, vlen;
    if (len < 2 || (t[0] | 32) != 'v' || !is_digit(t[1]))
        return 0;
    while (i < len && (is_digit(t[i]) || (t[i] == '.' && i + 1 < len && is_digit(t[i + 1]))))
        i++;
    if (i < len && t[i] >= 'a' && t[i] <= 'z')
        i++;
    vlen = i - 1;
    if (i < len && t[i] == '-')
        i = len;
    return i == len ? vlen : 0;
}

const VariantWord variant_words[] = {
    {"en", "English", 0},     {"de", "German", 0},     {"fr", "French", 0},
    {"it", "Italian", 0},     {"es", "Spanish", 0},    {"pl", "Polish", 0},
    {"cz", "Czech", 0},       {"dk", "Danish", 0},     {"fi", "Finnish", 0},
    {"gr", "Greek", 0},       {"nl", "Dutch", 0},      {"no", "Norwegian", 0},
    {"pt", "Portuguese", 0},  {"se", "Swedish", 0},    {"sv", "Swedish", 0},
    {"hu", "Hungarian", 0},   {"tr", "Turkish", 0},    {"ru", "Russian", 0},
    {"multi", "Multi-language", 0},
    {"aga", "AGA", 1},        {"ecs", "ECS", 1},       {"ocs", "OCS", 1},
    {"ntsc", "NTSC", 1},      {"pal", "PAL", 1},       {"cd32", "CD32", 1},
    {"cdtv", "CDTV", 1},      {"cd", "CD-ROM", 1},     {"arcadia", "Arcadia", 1},
    {"demo", "Demo versions", 1}, {"preview", "Demo versions", 1},
    {NULL, NULL, 0}};

/* By default an unlabelled drawer may get an English pack, any chipset and
 * NTSC (for many games the NTSC release is the only pack, and it runs on a
 * PAL machine) - but no other language, no CD32/CDTV/Arcadia and no demo. */
unsigned long variants_default(void)
{
    static const char *const ok[] = {"en", "aga", "ecs", "ocs", "ntsc", "pal", 0};
    unsigned long bits = 0;
    int i, j;
    for (i = 0; variant_words[i].word; i++)
        for (j = 0; ok[j]; j++)
            if (!strcmp(variant_words[i].word, ok[j]))
                bits |= 1UL << i;
    return bits;
}

unsigned long match_allowed = 0xffffffffUL;   /* set from the settings (engine.c) */
unsigned long match_sets = 0xffffffffUL;

#define IN_SETS(p) ((match_sets >> (p)->set) & 1)

static unsigned long variant_bit(const char *t)
{
    int i;
    for (i = 0; variant_words[i].word; i++)
        if (!strcmp(t, variant_words[i].word))
            return 1UL << i;
    return 0;
}

/* "0417", or several joined by '&' ("0107&0266") for a pack of two installs */
static int is_number_tok(const char *t, int len)
{
    int i = 0;
    while (i < len) {
        int d = 0;
        while (i < len && is_digit(t[i]))
            i++, d++;
        if (d < 3 || d > 4)
            return 0;
        if (i < len && t[i++] != '&')
            return 0;
    }
    return len > 0;
}

/* The same letters, in any order: variant words a drawer lists in another
 * order than the pack ("ntsc1mb" / "1mbntsc"). */
static int same_letters(const char *a, const char *b)
{
    int count[256], i;
    if (strlen(a) != strlen(b))
        return 0;
    memset(count, 0, sizeof(count));
    for (i = 0; a[i]; i++)
        count[(unsigned char)a[i]]++;
    for (i = 0; b[i]; i++)
        if (--count[(unsigned char)b[i]] < 0)
            return 0;
    return 1;
}

/* How many characters at the end of k are variant words - "aga", "2mb",
 * "cd32", "datadisk", a language... - as drawer names add them to the game's
 * name (MortalKombat22MB, PinballFntsiesAGAChip). */
static int variant_tail(const char *k)
{
    static const char *const words[] = {
        "aga", "ecs", "ocs", "cd32", "cdtv", "ntsc", "pal", "fixed", "nointro", "files", "image",
        "chip", "fast", "slow", "lowmem", "datadisks", "datadisk", "missiondisks", "missiondisk",
        "arcadia", "de", "fr", "it", "es", "pl", "cz", "dk", "fi", "gr", "nl", "pt", "se", "sv",
        "hu", "tr", "ru", "en", "uk", "us", 0};
    int len = (int)strlen(k), pos = len;
    while (pos > 0) {
        int i, found = 0, d;
        for (i = 0; words[i] && !found; i++) {
            int wl = (int)strlen(words[i]);
            if (pos >= wl && !memcmp(k + pos - wl, words[i], wl)) {
                pos -= wl;
                found = 1;
            }
        }
        if (found)
            continue;
        /* 2mb, 512kb, 512k, 2disk, 3disks */
        {
            static const char *const units[] = {"mb", "kb", "k", "disks", "disk", 0};
            for (i = 0; units[i] && !found; i++) {
                int ul = (int)strlen(units[i]);
                if (pos > ul && !memcmp(k + pos - ul, units[i], ul) && is_digit(k[pos - ul - 1])) {
                    for (d = pos - ul - 1; d > 0 && is_digit(k[d - 1]); d--)
                        ;
                    pos = d;
                    found = 1;
                }
            }
        }
        /* SceneryDisk, MissionDisks: "disk" without a number in front */
        if (!found) {
            static const char *const disk[] = {"disks", "disk", 0};
            for (i = 0; disk[i] && !found; i++) {
                int wl = (int)strlen(disk[i]);
                if (pos >= wl && !memcmp(k + pos - wl, disk[i], wl)) {
                    pos -= wl;
                    found = 1;
                }
            }
        }
        if (!found)
            break;
    }
    return len - pos;
}

/* Is s made only of variant words? ("" counts.) */
static int all_variant(const char *s)
{
    return variant_tail(s) == (int)strlen(s);
}

/* Is a's every letter in b, in order? (an abbreviation: "pinballfntsies"
 * in "pinballfantasies") */
static int is_subseq(const char *a, const char *b)
{
    for (; *a && *b; b++)
        if (*a == *b)
            a++;
    return *a == 0;
}

void packs_init(PackIndex *ix)
{
    memset(ix, 0, sizeof(*ix));
}

int packs_add(PackIndex *ix, const char *folder, const char *file,
              unsigned long size, unsigned long crc)
{
    char base[128], toks[128], ver[16], tmp[128];
    const char *p, *end;
    int flen = (int)strlen(file), fdl = (int)strlen(folder), first = 1;
    unsigned long vbits = 0;
    unsigned long need;
    char *mem;
    Pack *pk;

    end = file + flen;
    if (flen > 4 && !strcmp(end - 4, ".lha"))
        end -= 4;
    else if (flen > 4 && !strcmp(end - 4, ".lzx"))
        end -= 4;
    base[0] = toks[0] = ver[0] = 0;
    for (p = file; p < end;) {
        const char *q = p;
        int len;
        while (q < end && *q != '_')
            q++;
        len = (int)(q - p);
        if (first) {
            if (len > (int)sizeof(tmp) - 1)
                len = (int)sizeof(tmp) - 1;
            memcpy(tmp, p, len);
            tmp[len] = 0;
            norm_name(tmp, base, sizeof(base));
            first = 0;
        } else if (!ver[0] && is_version_tok(p, len)) {
            int vl = is_version_tok(p, len);
            if (vl > (int)sizeof(ver) - 1)
                vl = (int)sizeof(ver) - 1;
            memcpy(ver, p + 1, vl);
            ver[vl] = 0;
        } else if (!is_number_tok(p, len) && len < (int)sizeof(tmp)) {
            char nt[128];
            memcpy(tmp, p, len);
            tmp[len] = 0;
            norm_name(tmp, nt, sizeof(nt));
            vbits |= variant_bit(nt);
            if (strlen(toks) + strlen(nt) < sizeof(toks))
                strcat(toks, nt);
        }
        p = q < end ? q + 1 : q;
    }

    if (ix->n == ix->cap) {
        int nc = ix->cap ? ix->cap * 2 : 1024;
        Pack *np = (Pack *)realloc(ix->packs, nc * sizeof(Pack));
        if (!np)
            return -1;
        ix->packs = np;
        ix->cap = nc;
    }
    /* one block per pack: file, folder, ver, full, base, tokens */
    need = (unsigned long)flen + fdl + strlen(ver) + strlen(base) * 2 + strlen(toks) * 2 + 6;
    mem = (char *)malloc(need);
    if (!mem)
        return -1;
    pk = &ix->packs[ix->n++];
    pk->size = size;
    pk->crc = crc;
    pk->vbits = vbits;
    pk->set = 0;
    pk->file = mem;   strcpy(mem, file);   mem += flen + 1;
    pk->folder = mem; strcpy(mem, folder); mem += fdl + 1;
    pk->ver = mem;    strcpy(mem, ver);    mem += strlen(ver) + 1;
    pk->full = mem;   strcpy(mem, base);   strcat(mem, toks); mem += strlen(mem) + 1;
    pk->base = mem;   strcpy(mem, base);   mem += strlen(base) + 1;
    pk->tokens = mem; strcpy(mem, toks);
    return 0;
}

static int cmp_full(const void *a, const void *b)
{
    return strcmp(sort_ix->packs[*(const int *)a].full, sort_ix->packs[*(const int *)b].full);
}

static int cmp_base(const void *a, const void *b)
{
    return strcmp(sort_ix->packs[*(const int *)a].base, sort_ix->packs[*(const int *)b].base);
}

int packs_finish(PackIndex *ix)
{
    int i;
    ix->byfull = (int *)malloc((ix->n + 1) * sizeof(int));
    ix->bybase = (int *)malloc((ix->n + 1) * sizeof(int));
    if (!ix->byfull || !ix->bybase)
        return -1;
    for (i = 0; i < ix->n; i++)
        ix->byfull[i] = ix->bybase[i] = i;
    sort_ix = ix;
    qsort(ix->byfull, ix->n, sizeof(int), cmp_full);
    qsort(ix->bybase, ix->n, sizeof(int), cmp_base);
    return 0;
}

void packs_free(PackIndex *ix)
{
    int i;
    for (i = 0; i < ix->n; i++)
        free((void *)ix->packs[i].file);
    free(ix->packs);
    free(ix->byfull);
    free(ix->bybase);
    memset(ix, 0, sizeof(*ix));
}

/* [*lo, *hi) = the entries of order[] whose key equals k. */
static int find(const PackIndex *ix, const int *order, int use_base, const char *k, int *lo, int *hi)
{
    int a = 0, b = ix->n;
    while (a < b) {
        int m = (a + b) / 2;
        const Pack *p = &ix->packs[order[m]];
        if (strcmp(use_base ? p->base : p->full, k) < 0)
            a = m + 1;
        else
            b = m;
    }
    *lo = a;
    while (b < ix->n && !strcmp(use_base ? ix->packs[order[b]].base : ix->packs[order[b]].full, k))
        b++;
    *hi = b;
    return b > a;
}

#define ANY        ((const char *)0)   /* any variant */
#define ANY_LOCAL  ((const char *)1)   /* any whose variants are all allowed */

/* Pick the newest among order[lo..hi); want_tokens = exactly these tokens
 * ("" = the plain pack), or ANY / ANY_LOCAL. */
static const Pack *newest(const PackIndex *ix, const int *order, int lo, int hi,
                          const char *want_tokens, int *variants)
{
    const Pack *best = 0;
    char seen[8][64];
    int nseen = 0, i, j;
    for (i = lo; i < hi; i++) {
        const Pack *p = &ix->packs[order[i]];
        if (!IN_SETS(p))
            continue;
        if (want_tokens == ANY_LOCAL ? (p->vbits & ~match_allowed) != 0
            : want_tokens != ANY && strcmp(p->tokens, want_tokens) &&
              !(*want_tokens && same_letters(p->tokens, want_tokens)))
            continue;
        for (j = 0; j < nseen && strcmp(seen[j], p->tokens); j++)
            ;
        if (j == nseen && nseen < 8) {
            strncpy(seen[nseen], p->tokens, 63);
            seen[nseen++][63] = 0;
        }
        if (!best || version_cmp(p->ver, best->ver) > 0)
            best = p;
    }
    if (variants)
        *variants = nseen;
    return best;
}

/* The newest pack whose variant starts the drawer's suffix, the rest of the
 * suffix being only variant words: AlienBreed3D2AGA2MB -> the AGA pack. */
static const Pack *newest_part(const PackIndex *ix, const int *order, int lo, int hi,
                               const char *suffix, int *variants)
{
    const Pack *best = 0;
    int i, bestlen = 0;
    for (i = lo; i < hi; i++) {
        const Pack *p = &ix->packs[order[i]];
        int tl = (int)strlen(p->tokens);
        if (!IN_SETS(p) || !tl || tl < bestlen || strncmp(suffix, p->tokens, tl) || !all_variant(suffix + tl))
            continue;
        if (!best || tl > bestlen || version_cmp(p->ver, best->ver) == 1) {
            best = p;
            bestlen = tl;
        }
    }
    if (best)
        *variants = 1;
    return best;
}

/* Try key k: as name+tokens, then as a base name (plain variant first). */
static const Pack *lookup(const PackIndex *ix, const char *k, int *how, int how_full, int how_base,
                          int *variants)
{
    int lo, hi;
    const Pack *p;
    if (!*k)
        return 0;
    if (find(ix, ix->byfull, 0, k, &lo, &hi) &&
        (p = newest(ix, ix->byfull, lo, hi, ANY, variants)) != NULL) {
        *how = how_full;
        return p;
    }
    if (find(ix, ix->bybase, 1, k, &lo, &hi)) {
        *how = how_base;
        p = newest(ix, ix->bybase, lo, hi, "", 0);
        if (p) {
            *variants = 1;
            return p;
        }
        p = newest(ix, ix->bybase, lo, hi, ANY_LOCAL, variants);
        if (!p)
            *how = HOW_NONE;
        return p;
    }
    return 0;
}

/* The packs whose base name starts with k, if that is exactly one base. */
static const Pack *only_longer(const PackIndex *ix, const char *k, int *variants, int *how)
{
    int a = 0, b = ix->n, kl = (int)strlen(k), lo, hi;
    const Pack *p;
    char base[128];
    if (kl < 5)
        return 0;
    while (a < b) {
        int m = (a + b) / 2;
        if (strcmp(ix->packs[ix->bybase[m]].base, k) < 0)
            a = m + 1;
        else
            b = m;
    }
    if (a >= ix->n || strncmp(ix->packs[ix->bybase[a]].base, k, kl))
        return 0;
    strcpy(base, ix->packs[ix->bybase[a]].base);
    find(ix, ix->bybase, 1, base, &lo, &hi);
    if (hi < ix->n && !strncmp(ix->packs[ix->bybase[hi]].base, k, kl))
        return 0;                       /* a second, different base: unsure */
    if (!all_variant(base + kl))
        return 0;                       /* Fire&Ice is not Fire&IceXmasDemo */
    p = newest(ix, ix->bybase, lo, hi, "", 0);
    if (p)
        *variants = 1;
    else
        p = newest(ix, ix->bybase, lo, hi, ANY_LOCAL, variants);
    if (p)
        *how = HOW_PREFIX;
    return p;
}

/* Last resort for shortened names (PinballFntsiesAGA, IndianaJonesAtlantisAdv):
 * the one base name that starts like the drawer's and holds all its letters
 * in order, the variant words at the end of the drawer name aside. */
static const Pack *abbreviation(const PackIndex *ix, const char *k, int *variants, int *how)
{
    char stem[128], tail[64];
    const char *found = 0;
    const Pack *p;
    int i, sl, lo, hi;
    sl = (int)strlen(k) - variant_tail(k);
    if (sl < 6 || sl >= (int)sizeof(stem))
        return 0;
    memcpy(stem, k, sl);
    stem[sl] = 0;
    strncpy(tail, k + sl, sizeof(tail) - 1);
    tail[sizeof(tail) - 1] = 0;
    for (i = 0; i < ix->n; i++) {
        const char *b = ix->packs[ix->bybase[i]].base;
        int bl = (int)strlen(b);
        if (!IN_SETS(&ix->packs[ix->bybase[i]]))
            continue;
        if (strncmp(b, stem, 4) || bl <= sl || sl * 2 < bl || !is_subseq(stem, b) ||
            (sl < 10 && !strncmp(b, stem, sl)))
            continue;                   /* Apache is not short for ApacheFlight, but
                                         * F1WorldChampionshipEd is for ...Edition */
        if (found && strcmp(found, b))
            return 0;                   /* two different games fit: unsure */
        found = b;
    }
    if (!found)
        return 0;
    find(ix, ix->bybase, 1, found, &lo, &hi);
    /* the longest tail that is some pack's variant ("Fntsies" + "AGA", not
     * "Fntsi" + "es" + "AGA": a language code can end a word, too) */
    p = 0;
    for (i = 0; tail[i] && !p; i++)
        if (all_variant(tail + i)) {
            p = newest(ix, ix->bybase, lo, hi, tail + i, variants);
            if (!p)
                p = newest_part(ix, ix->bybase, lo, hi, tail + i, variants);
        }
    if (!p) {
        p = newest(ix, ix->bybase, lo, hi, "", 0);
        if (p)
            *variants = 1;
    }
    if (!p && !tail[0])
        p = newest(ix, ix->bybase, lo, hi, ANY_LOCAL, variants);
    if (p)
        *how = HOW_ABBREV;
    return p;
}

void match_game(const PackIndex *ix, const char *drawer, const char *slavefile,
                const SlaveInfo *si, Match *m)
{
    char k[128], sf[128];
    const Pack *p;
    int variants = 0, i;

    memset(m, 0, sizeof(*m));
    if (si)
        strcpy(m->installed, si->version);
    norm_name(drawer, k, sizeof(k));
    p = lookup(ix, k, &m->how, HOW_NAME, HOW_BASE, &variants);

    /* AlienBreed3D2AGA2MB: the longest pack base the drawer name starts with */
    if (!p) {
        int best = -1, blen = 0, kl = (int)strlen(k);
        for (i = 0; i < ix->n; i++) {
            const char *b = ix->packs[ix->bybase[i]].base;
            int bl = (int)strlen(b);
            if (!IN_SETS(&ix->packs[ix->bybase[i]]))
                continue;
            if (bl >= 5 && bl > blen && bl < kl && !strncmp(k, b, bl) && all_variant(k + bl)) {
                best = i;
                blen = bl;
            }
        }
        if (best >= 0) {
            int lo, hi;
            char base[128];
            strcpy(base, ix->packs[ix->bybase[best]].base);
            find(ix, ix->bybase, 1, base, &lo, &hi);
            /* the suffix as tokens, else the plain pack - never some other
             * variant (Lemmings2Disk is not Lemmings_Files_AltLevels) */
            p = newest(ix, ix->bybase, lo, hi, k + blen, &variants);
            if (!p)
                p = newest_part(ix, ix->bybase, lo, hi, k + blen, &variants);
            if (!p)
                p = newest(ix, ix->bybase, lo, hi, "", &variants);
            if (p)
                m->how = HOW_PREFIX;
        }
    }
    /* LordsOfChaos -> LordsOfChaos&DataDisk: the only pack name that starts
     * with the drawer's name */
    if (!p)
        p = only_longer(ix, k, &variants, &m->how);
    if (!p && slavefile) {
        const char *dot = strrchr(slavefile, '.');
        int len = dot ? (int)(dot - slavefile) : (int)strlen(slavefile);
        char tmp[128];
        if (len > (int)sizeof(tmp) - 1)
            len = (int)sizeof(tmp) - 1;
        memcpy(tmp, slavefile, len);
        tmp[len] = 0;
        norm_name(tmp, sf, sizeof(sf));
        if (strcmp(sf, k))
            p = lookup(ix, sf, &m->how, HOW_SLAVEFILE, HOW_SLAVEFILE, &variants);
    }
    if (!p && si && si->name[0]) {
        /* "Quik The Thunder Rabbit (ECS)": the note in brackets is not the name */
        char nm[64];
        int j = 0;
        for (i = 0; si->name[i] && j < (int)sizeof(nm) - 1; i++) {
            if (si->name[i] == '(' || si->name[i] == '[')
                break;
            nm[j++] = si->name[i];
        }
        nm[j] = 0;
        norm_name(nm, sf, sizeof(sf));
        p = lookup(ix, sf, &m->how, HOW_SLAVENAME, HOW_SLAVENAME, &variants);
        if (!p) {
            p = only_longer(ix, sf, &variants, &m->how);
            if (p)
                m->how = HOW_SLAVENAME;
        }
    }

    if (!p)
        p = abbreviation(ix, k, &variants, &m->how);

    m->newest = p;
    m->variants = variants;
    if (!p) {
        m->how = HOW_NONE;
        m->status = M_NOMATCH;
    } else if (!m->installed[0]) {
        m->status = M_UNKNOWN;
    } else {
        int c = version_cmp(m->installed, p->ver);
        m->status = c == VERSION_UNSURE ? M_CHECK : c < 0 ? M_UPDATE : c > 0 ? M_NEWER : M_CURRENT;
    }
}

const char *status_word(int status)
{
    switch (status) {
    case M_UNKNOWN: return "unknown";
    case M_CURRENT: return "current";
    case M_UPDATE:  return "UPDATE";
    case M_NEWER:   return "newer";
    case M_CHECK:   return "check";
    default:        return "no-match";
    }
}
