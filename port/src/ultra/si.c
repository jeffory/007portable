/**
 * @file si.c
 * Controller (SI) backend. One controller is reported present; reads are
 * filled by the port input backend. Rumble/controller-pak raw SI access
 * reports "no pak", which joy.c handles gracefully.
 */
#include <ultra64.h>
#include <string.h>
#include "port.h"

static OSContPad sPads[MAXCONTROLLERS];

s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *status)
{
    int i;

    portInputInit();

    *bitpattern = 0x01; /* controller 0 connected */
    memset(status, 0, sizeof(OSContStatus) * MAXCONTROLLERS);
    status[0].type = CONT_TYPE_NORMAL;
    status[0].status = 0; /* no controller pak */
    for (i = 1; i < MAXCONTROLLERS; i++) {
        status[i].cont_errno = CONT_NO_RESPONSE_ERROR;
    }
    return 0;
}

s32 osContStartQuery(OSMesgQueue *mq)
{
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

void osContGetQuery(OSContStatus *status)
{
    int i;

    memset(status, 0, sizeof(OSContStatus) * MAXCONTROLLERS);
    status[0].type = CONT_TYPE_NORMAL;
    for (i = 1; i < MAXCONTROLLERS; i++) {
        status[i].cont_errno = CONT_NO_RESPONSE_ERROR;
    }
}

s32 osContStartReadData(OSMesgQueue *mq)
{
    portInputRead(sPads);
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

void osContGetReadData(OSContPad *pads)
{
    memcpy(pads, sPads, sizeof(sPads));
}

s32 osContSetCh(u8 ch) { return 0; }

/* --- raw SI / controller pak / rumble: nothing attached -------------------
 * joy.c/motor.c call the internal double-underscore entry points. */

#include "libultra/io/controller.h"

u8 __osContLastCmd;
OSPifRam __osPfsPifRam;

s32 __osSiGetAccess(void) { return 0; }
void __osSiRelAccess(void) {}
s32 __osSiRawStartDma(s32 direction, void *dramAddr) { return -1; }
s32 __osContRamRead(OSMesgQueue *mq, s32 channel, u16 address, u8 *buffer) { return PFS_ERR_NOPACK; }
s32 __osContRamWrite(OSMesgQueue *mq, s32 channel, u16 address, u8 *buffer, s32 force) { return PFS_ERR_NOPACK; }
u8 __osContAddressCrc(u16 addr) { return 0; }

/* osPfsChecker and osMotor* are defined by kept game code (src/joy.c,
 * src/motor.c) on top of the raw SI stubs below. */
s32 osPfsInit(OSMesgQueue *mq, OSPfs *pfs, s32 channel) { return PFS_ERR_NOPACK; }
s32 osPfsInitPak(OSMesgQueue *mq, OSPfs *pfs, s32 channel) { return PFS_ERR_NOPACK; }
s32 osPfsPifRam(s32 channel) { return PFS_ERR_NOPACK; }

s32 osContRamRead(OSMesgQueue *mq, s32 channel, u16 address, u8 *buffer) { return PFS_ERR_NOPACK; }
s32 osContRamWrite(OSMesgQueue *mq, s32 channel, u16 address, u8 *buffer, s32 force) { return PFS_ERR_NOPACK; }
u8 osContAddressCrc(u16 addr) { return 0; }
