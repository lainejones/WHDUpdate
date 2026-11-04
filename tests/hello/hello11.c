/* hello11.c - hello2 (StackSwap, main(void)) with a REAL printf (a %ld
 * argument, so gcc cannot turn it into puts) on the swapped stack. */
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/dos.h>

static void mark(const char *m)
{
    BPTR f = Open((STRPTR)"TD0:hello11.trace", MODE_READWRITE);
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
volatile long value = 42;

static int real_main(void)
{
    mark("real_main");
    printf("hello11: value %ld\n", value);
    mark("printf done");
    fflush(stdout);
    mark("fflush done");
    return 0;
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
