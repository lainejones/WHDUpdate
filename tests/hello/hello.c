/* hello.c - does a plain libnix printf survive on this machine? */
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>

static void mark(const char *m)
{
    BPTR f = Open((STRPTR)"T:hello.trace", MODE_READWRITE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, (APTR)m, strlen(m));
        Write(f, (APTR)"\n", 1);
        Close(f);
    }
}

int main(void)
{
    mark("main");
    printf("hello from libnix\n");
    mark("printf done");
    fflush(stdout);
    mark("fflush done");
    printf("second line %ld\n", 42L);
    mark("second printf done, returning");
    return 0;
}
