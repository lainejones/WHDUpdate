/* test_host.c - the platform-neutral parts, tested on the build host.
 *
 *   test_host                         unit checks only
 *   test_host dat GAMES.zip           parse a real DAT zip
 *   test_host slave FILE...           print what each slave says
 *   test_host match GAMES.zip NAMES   match "drawer<TAB>slavefile" lines
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/slave.h"
#include "../src/dat.h"
#include "../src/match.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static unsigned char *load(const char *fn, long *len)
{
    FILE *f = fopen(fn, "rb");
    unsigned char *b;
    if (!f) { perror(fn); exit(2); }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc(*len + 1);
    if (fread(b, 1, *len, f) != (size_t)*len) { perror(fn); exit(2); }
    fclose(f);
    return b;
}

static int n_missing;
static const char *missing_files[64];
static void on_missing(void *ctx, const Pack *p)
{
    (void)ctx;
    if (n_missing < 64)
        missing_files[n_missing++] = p->file;
}
static int missing_has(const char *file)
{
    int i;
    for (i = 0; i < n_missing; i++)
        if (!strcmp(missing_files[i], file))
            return 1;
    return 0;
}

static void units(void)
{
    match_allowed = variants_default();
    char out[64];
    CHECK(version_cmp("1.2", "1.10") == VERSION_UNSURE);
    CHECK(version_cmp("1.51", "1.6") == VERSION_UNSURE);
    CHECK(version_cmp("1.5", "1.6") < 0);
    CHECK(version_cmp("2.06", "2.5") == VERSION_UNSURE);
    CHECK(version_cmp("2.06", "2.07") < 0);
    CHECK(version_cmp("1.20", "1.2") == VERSION_UNSURE);
    CHECK(version_cmp("1.3b", "1.3") > 0);
    CHECK(version_cmp("1.3", "1.3") == 0);
    CHECK(version_cmp("2.0", "1.9.9") > 0);
    CHECK(version_cmp("1.2.1", "1.2") > 0);
    CHECK(version_cmp("", "1.0") < 0);
    CHECK(version_cmp("1.0", "") > 0);
    norm_name("Bubble&Squeak!", out, sizeof(out));
    CHECK(!strcmp(out, "bubblesqueak"));
    {
        /* a minimal fake slave: hunk header filler + security + id + v10 fields */
        unsigned char s[256];
        SlaveInfo si;
        memset(s, 0, sizeof(s));
        memcpy(s + 32, "\x70\xff\x4e\x75WHDLOADS", 12);
        s[32 + 12] = 0; s[32 + 13] = 17;                /* ws_Version 17 */
        s[32 + 0x24] = 0; s[32 + 0x25] = 0x40;          /* ws_name */
        s[32 + 0x28] = 0; s[32 + 0x29] = 0x50;          /* ws_info */
        strcpy((char *)s + 32 + 0x40, "Test Game");
        strcpy((char *)s + 32 + 0x50, "Installed by X\nVersion 1.3b (16.07.12)");
        CHECK(slave_parse(s, sizeof(s), &si) == 1);
        CHECK(si.ws_version == 17);
        CHECK(!strcmp(si.name, "Test Game"));
        CHECK(!strcmp(si.version, "1.3b"));
        CHECK(!strcmp(si.date, "2012-07-16"));
        CHECK(slave_parse((const unsigned char *)"not a slave at all", 18, &si) == 0);
    }
    {
        PackIndex ix;
        Match m;
        SlaveInfo si;
        packs_init(&ix);
        packs_add(&ix, "0", "1869_v1.0_AGA.lha", 1, 1);
        packs_add(&ix, "0", "1869_v1.2.lha", 1, 1);
        packs_add(&ix, "0", "1869_v1.2_De_0417.lha", 1, 1);
        packs_add(&ix, "A", "AlienBreed3D2_v1.1_AGA_0280.lha", 1, 1);
        packs_add(&ix, "A", "Aaargh!_v1.0a_0511.lha", 1, 1);
        packs_add(&ix, "B", "BuckRogers_v1.3_De_0445.lha", 1, 1);
        packs_add(&ix, "C", "ConquestsOfTheLongbow_v1.1-B.lha", 1, 1);
        packs_add(&ix, "H", "Hybris_v2.4_NTSC_1457.lha", 1, 1);
        packs_add(&ix, "L", "LordsOfChaos&DataDisk_v1.2.lha", 1, 1);
        packs_add(&ix, "B", "Bubble+_v1.0.lha", 1, 1);
        packs_add(&ix, "F", "Fire&IceXmasDemo_v1.0.lha", 1, 1);
        packs_add(&ix, "P", "PinballFantasies_v1.3_AGA.lha", 1, 1);
        packs_add(&ix, "P", "PinballFantasies_v1.3.lha", 1, 1);
        packs_add(&ix, "M", "MortalKombat2_v1.4_1105.lha", 1, 1);
        packs_add(&ix, "L", "Lemmings_v1.5_Files_AltLevels_2238.lha", 1, 1);
        packs_add(&ix, "A", "ApacheFlight_v1.0.lha", 1, 1);
        packs_add(&ix, "C", "Corporation&Missions_v1.4_2Disk_0107&0266.lha", 1, 1);
        packs_add(&ix, "C", "Corporation_v1.4_1Disk_0107.lha", 1, 1);
        packs_add(&ix, "L", "Lemmings2_v1.8_1MB_NTSC_1976.lha", 1, 1);
        packs_add(&ix, "F", "F1WorldChampionshipEdition_v1.0_2061.lha", 1, 1);
        packs_add(&ix, "J", "Jet&HawaiianSceneryDisk_v1.0.lha", 1, 1);
        packs_add(&ix, "J", "Jet_v1.0.lha", 1, 1);
        packs_add(&ix, "0", "4DSportsDriving&MasterTracks_v1.2_1461.lha", 1, 1);
        packs_add(&ix, "0", "4DSportsDriving_v1.2_1461.lha", 1, 1);
        CHECK(packs_finish(&ix) == 0);
        memset(&si, 0, sizeof(si));
        strcpy(si.version, "1.0");
        match_game(&ix, "1869De", "1869.slave", &si, &m);
        CHECK(m.how == HOW_NAME && m.status == M_UPDATE && !strcmp(m.newest->file, "1869_v1.2_De_0417.lha"));
        match_game(&ix, "1869", "1869.slave", &si, &m);
        CHECK(m.how == HOW_NAME && !strcmp(m.newest->file, "1869_v1.2.lha"));
        match_game(&ix, "1869AGA", "1869.slave", &si, &m);
        CHECK(m.status == M_CURRENT && !strcmp(m.newest->file, "1869_v1.0_AGA.lha"));
        match_game(&ix, "AlienBreed3D2AGA2MB", "AB3D2.slave", &si, &m);
        CHECK(m.how == HOW_PREFIX && !strcmp(m.newest->file, "AlienBreed3D2_v1.1_AGA_0280.lha"));
        match_game(&ix, "Aaargh", "Aaargh.slave", &si, &m);
        CHECK(m.how == HOW_NAME && m.status == M_UPDATE);   /* 1.0 < 1.0a */
        match_game(&ix, "BuckRogers", "BuckRogers.slave", &si, &m);
        CHECK(m.status == M_NOMATCH);                       /* only a German pack */
        match_game(&ix, "Hybris", "Hybris.slave", &si, &m);
        CHECK(m.status == M_UPDATE);                        /* NTSC is the only one */
        match_game(&ix, "LordsOfChaos", "L.slave", &si, &m);
        CHECK(m.how == HOW_PREFIX && m.status == M_UPDATE);
        match_game(&ix, "BuckRogersDe", "BuckRogers.slave", &si, &m);
        CHECK(m.status == M_UPDATE);
        {   /* Not wanted, on an ECS machine: the default without AGA */
            unsigned long aga = 0, ecs;
            int k;
            for (k = 0; variant_words[k].word; k++)
                if (!strcmp(variant_words[k].word, "aga"))
                    aga = 1UL << k;
            ecs = variants_default() & ~aga;
            CHECK(variants_unwanted(m.newest->vbits, ecs));          /* BuckRogers De */
            match_game(&ix, "1869AGA", "1869.slave", &si, &m);
            CHECK(variants_unwanted(m.newest->vbits, ecs));
            CHECK(!variants_unwanted(m.newest->vbits, variants_default()));
            match_game(&ix, "1869", "1869.slave", &si, &m);
            CHECK(!variants_unwanted(m.newest->vbits, ecs));
            match_game(&ix, "Hybris", "Hybris.slave", &si, &m);
            CHECK(!variants_unwanted(m.newest->vbits, ecs));         /* NTSC is on */
            match_game(&ix, "Lemmings2", "L2.slave", &si, &m);       /* 1MB_NTSC: 1MB is no variant word */
            CHECK(m.newest && !variants_unwanted(m.newest->vbits, ecs));
        }
        match_game(&ix, "ConquestsOfTheLongbow", "C.slave", &si, &m);
        CHECK(m.status == M_UPDATE && !strcmp(m.newest->ver, "1.1"));
        match_game(&ix, "Bubble&Squeak", "B.slave", &si, &m);
        CHECK(m.status == M_NOMATCH);                       /* not Bubble+ */
        match_game(&ix, "Fire&Ice", "F.slave", &si, &m);
        CHECK(m.status == M_NOMATCH);                       /* not the Xmas demo */
        match_game(&ix, "PinballFntsiesAGA", "P.slave", &si, &m);
        CHECK(m.how == HOW_ABBREV && !strcmp(m.newest->file, "PinballFantasies_v1.3_AGA.lha"));
        match_game(&ix, "MortalKombat22MB", "M.slave", &si, &m);
        CHECK(m.how == HOW_PREFIX && m.status == M_UPDATE);
        match_game(&ix, "Lemmings2Disk", "L.slave", &si, &m);
        CHECK(m.status == M_NOMATCH);
        match_game(&ix, "Apache", "A.slave", &si, &m);
        CHECK(m.status == M_NOMATCH);
        match_game(&ix, "4DSportsDriving&MstrTrcks", "4DSportsDriving.slave", &si, &m);
        CHECK(!strcmp(m.newest->file, "4DSportsDriving&MasterTracks_v1.2_1461.lha"));
        match_game(&ix, "4DSportsDriving", "4DSportsDriving.slave", &si, &m);
        CHECK(!strcmp(m.newest->file, "4DSportsDriving_v1.2_1461.lha"));
        {   /* the missing games: every base no installed game has, one per base
             * (variants allowed only: no German 1869, no NTSC-only Hybris here) */
            static const char *const present[] = {"1869", "aaargh", "alienbreed3d2"};
            unsigned long keep = match_allowed;
            match_allowed = variants_default() & ~0UL;
            n_missing = 0;
            packs_missing(&ix, present, 3, on_missing, NULL);
            CHECK(n_missing > 5);
            CHECK(missing_has("4DSportsDriving&MasterTracks_v1.2_1461.lha"));
            CHECK(missing_has("Jet_v1.0.lha"));
            CHECK(!missing_has("1869_v1.2.lha") && !missing_has("Aaargh!_v1.0a_0511.lha"));
            CHECK(!missing_has("BuckRogers_v1.3_De_0445.lha"));      /* German only */
            match_allowed = keep;
        }
        match_game(&ix, "Jet&HawaiianScenery", "J.slave", &si, &m);
        CHECK(m.status != M_NOMATCH && !strcmp(m.newest->file, "Jet&HawaiianSceneryDisk_v1.0.lha"));
        match_game(&ix, "Corporation&Missions2Disk", "Corporation.slave", &si, &m);
        CHECK(m.newest && !strcmp(m.newest->file, "Corporation&Missions_v1.4_2Disk_0107&0266.lha"));
        match_game(&ix, "Lemmings2NTSC1MB", "Lemmings2.slave", &si, &m);
        CHECK(m.newest && !strcmp(m.newest->file, "Lemmings2_v1.8_1MB_NTSC_1976.lha"));
        match_game(&ix, "F1WorldChampionshipEd", "F1.slave", &si, &m);
        CHECK(m.newest && !strcmp(m.newest->file, "F1WorldChampionshipEdition_v1.0_2061.lha"));
        /* a pack set not in use is not matched against */
        ix.packs[0].set = 1;                 /* 1869_v1.0_AGA from another set */
        match_sets = 1;
        match_game(&ix, "1869AGA", "1869.slave", &si, &m);
        CHECK(!m.newest || strcmp(m.newest->file, "1869_v1.0_AGA.lha"));
        match_sets = 3;
        match_game(&ix, "1869AGA", "1869.slave", &si, &m);
        CHECK(m.newest && !strcmp(m.newest->file, "1869_v1.0_AGA.lha"));
        match_sets = 0xffffffffUL;
        si.version[0] = 0;
        match_game(&ix, "Nothing", "Nothing.slave", &si, &m);
        CHECK(m.status == M_NOMATCH);
        packs_free(&ix);
    }
}

