/**
 * @file input_sdl.c
 * SDL input backend (M5): keyboard + SDL_GameController + optional mouselook.
 *
 * Keyboard:
 *   arrows      = stick        Enter = Start     X = A     C = B
 *   IJKL / WASD = C buttons    Q/E   = L/R       Space = Z
 *   (WASD is meant for control style 1.2 "Solitaire", where the C buttons
 *    move and the stick turns/looks — the natural style for mouselook.)
 *
 * Gamepad (SDL_GameController, hotplug supported):
 *   left stick  = N64 stick            right stick = C buttons
 *   A/B         = A/B (Y=A, X=B too)   Start = Start
 *   RT          = Z (fire)             LT    = R (aim)
 *   LB/RB       = L/R                  Back  = L
 *   D-pad       = D-pad (moves in 1.2, menus)
 *
 * Mouselook (default ON; PORT_MOUSELOOK=0 disables):
 *   relative mouse motion drives stick X (turn) / stick Y (look) the way
 *   control style 1.2 expects (the port forces 1.2 while grabbed, so WASD
 *   moves and the mouse looks); LMB = Z (fire), RMB = R (aim).
 *   PORT_MOUSE_SENS=<float> (default 2.0), PORT_MOUSE_INVERT=1 flips Y.
 *   F1 toggles the mouse grab.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include "port.h"

#define STICK_MAX      70            /* keyboard parity; MoveBond rejects |x| >= 100 */
#define AXIS_DEADZONE  6000          /* of 32767 */
#define CBTN_THRESHOLD 16384         /* right stick -> C button trip point */
#define TRIG_THRESHOLD 8000          /* analog trigger -> button trip point */

static SDL_GameController *sPad;

static int sMouselook = -1;          /* -1 = env not read yet */
static int sMouseGrabbed;
static int sMouseInvert;
static float sMouseSens = 2.0f;

static s32 clampStick(s32 v)
{
    if (v > STICK_MAX) return STICK_MAX;
    if (v < -STICK_MAX) return -STICK_MAX;
    return v;
}

/* map one analog axis to the stick range with a deadzone */
static s32 axisToStick(Sint16 v)
{
    s32 mag = (v < 0) ? -(s32)v : (s32)v;

    if (mag <= AXIS_DEADZONE) {
        return 0;
    }
    mag = (mag - AXIS_DEADZONE) * STICK_MAX / (32767 - AXIS_DEADZONE);
    return (v < 0) ? -mag : mag;
}

static void padScan(void)
{
    int i;
    int n = SDL_NumJoysticks();

    for (i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            sPad = SDL_GameControllerOpen(i);
            if (sPad != NULL) {
                fprintf(stderr, "port/input: gamepad connected: %s\n",
                        SDL_GameControllerName(sPad));
                return;
            }
        }
    }
}

int portMouselookGrabbed(void)
{
    return sMouselook > 0 && sMouseGrabbed;
}

static void mouselookSetGrab(int on)
{
    sMouseGrabbed = on;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    /* drop the pending relative delta so a re-grab doesn't jerk the view */
    SDL_GetRelativeMouseState(NULL, NULL);
}

void portInputInit(void)
{
    /* PORT_NO_GAMEPAD: skip controller enumeration entirely. Useful in
     * sandboxed/headless environments where the SDL3-backed sdl2-compat's
     * udev device scan can crash (masked /run/udev). Keyboard still works. */
    if (getenv("PORT_NO_GAMEPAD") != NULL) {
        fprintf(stderr, "port/input: PORT_NO_GAMEPAD set; keyboard only\n");
    } else if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "port/input: SDL gamecontroller init failed: %s\n", SDL_GetError());
    } else {
        padScan();
    }

    {
        /* Mouselook defaults ON (PORT_MOUSELOOK=0 disables): the mouse
         * drives the stick (turn/look), WASD moves via the forced 1.2
         * control style, LMB fires, RMB aims. */
        const char *e = getenv("PORT_MOUSELOOK");

        sMouselook = (e == NULL) || (e[0] != '0');
    }
    if (sMouselook) {
        const char *sens = getenv("PORT_MOUSE_SENS");

        if (sens != NULL && atof(sens) > 0.0) {
            sMouseSens = (float)atof(sens);
        }
        sMouseInvert = getenv("PORT_MOUSE_INVERT") != NULL;
        mouselookSetGrab(1);
        fprintf(stderr, "port/input: mouselook on (sens %.2f%s) — use control style 1.2, F1 releases the mouse\n",
                sMouseSens, sMouseInvert ? ", inverted" : "");
    }
}

