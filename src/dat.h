/* dat.h - Retroplay's DAT (a Logiqx XML datfile) inside its zip. */
#ifndef WHDU_DAT_H
#define WHDU_DAT_H

/* One <rom> line: name = "1869_v1.2_De_0417.lha" (entities decoded),
 * folder = the enclosing <machine name="..."> ("0", "A", ...), which is
 * also the pack's folder on the server. Return nonzero to stop. */
typedef int (*dat_rom_fn)(void *ctx, const char *folder, const char *name,
                          unsigned long size, unsigned long crc);

/* Parse the first *.dat in an in-memory zip.
 * 0 = done, -1 = not a zip / no .dat, -2 = no memory, -3 = stopped,
 * -4 = bad compressed data. *date gets the header's <date> ("" if none). */
int dat_from_zip(const unsigned char *zip, unsigned long ziplen,
                 dat_rom_fn fn, void *ctx, char date[16]);

/* CRC-32 (the zip/DAT one) of a buffer, continuing from crc (start with 0). */
unsigned long crc32_update(unsigned long crc, const unsigned char *p, unsigned long n);

#endif
