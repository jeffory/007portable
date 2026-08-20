/**
 * @file eeprom.c
 * File-backed EEPROM (data/eeprom.bin). GoldenEye uses a 4 Kbit EEPROM
 * (64 blocks x 8 bytes = 512 bytes). PORT_EEPROM overrides the path —
 * the golden suite points it at a throwaway copy of a pinned fixture so
 * the player's real save can neither perturb a test run nor be clobbered
 * by one.
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

#define EEPROM_SIZE   512
#define EEPROM_BLOCK  8

static u8 sEeprom[EEPROM_SIZE];
static int sLoaded;

static const char *eepromPath(void)
{
    static char buf[1024];
    const char *p = getenv("PORT_EEPROM");
    return p != NULL ? p : portPathFile(buf, sizeof(buf), "eeprom.bin");
}

static void eepromEnsureLoaded(void)
{
    FILE *f;

    if (sLoaded) {
        return;
    }
    sLoaded = 1;
    memset(sEeprom, 0xFF, sizeof(sEeprom)); /* fresh EEPROM reads as 0xFF */
    f = fopen(eepromPath(), "rb");
    if (f != NULL) {
        fread(sEeprom, 1, sizeof(sEeprom), f);
        fclose(f);
    }
}

static void eepromFlush(void)
{
    FILE *f = fopen(eepromPath(), "wb");

    if (f != NULL) {
        fwrite(sEeprom, 1, sizeof(sEeprom), f);
        fclose(f);
    }
}

s32 osEepromProbe(OSMesgQueue *mq)
{
    return EEPROM_TYPE_4K;
}

s32 osEepromRead(OSMesgQueue *mq, u8 address, u8 *buffer)
{
    eepromEnsureLoaded();
    if ((u32)address * EEPROM_BLOCK >= EEPROM_SIZE) {
        return -1;
    }
    memcpy(buffer, &sEeprom[address * EEPROM_BLOCK], EEPROM_BLOCK);
    return 0;
}

s32 osEepromWrite(OSMesgQueue *mq, u8 address, u8 *buffer)
{
    eepromEnsureLoaded();
    if ((u32)address * EEPROM_BLOCK >= EEPROM_SIZE) {
        return -1;
    }
    memcpy(&sEeprom[address * EEPROM_BLOCK], buffer, EEPROM_BLOCK);
    eepromFlush();
    return 0;
}

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, s32 nbytes)
{
    eepromEnsureLoaded();
    if ((u32)address * EEPROM_BLOCK + nbytes > EEPROM_SIZE) {
        return -1;
    }
    memcpy(buffer, &sEeprom[address * EEPROM_BLOCK], nbytes);
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, s32 nbytes)
{
    eepromEnsureLoaded();
    if ((u32)address * EEPROM_BLOCK + nbytes > EEPROM_SIZE) {
        return -1;
    }
    memcpy(&sEeprom[address * EEPROM_BLOCK], buffer, nbytes);
    eepromFlush();
    return 0;
}
