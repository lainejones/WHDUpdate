/* slave.h - what a WHDLoad slave says about itself.
 *
 * Plain C, no AmigaOS headers: the same code is tested on the build host. */
#ifndef WHDU_SLAVE_H
#define WHDU_SLAVE_H

typedef struct {
    unsigned short ws_version;   /* WHDLoad version the slave was written for */
    unsigned short flags;        /* ws_Flags: WHDLF_... */
    unsigned long basemem;       /* ws_BaseMemSize: the chip memory the game is given */
    long expmem;                 /* ws_ExpMem (version 8+): more, fast if there is any;
                                  * negative = only if there is some to spare */
    char name[64];               /* ws_name (slave version 10+), or "" */
    char version[16];            /* "1.2" from "Version 1.2" in ws_info, or "" */
    char date[11];               /* "2012-07-16" from "(16.07.2012)", or "" */
    char kick[48];               /* ws_kickname (version 16+): the Kickstart image it
                                  * needs, "40068.a1200" (WHDLoad loads
                                  * DEVS:Kickstarts/kick40068.a1200); several (any one will do)
                                  * with '|' between them; "" for none */
    unsigned short kickcrc[4];   /* the CRC16 WHDLoad checks each of them by, in the same order */
} SlaveInfo;

#define WHDLF_REQ68020 0x0010    /* needs a 68020 or better */
#define WHDLF_REQAGA   0x0020    /* needs the AGA chipset */

/* buf/len = the slave file (or its first part). 1 = a slave, 0 = not one. */
int slave_parse(const unsigned char *buf, long len, SlaveInfo *out);

/* Compare two install versions ("1.2", "2.06", "1.3b"): -1, 0 or 1, or
 * VERSION_UNSURE. Installs number both ways - Lotus3 went 1.51 -> 1.6
 * (decimals), IK+ went 1.9 -> 1.10 (whole numbers) - so where the two
 * readings disagree ("1.51" vs "1.6", "1.10" vs "1.7") the answer is
 * VERSION_UNSURE. A trailing letter is newer than none ("1.3b" > "1.3"),
 * 1.2.1 > 1.2, and an empty version is the oldest. */
#define VERSION_UNSURE 2
int version_cmp(const char *a, const char *b);

#endif
