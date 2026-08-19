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
