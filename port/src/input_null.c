/**
 * @file input_null.c
 * Headless input backend: controller 0 present, nothing pressed.
 */
#include <string.h>
#include <ultra64.h>
#include "port.h"

void portInputInit(void) {}

void portInputRead(OSContPad *pads)
{
    memset(pads, 0, sizeof(OSContPad) * MAXCONTROLLERS);
}

int portMouselookGrabbed(void)
{
    return 0;
}

void portInputSetAimMode(s32 aiming)
{
    (void)aiming;
}

s32 portInputConsumeMouseLook(f32 *dtheta, f32 *dverta)
{
    (void)dtheta;
    (void)dverta;
    return 0;
}

s32 portInputConsumeWeaponScroll(void)
{
    return 0;
}

s32 portInputConsumeWeaponSelect(void)
{
    return 0;
}
