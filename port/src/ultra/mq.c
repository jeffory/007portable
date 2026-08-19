/**
 * @file mq.c
 * Cooperative libultra message queues for the PC port.
 *
 * The N64 game is a set of threads passing messages; on PC everything runs
 * on one thread and a blocking receive/send drives the scheduler pump
 * (portSchedPump) until the queue has what the caller needs. The pump only
 * ever posts with OS_MESG_NOBLOCK, so it cannot re-enter itself.
 */
#include <ultra64.h>
#include "port.h"

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count)
{
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    s32 last;

    while (mq->validCount >= mq->msgCount) {
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }
        portSchedPump();
    }

    last = (mq->first + mq->validCount) % mq->msgCount;
    mq->msg[last] = msg;
    mq->validCount++;
    return 0;
}

s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    while (mq->validCount >= mq->msgCount) {
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }
        portSchedPump();
    }

    mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    return 0;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag)
{
    while (mq->validCount == 0) {
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }
        portSchedPump();
    }

    if (msg != NULL) {
        *msg = mq->msg[mq->first];
    }

    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    return 0;
}
