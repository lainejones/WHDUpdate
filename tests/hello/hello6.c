/* hello6.c - hello3 plus ONLY a 32-bit division by a variable (libnix20's
 * __divsi3 -> utility.library auto-opened and auto-closed by libnix). */
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>

static void mark(const char *m)
{
    BPTR f = Open((STRPTR)"TD0:hello6.trace", MODE_READWRITE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, (APTR)m, strlen(m));
        Write(f, (APTR)"\n", 1);
        Close(f);
    }
}

volatile long divisor = 7;

int main(void)
{
    long q = 123456L / divisor;
    mark("main");
    printf("hello6: %ld\n", q);
    mark("printf done, returning");
    return 0;
}
