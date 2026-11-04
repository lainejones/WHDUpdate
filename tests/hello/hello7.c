/* hello7.c - hello3's shape with ONLY main(argc, argv) and fputs added: no
 * division, no utility.library. */
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>

static void mark(const char *m)
{
    BPTR f = Open((STRPTR)"TD0:hello7.trace", MODE_READWRITE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, (APTR)m, strlen(m));
        Write(f, (APTR)"\n", 1);
        Close(f);
    }
}

int main(int argc, char **argv)
{
    (void)argv;
    mark("main");
    printf("hello7: argc %d\n", argc);
    fputs("a line by fputs\n", stdout);
    mark("printf done, returning");
    return 0;
}
