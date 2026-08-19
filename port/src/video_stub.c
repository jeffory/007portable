/**
 * @file video_stub.c
 * M1 graphics sink: accepts the gfx task, counts the display list, does
 * nothing with it. Replaced by the fast3d glue in M2.
 */
#include <ultra64.h>
#include <stdio.h>
#include "sched.h"
#include "port.h"

static u32 sTasksSeen;

void portVideoInit(void) {}

void portVideoProcessTask(OSScTask *task)
{
    sTasksSeen++;
    if (g_PortConfig.verbose && (sTasksSeen % 60) == 1) {
        u32 numCmds = ((u32)task->list.t.data_size) / 8;

        fprintf(stderr, "port/video: task %u, %u Gfx cmds\n", sTasksSeen, numCmds);
    }
}
