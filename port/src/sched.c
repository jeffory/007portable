/**
 * @file sched.c (port)
 * Cooperative scheduler replacing src/sched.c.
 *
 * The game observes the N64 scheduler through exactly four seams:
 *   1. OS_SC_RETRACE_MSG posted to client queues (gfxFrameMsgQ) each vsync
 *   2. gfx tasks submitted on sched_cmdQ by rspGfxTaskStart()
 *   3. OS_SC_DONE_MSG sent back to task->msgQ when the RCP finishes
 *   4. joyPoll()/musicFadeTick()/viVsyncRelated() called at retrace time
 *
 * portSchedPump() reproduces those four behaviors on one thread. It is
 * called from blocking osRecvMesg/osSendMesg (see port/src/ultra/mq.c),
 * so whenever the game waits for a message, time advances here.
 */
#include <ultra64.h>
#include "sched.h"
#include "joy.h"
#include "music.h"
#include "fr.h"
#include "port.h"

/* --- state shared with the game (normally owned by init.c/sched.c) -------- */
OSMesgQueue gfxFrameMsgQ;
OSMesg gfxFrameMsgBuf[32];
OSMesgQueue *sched_cmdQ;

f32 g_ViXScales[2] = {1.0f, 1.0f};
f32 g_ViYScales[2] = {1.0f, 1.0f};
s32 g_ViChangeVideoModes[2] = {0, 0};
OSViMode g_ViModes[2];
OSViMode *g_ViModePtrs[2];
s32 dword_CODE_bss_8005DBE8[2];

/* --- port-private ----------------------------------------------------------- */
#define RETRACE_NS (1000000000ull / 60ull)

static OSMesgQueue sCmdQ;
static OSMesg sCmdMsgBuf[OS_SC_MAX_MESGS];
static OSScMsg sRetraceMsg = { OS_SC_RETRACE_MSG };
static u64 sNextRetraceNs;
static u32 sFrameCount;
static int sInPump; /* guards against re-entry via osSendMesg->pump */

static struct {
    OSMesgQueue *msgQ;
    int used;
} sClients[4];

void portTimersService(void); /* port/src/ultra/timers.c */

void portSchedInit(void)
{
    osCreateMesgQueue(&gfxFrameMsgQ, gfxFrameMsgBuf, 32);
    osCreateMesgQueue(&sCmdQ, sCmdMsgBuf, OS_SC_MAX_MESGS);
    sched_cmdQ = &sCmdQ;
    sClients[0].msgQ = &gfxFrameMsgQ;
    sClients[0].used = 1;
    sNextRetraceNs = portPlatformTimeNs() + RETRACE_NS;
}

/* audi.c would call these in M4; provide them now for link completeness */
void osScAddClient(OSSched *s, OSScClient *c, OSMesgQueue *msgQ, OSScClient *next)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (!sClients[i].used) {
            sClients[i].msgQ = msgQ;
            sClients[i].used = 1;
            return;
        }
    }
}

void osScRemoveClient(OSSched *s, OSScClient *c) {}

OSMesgQueue *osScGetCmdQ(OSSched *s)
{
    return &sCmdQ;
}

static void schedDrainTasks(void)
{
    OSScTask *task;

    while (osRecvMesg(&sCmdQ, (OSMesg *)&task, OS_MESG_NOBLOCK) == 0) {
        portVideoProcessTask(task);
        if (task->flags & OS_SC_SWAPBUFFER) {
            portPlatformPresent(task->framebuffer);
        }
        if (task->msgQ != NULL) {
            osSendMesg(task->msgQ, task->msg, OS_MESG_NOBLOCK);
        }
    }
}

static void schedRetrace(void)
{
    extern OSViMode *viMode; /* fr.c */
    int i;

    sFrameCount++;
    /* On N64, retraces during early boot call viVsyncRelated() while
     * viMode is still NULL; the writes land in TLB-mapped pages and are
     * harmless. On PC that's a real NULL deref, so wait until the game
     * has picked a video mode. */
    if (viMode != NULL) {
        viVsyncRelated();
    }
    joyPoll();
    musicFadeTick();
    portAudioFrame(); /* M4: synthesize + queue one video frame of audio */

    {
        static u32 sRetraceTick;
        void portStateHashTick(u32 tick);
        portStateHashTick(++sRetraceTick); /* PORT_STATE_HASH, phase 2 */
    }

    for (i = 0; i < 4; i++) {
        if (sClients[i].used) {
            osSendMesg(sClients[i].msgQ, (OSMesg)&sRetraceMsg, OS_MESG_NOBLOCK);
        }
    }
}

void portSchedPump(void)
{
    u64 now;

    if (sInPump) {
        return; /* a NOBLOCK send filled a queue mid-pump; nothing to do */
    }
    sInPump = 1;

    portPlatformPoll();

    /* PORT_DETERMINISTIC: virtual time advances exactly one retrace period
     * per pump, making every derived clock (osGetCount, audio budget,
     * retrace/poll counts) a pure function of the pump count — headless
     * runs become bit-reproducible and run flat out (the sleep branch
     * below can never be taken: `now` always lands on the deadline). */
    portTimeVirtualStep(RETRACE_NS);

    portTimersService();
    schedDrainTasks();

    now = portPlatformTimeNs();
    if (now >= sNextRetraceNs) {
        /* Accumulate the deadline instead of rebasing off `now`: rebasing
         * added every pump's sleep overshoot (~1-2ms) to the frame period,
         * so retraces ran at ~55-57Hz and fixed-timeline scenes (the intro
         * cinemas) played in slow motion. Only resync when we've fallen a
         * whole period behind (slow machine) — never burst retraces. */
        sNextRetraceNs += RETRACE_NS;
        if (now >= sNextRetraceNs) {
            sNextRetraceNs = now + RETRACE_NS;
        }
        schedRetrace();
    } else if (sNextRetraceNs - now > 2000000ull) {
        /* coarse sleep while far out; inside the last 2ms just return so the
         * caller's recv loop re-pumps and the retrace fires on time */
        portPlatformSleepMs(1);
    }

    sInPump = 0;
}

/* --- misc sched.c exports referenced by kept files -------------------------- */
static u32 sCounters[16];

u32 *get_counters(void)
{
    return sCounters;
}

void activate_stderr(u32 flag) {}
void enable_stderr(u32 flag) {}
void permit_stderr(u32 flag) {}
void setUserCompareValue(u32 value) {}
void CheckDisplayErrorBuffer(u32 *buffer) {}
void CheckDisplayErrorBufferEvery16Frames(u32 framecount) {}
void osCreateLog(void) {}
