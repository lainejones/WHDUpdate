/* hello5.c - hello3 with main(argc, argv), a 32-bit division (libnix20's
 * __divsi3 opens utility.library behind the scenes on a 68000 build) and
 * fputs: closer still to WHDUpdate's usage path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

static void mark(const char *m)
{
    BPTR f = Open((STRPTR)"TD0:hello5.trace", MODE_READWRITE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, (APTR)m, strlen(m));
        Write(f, (APTR)"\n", 1);
        Close(f);
    }
}

#define TEMPLATE "DIR/M,ALL/S,UPDATE/S,YES/S,GAME/K,SHOWALL/S,REPORT/K,OFFLINE/S,FTP/S,DEPTH/K/N,GUI/S"
#define NEED_STACK 32768UL
static struct StackSwapStruct swap;
static APTR swapMem;
static int swapRc, g_argc;
static char **g_argv;
volatile long divisor = 7;

static int real_main(int argc, char **argv)
{
    LONG args[11] = {0};
    struct RDArgs *rda = NULL;
    void *a, *b;
    long q = 123456L / divisor;
    (void)argv;
    mark("real_main");
    a = malloc(65536);
    b = malloc(100);
    if (argc) {
        rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
        mark(rda ? "ReadArgs done" : "ReadArgs FAILED");
    }
    printf("hello5: usage would go here (%ld)\n", q);
    fputs("a line by fputs\n", stdout);
    fflush(stdout);
    mark("printf done");
    if (rda)
        FreeArgs(rda);
    free(b);
    free(a);
    free(NULL);
    mark("frees done");
    return 5;
}

int main(int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    mark("main");
    if ((ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower >= NEED_STACK - 4096)
        return real_main(argc, argv);
    if (!(swapMem = AllocVec(NEED_STACK, MEMF_ANY)))
        return 20;
    g_argc = argc;
    g_argv = argv;
    swap.stk_Lower = swapMem;
    swap.stk_Upper = (ULONG)swapMem + NEED_STACK;
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    StackSwap(&swap);
    swapRc = real_main(g_argc, g_argv);
    StackSwap(&swap);
    mark("swapped back");
    FreeVec(swapMem);
    mark("returning");
    return swapRc;
}
