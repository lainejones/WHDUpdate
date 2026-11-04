/* hello2.c - the same printf, but on a StackSwap()ed 32 KB stack as
 * WHDUpdate's main() does when started by Run (4 KB stack). */
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/dos.h>

static void mark(const char *m)
{
    BPTR f = Open((STRPTR)"T:hello2.trace", MODE_READWRITE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, (APTR)m, strlen(m));
        Write(f, (APTR)"\n", 1);
        Close(f);
    }
}

#define NEED_STACK 32768UL
static struct StackSwapStruct swap;
static APTR swapMem;
static int swapRc;

static int real_main(void)
{
    mark("real_main");
    printf("hello from the swapped stack\n");
    mark("printf done");
    fflush(stdout);
    mark("fflush done");
    return 0;
}

int main(void)
{
    struct Task *me = FindTask(NULL);
    mark("main");
    if ((ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower >= NEED_STACK - 4096) {
        mark("stack big enough, no swap");
        return real_main();
    }
    if (!(swapMem = AllocVec(NEED_STACK, MEMF_ANY)))
        return 20;
    swap.stk_Lower = swapMem;
    swap.stk_Upper = (ULONG)swapMem + NEED_STACK;
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    mark("swapping");
    StackSwap(&swap);
    swapRc = real_main();
    StackSwap(&swap);
    mark("swapped back");
    FreeVec(swapMem);
    mark("returning");
    return swapRc;
}