void portInputRead(OSContPad *pads)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    u16 buttons = 0;
    s32 x = 0;
    s32 y = 0;

    memset(pads, 0, sizeof(OSContPad) * MAXCONTROLLERS);

    /* PORT_AUTOSTART: headless testing aid — synthesize START presses at
     * fixed poll counts to drive through intro/menus deterministically */
    {
        static int autostart = -1;
        static u32 polls;

        if (autostart < 0) {
            autostart = getenv("PORT_AUTOSTART") != NULL;
        }
        polls++;
        if (autostart &&
            ((polls >= 600 && polls < 605) ||
             (polls >= 1500 && polls < 1505) ||
             (polls >= 3000 && polls < 3005) ||
             (polls >= 4500 && polls < 4505) ||
             (polls >= 6000 && polls < 6005))) {
            pads[0].button = START_BUTTON;
            return;
        }
    }

    /* PORT_TEST_VPAD: headless testing aid — attach an SDL virtual gamepad
     * mid-run (hotplug path), drive a canned input script through it, then
     * detach it (disconnect path). Verifiable via gdb/screenshots. */
#if SDL_VERSION_ATLEAST(2, 0, 14)
    {
        static int vtest = -1;
        static int vdev = -1;
        static u32 vpolls;

        if (vtest < 0) {
            vtest = getenv("PORT_TEST_VPAD") != NULL;
        }
        if (vtest) {
            vpolls++;
            if (vpolls == 120) { /* ~2s in: hotplug */
                vdev = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                                 SDL_CONTROLLER_AXIS_MAX,
                                                 SDL_CONTROLLER_BUTTON_MAX, 0);
                fprintf(stderr, "port/input: vpad attached (dev %d)\n", vdev);
            } else if (vpolls == 3300 && vdev >= 0) { /* ~55s in: unplug */
                SDL_JoystickDetachVirtual(vdev);
                vdev = -1;
                fprintf(stderr, "port/input: vpad detached\n");
            } else if (sPad != NULL && vdev >= 0) {
                SDL_Joystick *vj = SDL_GameControllerGetJoystick(sPad);

                if (vj != NULL) {
                    /* script (timed past stage load): turn right 30..35s,
                     * fire (RT) 35..37s, C-forward via right stick 37..42s */
                    Sint16 lx = (vpolls >= 1800 && vpolls < 2100) ? 32767 : 0;
                    Sint16 rt = (vpolls >= 2100 && vpolls < 2220) ? 32767 : 0;
                    Sint16 ry = (vpolls >= 2220 && vpolls < 2520) ? -32767 : 0;

                    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_LEFTX, lx);
                    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, rt);
                    SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_RIGHTY, ry);
                }
            }
        }
    }
