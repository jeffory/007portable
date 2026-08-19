/**
 * @file threads.c
 * No-op thread layer. The port runs everything cooperatively on one host
 * thread: the game's "threads" (idle/rmon/sched/audio) either don't exist
 * or are folded into portSchedPump(), and bossEntry() is called directly.
 */
#include <ultra64.h>

void osCreateThread(OSThread *t, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri pri)
{
    /* never started; the port calls the work it needs directly */
}

void osStartThread(OSThread *t) {}
void osStopThread(OSThread *t) {}
void osDestroyThread(OSThread *t) {}
void osYieldThread(void) {}
void osSetThreadPri(OSThread *t, OSPri pri) {}

OSPri osGetThreadPri(OSThread *t)
{
    return 10;
}

OSId osGetThreadId(OSThread *t)
{
    return 3; /* MAIN_THREAD_ID */
}
