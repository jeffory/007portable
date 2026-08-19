/**
 * @file pi.c
 * PI (cartridge DMA) backend: reads come straight out of the ROM image
 * loaded by rom.c. "Device addresses" in this codebase are plain ROM
 * offsets from the linker script (masked with 0x1FFFFFFF like real PI DMA).
 */
#include <ultra64.h>
#include <string.h>
#include <stdio.h>
#include "port.h"

void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdMsgCnt)
{
}

static s32 piDoTransfer(u16 direction, u32 devAddr, void *dramAddr, u32 size)
{
    u32 off = devAddr & 0x1FFFFFFF;

    if (direction != OS_READ) {
        /* romWrite() is Indy-devboard only; ignore */
        return 0;
    }
    if (g_PortRomData == NULL || off >= g_PortRomSize) {
        fprintf(stderr, "port/pi: DMA read out of range: off=0x%x size=0x%x (rom 0x%x)\n",
                off, size, g_PortRomSize);
        memset(dramAddr, 0, size);
        return -1;
    }
    if (off + size > g_PortRomSize) {
        size = g_PortRomSize - off;
    }
    memcpy(dramAddr, g_PortRomData + off, size);
    return 0;
}

s32 osPiStartDma(OSIoMesg *mb, s32 priority, s32 direction, u32 devAddr, void *dramAddr,
                 u32 size, OSMesgQueue *mq)
{
    s32 ret = piDoTransfer((u16)direction, devAddr, dramAddr, size);

    /* completion is immediate; the caller blocks on mq right after */
    if (mq != NULL) {
        osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    }
    return ret;
}

s32 osPiRawStartDma(s32 direction, u32 devAddr, void *dramAddr, u32 size)
{
    return piDoTransfer((u16)direction, devAddr, dramAddr, size);
}

u32 osPiGetStatus(void)
{
    return 0; /* never busy */
}

s32 osPiWriteIo(u32 devAddr, u32 data) { return 0; }
s32 osPiReadIo(u32 devAddr, u32 *data) { *data = 0; return 0; }