#endif

    /* ---- gamepad (with hotplug) ------------------------------------- */
    SDL_GameControllerUpdate();

    if (sPad != NULL && !SDL_GameControllerGetAttached(sPad)) {
        fprintf(stderr, "port/input: gamepad disconnected\n");
        SDL_GameControllerClose(sPad);
        sPad = NULL;
    }
    if (sPad == NULL) {
        static u32 scanTick;

        if ((scanTick++ % 30) == 0) { /* re-scan every half second */
            padScan();
        }
    }

    if (sPad != NULL) {
        Sint16 rx = SDL_GameControllerGetAxis(sPad, SDL_CONTROLLER_AXIS_RIGHTX);
        Sint16 ry = SDL_GameControllerGetAxis(sPad, SDL_CONTROLLER_AXIS_RIGHTY);

        x += axisToStick(SDL_GameControllerGetAxis(sPad, SDL_CONTROLLER_AXIS_LEFTX));
        y -= axisToStick(SDL_GameControllerGetAxis(sPad, SDL_CONTROLLER_AXIS_LEFTY)); /* SDL +Y is down */

        if (rx >  CBTN_THRESHOLD) buttons |= R_CBUTTONS;
        if (rx < -CBTN_THRESHOLD) buttons |= L_CBUTTONS;
        if (ry < -CBTN_THRESHOLD) buttons |= U_CBUTTONS;
        if (ry >  CBTN_THRESHOLD) buttons |= D_CBUTTONS;

        if (SDL_GameControllerGetAxis(sPad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIG_THRESHOLD) buttons |= Z_TRIG;
        if (SDL_GameControllerGetAxis(sPad, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > TRIG_THRESHOLD) buttons |= R_TRIG;

        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_A)) buttons |= A_BUTTON;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_Y)) buttons |= A_BUTTON;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_B)) buttons |= B_BUTTON;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_X)) buttons |= B_BUTTON;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_START)) buttons |= START_BUTTON;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_BACK)) buttons |= L_TRIG;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  buttons |= L_TRIG;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) buttons |= R_TRIG;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_DPAD_UP))    buttons |= U_JPAD;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  buttons |= D_JPAD;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  buttons |= L_JPAD;
        if (SDL_GameControllerGetButton(sPad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) buttons |= R_JPAD;
    }

    /* ---- keyboard ---------------------------------------------------- */
    if (keys != NULL) {
        if (keys[SDL_SCANCODE_LEFT])  x -= STICK_MAX;
        if (keys[SDL_SCANCODE_RIGHT]) x += STICK_MAX;
        if (keys[SDL_SCANCODE_DOWN])  y -= STICK_MAX;
        if (keys[SDL_SCANCODE_UP])    y += STICK_MAX;

        if (keys[SDL_SCANCODE_RETURN]) buttons |= START_BUTTON;
        if (keys[SDL_SCANCODE_X])      buttons |= A_BUTTON;
        if (keys[SDL_SCANCODE_C])      buttons |= B_BUTTON;
        /* GE reloads (and opens doors) with B; R is where PC hands expect it */
        if (keys[SDL_SCANCODE_R])      buttons |= B_BUTTON;
        /* F = A (weapon cycle / accept) for the same reason */
        if (keys[SDL_SCANCODE_F])      buttons |= A_BUTTON;
        if (keys[SDL_SCANCODE_SPACE])  buttons |= Z_TRIG;
        if (keys[SDL_SCANCODE_Q])      buttons |= L_TRIG;
        if (keys[SDL_SCANCODE_E])      buttons |= R_TRIG;
        if (keys[SDL_SCANCODE_I])      buttons |= U_CBUTTONS;
        if (keys[SDL_SCANCODE_K])      buttons |= D_CBUTTONS;
        if (keys[SDL_SCANCODE_J])      buttons |= L_CBUTTONS;
        if (keys[SDL_SCANCODE_L])      buttons |= R_CBUTTONS;
        /* WASD always means "move", whatever moves in the active style:
         * - mouselook grabbed: the port forces style 1.2 Solitaire, where
         *   the C buttons move (the stick is the mouse's look axis)
         * - otherwise: the game defaults to 1.1 Honey, where the analog
         *   stick moves/turns (tank-style, like the N64 original) and the
         *   C buttons would be look/strafe */
        if (sMouselook > 0 && sMouseGrabbed) {
            if (keys[SDL_SCANCODE_W])  buttons |= U_CBUTTONS;
            if (keys[SDL_SCANCODE_S])  buttons |= D_CBUTTONS;
            if (keys[SDL_SCANCODE_A])  buttons |= L_CBUTTONS;
            if (keys[SDL_SCANCODE_D])  buttons |= R_CBUTTONS;
        } else {
            if (keys[SDL_SCANCODE_W])  y += STICK_MAX;
            if (keys[SDL_SCANCODE_S])  y -= STICK_MAX;
            if (keys[SDL_SCANCODE_A])  x -= STICK_MAX;
            if (keys[SDL_SCANCODE_D])  x += STICK_MAX;
        }
    }

    /* ---- mouselook ---------------------------------------------------- */
    if (sMouselook > 0) {
        int dx, dy;
        Uint32 mb = SDL_GetRelativeMouseState(&dx, &dy);

        /* F1 toggles the grab (edge-triggered) */
        {
            static int prevF1;
            int f1 = keys != NULL && keys[SDL_SCANCODE_F1];

            if (f1 && !prevF1) {
                mouselookSetGrab(!sMouseGrabbed);
            }
            prevF1 = f1;
        }

        if (sMouseGrabbed) {
            /* dx > 0 (mouse right) turns right: +stick_x.
             * dy > 0 (mouse down) must pitch down: with GE's default
             * (flight-style) pitch, stick +Y looks down, so +stick_y. */
            x += (s32)((f32)dx * sMouseSens);
            y += (s32)((f32)(sMouseInvert ? -dy : dy) * sMouseSens);

            if (mb & SDL_BUTTON_LMASK) buttons |= Z_TRIG; /* fire */
            if (mb & SDL_BUTTON_RMASK) buttons |= R_TRIG; /* aim */
        }
    }

    pads[0].button = buttons;
    pads[0].stick_x = (s8)clampStick(x);
    pads[0].stick_y = (s8)clampStick(y);
    pads[0].errno = 0;

    /* PORT_INPUT_TRACE=1: once a second, log every pressed key (scancode +
     * name) and the resulting pad state — for diagnosing per-machine
     * keyboard weirdness (compositor eating keys, stale binaries, ...) */
    {
        static int trace = -1;
        static u32 traceTick;

        if (trace < 0) {
            trace = getenv("PORT_INPUT_TRACE") != NULL;
        }
        if (trace && (traceTick++ % 60) == 0 && keys != NULL) {
            int sc;
            int any = 0;

            fprintf(stderr, "port/input: keys:");
            for (sc = 0; sc < SDL_NUM_SCANCODES; sc++) {
                if (keys[sc]) {
                    fprintf(stderr, " %d(%s)", sc, SDL_GetScancodeName((SDL_Scancode)sc));
                    any = 1;
                }
            }
            if (!any) {
                fprintf(stderr, " none");
            }
            fprintf(stderr, " -> buttons=%04x stick=%d,%d grab=%d\n",
                    buttons, pads[0].stick_x, pads[0].stick_y, sMouseGrabbed);
        }
    }
}
