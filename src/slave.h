/* slave.h - what a WHDLoad slave says about itself.
 *
 * Plain C, no AmigaOS headers: the same code is tested on the build host. */
#ifndef WHDU_SLAVE_H
#define WHDU_SLAVE_H

typedef struct {
    unsigned short ws_version;   /* WHDLoad version the slave was written for */
    char name[64];               /* ws_name (slave version 10+), or "" */
    char version[16];            /* "1.2" from "Version 1.2" in ws_info, or "" */
    char date[11];               /* "2012-07-16" from "(16.07.2012)", or "" */
} SlaveInfo;

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
