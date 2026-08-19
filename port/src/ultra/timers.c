/**
 * @file timers.c
 * osGetCount/osGetTime/osSetTimer for the PC port.
 *
 * The counter runs at the VR4300 rate (46.875 MHz) scaled from the host
 * monotonic clock; frametiming.c and boss.c do interval arithmetic in these
 * units. Timers are a simple deadline list serviced by portSchedPump().
 */
#include <ultra64.h>
#include "port.h"

#define MAX_TIMERS 8

struct porttimer {
    OSTimer *timer;
    u64 deadlineNs;
    u64 intervalNs;
    int active;
};

static struct porttimer sTimers[MAX_TIMERS];

static u64 counterToNs(OSTime cycles)
{
    return (u64)cycles * 1000000000ull / PORT_COUNTER_HZ;
}

/* 46,875,000 / 1e9 reduces to exactly 3/64. Rebasing to process start
 * keeps ns*3 far from u64 overflow (the raw monotonic clock is machine
 * uptime, and multiplying that by the rate overflows within days --
 * which made the counter jump backwards and stalled the main loop). */
static u64 counterNow(void)
{
    static u64 t0;
    u64 ns = portPlatformTimeNs();

    if (t0 == 0) {
        t0 = ns;
    }
    return (ns - t0) * 3 / 64;
}

u32 osGetCount(void)
{
    return (u32)counterNow();
}

OSTime osGetTime(void)
{
    return (OSTime)counterNow();
}

void osSetTime(OSTime time)
{
    /* only used to zero the time base on N64; harmless to ignore */
}

s32 osSetTimer(OSTimer *timer, OSTime countdown, OSTime interval, OSMesgQueue *mq, OSMesg msg)
{
    int i;

    timer->interval = interval;
    timer->mq = mq;
    timer->msg = msg;

    for (i = 0; i < MAX_TIMERS; i++) {
        if (!sTimers[i].active) {
            sTimers[i].timer = timer;
            sTimers[i].deadlineNs = portPlatformTimeNs() + counterToNs(countdown ? countdown : interval);
            sTimers[i].intervalNs = counterToNs(interval);
            sTimers[i].active = 1;
            return 0;
        }
    }
    return -1;
}

s32 osStopTimer(OSTimer *timer)
{
    int i;

    for (i = 0; i < MAX_TIMERS; i++) {
        if (sTimers[i].active && sTimers[i].timer == timer) {
            sTimers[i].active = 0;
            return 0;
        }
    }
    return -1;
}

/* Called from portSchedPump(). */
void portTimersService(void)
{
    u64 now = portPlatformTimeNs();
    int i;

    for (i = 0; i < MAX_TIMERS; i++) {
        if (sTimers[i].active && now >= sTimers[i].deadlineNs) {
            osSendMesg(sTimers[i].timer->mq, sTimers[i].timer->msg, OS_MESG_NOBLOCK);
            if (sTimers[i].intervalNs != 0) {
                sTimers[i].deadlineNs += sTimers[i].intervalNs;
            } else {
                sTimers[i].active = 0;
            }
        }
    }
}