typedef struct { PackIndex *ix; long n; char first[128]; } DatCtx;

static int on_rom(void *c, const char *folder, const char *name, unsigned long size, unsigned long crc)
{
    DatCtx *d = c;
    if (!d->n++)
        snprintf(d->first, sizeof(d->first), "%s/%s %lu %08lx", folder, name, size, crc);
    if (d->ix)
        packs_add(d->ix, folder, name, size, crc);
    return 0;
}

int main(int argc, char **argv)
{
    units();
    if (argc >= 3 && !strcmp(argv[1], "dat")) {
        long len;
        unsigned char *z = load(argv[2], &len);
        DatCtx d = {0};
        char date[16];
        int rc = dat_from_zip(z, len, on_rom, &d, date);
        printf("dat rc=%d date=%s roms=%ld first=%s\n", rc, date, d.n, d.first);
        CHECK(rc == 0 && d.n > 1000);
    } else if (argc >= 3 && !strcmp(argv[1], "slave")) {
        int i;
        for (i = 2; i < argc; i++) {
            long len;
            unsigned char *b = load(argv[i], &len);
            SlaveInfo si;
            int ok = slave_parse(b, len, &si);
            printf("%s\t%d\tws%u\t%s\t%s\t%s\n", argv[i], ok, si.ws_version, si.version, si.date, si.name);
            free(b);
        }
    } else if (argc >= 4 && !strcmp(argv[1], "match")) {
        long len, counts[7] = {0}, st[6] = {0};
        unsigned char *z = load(argv[2], &len);
        PackIndex ix;
        DatCtx d = {0};
        char line[512], date[16];
        FILE *f = fopen(argv[3], "r");
        packs_init(&ix);
        d.ix = &ix;
        CHECK(dat_from_zip(z, len, on_rom, &d, date) == 0);
        CHECK(packs_finish(&ix) == 0);
        while (f && fgets(line, sizeof(line), f)) {
            char *tab = strchr(line, '\t'), *nl;
            Match m;
            SlaveInfo si;
            memset(&si, 0, sizeof(si));
            if ((nl = strpbrk(line, "\r\n")))
                *nl = 0;
            if (tab) {
                char *t2;
                *tab++ = 0;
                if ((t2 = strchr(tab, '\t'))) {      /* drawer slave version name */
                    char *t3;
                    *t2++ = 0;
                    if ((t3 = strchr(t2, '\t'))) {
                        *t3++ = 0;
                        strncpy(si.name, t3, sizeof(si.name) - 1);
                    }
                    strncpy(si.version, t2, sizeof(si.version) - 1);
                }
            }
            match_game(&ix, line, tab, &si, &m);
            counts[m.how]++;
            st[m.status]++;
            if (argc > 4)
                printf("%-8s %-30s %-6s %s\n", status_word(m.status), line, si.version,
                       m.newest ? m.newest->file : "-");
        }
        printf("how: name %ld base %ld prefix %ld slavefile %ld slavename %ld abbrev %ld none %ld\n",
               counts[HOW_NAME], counts[HOW_BASE], counts[HOW_PREFIX], counts[HOW_SLAVEFILE],
               counts[HOW_SLAVENAME], counts[HOW_ABBREV], counts[HOW_NONE]);
        printf("status: current %ld update %ld newer %ld check %ld unknown %ld no-match %ld\n",
               st[M_CURRENT], st[M_UPDATE], st[M_NEWER], st[M_CHECK], st[M_UNKNOWN], st[M_NOMATCH]);
        packs_free(&ix);
    }
    printf(fails ? "%d check(s) failed\n" : "all host checks passed\n", fails);
    return fails != 0;
}
