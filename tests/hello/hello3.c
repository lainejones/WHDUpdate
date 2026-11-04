/* hello3.c - hello2 plus what WHDUpdate's usage path does besides: ReadArgs
 * on its template, a few malloc/free calls, free(NULL), then return. */
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
    BPTR f = Open((STRPTR)"TD0:hello3.trace", MODE_READWRITE);
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
static int swapRc;

static int real_main(void)
{
    LONG args[11] = {0};
    struct RDArgs *rda;
    void *a, *b;
    mark("real_main");
    a = malloc(65536);
    b = malloc(100);
    mark("malloc done");
    rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    mark(rda ? "ReadArgs done" : "ReadArgs FAILED");
    printf("hello3: usage would go here\n");
    mark("printf done");
    if (rda)
        FreeArgs(rda);
    free(b);
    free(a);
    free(NULL);
    free(NULL);
    mark("frees done (incl. two free(NULL))");
    return 5;
}

int main(void)
{
    struct Task *me = FindTask(NULL);
    mark("main");
    if ((ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower >= NEED_STACK - 4096)
        return real_main();
    if (!(swapMem = AllocVec(NEED_STACK, MEMF_ANY)))
        return 20;
    swap.stk_Lower = swapMem;
    swap.stk_Upper = (ULONG)swapMem + NEED_STACK;
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    StackSwap(&swap);
    swapRc = real_main();
    StackSwap(&swap);
    mark("swapped back");
    FreeVec(swapMem);
    mark("returning");
    return swapRc;
}
