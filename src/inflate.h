/* inflate.h - raw DEFLATE (RFC 1951) decoder, streaming its output.
 *
 * The compressed data is all in memory (the DAT zip is a few hundred KB);
 * the output goes to a callback in pieces of up to 32 KB, so the unpacked
 * file (a few MB) never has to fit in RAM at once. */
#ifndef WHDU_INFLATE_H
#define WHDU_INFLATE_H

/* return nonzero to stop */
typedef int (*inflate_sink)(void *ctx, const unsigned char *data, unsigned long len);

/* 0 = done, -1 = bad data, -2 = no memory, -3 = the sink stopped it.
 * *used (if not NULL) = compressed bytes consumed. */
int inflate_raw(const unsigned char *in, unsigned long inlen,
                inflate_sink sink, void *ctx, unsigned long *used);

#endif
