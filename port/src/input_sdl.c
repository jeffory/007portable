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
#include <sys/stat.h>
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

/* Direct aim (default on, PORT_DIRECT_AIM=0 falls back to the stick path):
 * mouse deltas accumulate here in degrees and bondview injects them
 * straight into the view angles — no virtual-stick translation, no turn
 * speed cap. Aim mode (R held) still routes the mouse through the stick so
 * the crosshair tracks it. */
#define DIRECT_AIM_DEG_PER_COUNT 0.05f

static int sDirectAim = -1;
static int sAimMode;
static float sLookAccumX;
static float sLookAccumY;

void portInputSetAimMode(s32 aiming)
{
    sAimMode = aiming != 0;
}

s32 portInputConsumeMouseLook(f32 *dtheta, f32 *dverta)
{
    if (sDirectAim <= 0 || sMouselook <= 0 || !sMouseGrabbed) {
        return 0;
    }
    *dtheta = sLookAccumX;
    *dverta = sLookAccumY;
    sLookAccumX = 0.0f;
    sLookAccumY = 0.0f;
    return 1;
}

/* ---- configurable keybindings ---------------------------------------------
 * Loaded from $PORT_INPUT_CONFIG, else $XDG_CONFIG_HOME/ge007/input.ini,
 * else ~/.config/ge007/input.ini. A commented default file is written on
 * first run. Format: "action = key[, key...]" — key names are SDL scancode
 * names ("W", "Left Shift", "Return", ...) or Mouse1..Mouse5. */

#define MAX_KEYS_PER_ACTION 4

enum {
    IN_FORWARD, IN_BACK, IN_LEFT, IN_RIGHT,       /* semantic move (style-aware) */
    IN_FIRE, IN_AIM,                              /* Z / R triggers */
    IN_A, IN_B, IN_START, IN_LSHOULDER,
    IN_C_UP, IN_C_DOWN, IN_C_LEFT, IN_C_RIGHT,    /* raw C buttons */
    IN_STICK_UP, IN_STICK_DOWN, IN_STICK_LEFT, IN_STICK_RIGHT,
    IN_GRAB_TOGGLE,
    IN_COUNT
};

static const char *const sActionNames[IN_COUNT] = {
    "forward", "back", "left", "right",
    "fire", "aim",
    "a", "b", "start", "l",
    "c_up", "c_down", "c_left", "c_right",
    "stick_up", "stick_down", "stick_left", "stick_right",
    "grab_toggle",
};

static const char *const sActionHelp[IN_COUNT] = {
    "move forward", "move back",
    "move left (strafe under mouselook, turn otherwise)",
    "move right",
    "fire (Z trigger)", "aim (R trigger)",
    "A button (accept / cycle weapon)",
    "B button (reload / open doors / activate)",
    "Start (pause)", "L shoulder button",
    "C up (raw)", "C down (raw)", "C left (raw)", "C right (raw)",
    "analog stick up (raw)", "analog stick down (raw)",
    "analog stick left (raw)", "analog stick right (raw)",
    "toggle the mouselook grab",
};

static const char *const sDefaultBindings[IN_COUNT] = {
    "W", "S", "A", "D",
    "Space, Mouse1", "E, Mouse2",
    "X, F", "C, R", "Return", "Q",
    "I", "K", "J", "L",
    "Up", "Down", "Left", "Right",
    "F1",
};

static struct {
    SDL_Scancode keys[MAX_KEYS_PER_ACTION];
    int numkeys;
    Uint32 mousemask;
} sBind[IN_COUNT];

static void bindParseAction(int action, const char *value)
{
    char buf[128];
    char *tok;
    char *save = NULL;

    sBind[action].numkeys = 0;
    sBind[action].mousemask = 0;

    snprintf(buf, sizeof(buf), "%s", value);

    for (tok = strtok_r(buf, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
        char *end;
        SDL_Scancode sc;

        while (*tok == ' ' || *tok == '\t') tok++;
        end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        if (*tok == '\0') continue;

        if (SDL_strncasecmp(tok, "mouse", 5) == 0 && tok[5] >= '1' && tok[5] <= '5' && tok[6] == '\0') {
            sBind[action].mousemask |= SDL_BUTTON(tok[5] - '0');
            continue;
        }

        sc = SDL_GetScancodeFromName(tok);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "port/input: unknown key name '%s' for %s (see SDL scancode names)\n",
                    tok, sActionNames[action]);
            continue;
        }
        if (sBind[action].numkeys < MAX_KEYS_PER_ACTION) {
            sBind[action].keys[sBind[action].numkeys++] = sc;
        }
    }
}

static const char *bindConfigPath(char *buf, size_t len)
{
    const char *e = getenv("PORT_INPUT_CONFIG");
    const char *xdg;

    if (e != NULL && e[0] != '\0') {
        snprintf(buf, len, "%s", e);
        return buf;
    }
    xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(buf, len, "%s/ge007/input.ini", xdg);
    } else {
        const char *home = getenv("HOME");
        snprintf(buf, len, "%s/.config/ge007/input.ini", home != NULL ? home : ".");
    }
    return buf;
}

static void bindWriteDefaultFile(const char *path)
{
    char dir[512];
    char *slash;
    FILE *f;
    int i;

    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = '\0';
        /* best-effort mkdir -p for the last two levels */
        char parent[512];
        snprintf(parent, sizeof(parent), "%s", dir);
        slash = strrchr(parent, '/');
        if (slash != NULL) { *slash = '\0'; mkdir(parent, 0755); }
        mkdir(dir, 0755);
    }

    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "# GoldenEye 007 PC port — input bindings\n");
    fprintf(f, "# action = key[, key...]   keys are SDL scancode names (\"W\", \"Left Shift\",\n");
    fprintf(f, "# \"Return\", \"Keypad 5\", ...) or Mouse1..Mouse5. Up to %d per action.\n", MAX_KEYS_PER_ACTION);
    fprintf(f, "# Delete this file to regenerate the defaults.\n\n");
    for (i = 0; i < IN_COUNT; i++) {
        fprintf(f, "# %s\n%s = %s\n\n", sActionHelp[i], sActionNames[i], sDefaultBindings[i]);
    }
    fprintf(f, "# mouselook (PORT_MOUSE_SENS / PORT_MOUSE_INVERT env vars override)\n");
    fprintf(f, "sensitivity = 2.0\ninvert = 0\n");
    fclose(f);
    fprintf(stderr, "port/input: wrote default bindings to %s\n", path);
}

static void bindLoad(void)
{
    char path[512];
    FILE *f;
    char line[256];
    int i;

    for (i = 0; i < IN_COUNT; i++) {
        bindParseAction(i, sDefaultBindings[i]);
    }

    bindConfigPath(path, sizeof(path));
    f = fopen(path, "r");
    if (f == NULL) {
        bindWriteDefaultFile(path);
        return;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq;
        char *name = line;
        char *val;
        char *end;

        while (*name == ' ' || *name == '\t') name++;
        if (*name == '#' || *name == ';' || *name == '\n' || *name == '\0' || *name == '[') continue;
        eq = strchr(name, '=');
        if (eq == NULL) continue;
        val = eq + 1;
        while (eq > name && (eq[-1] == ' ' || eq[-1] == '\t')) eq--;
        *eq = '\0';
        while (*val == ' ' || *val == '\t') val++;
        end = val + strlen(val);
        while (end > val && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *--end = '\0';

        if (strcmp(name, "sensitivity") == 0) {
            if (atof(val) > 0.0) sMouseSens = (float)atof(val);
            continue;
        }
        if (strcmp(name, "invert") == 0) {
            sMouseInvert = atoi(val) != 0;
            continue;
        }
        for (i = 0; i < IN_COUNT; i++) {
            if (strcmp(name, sActionNames[i]) == 0) {
                bindParseAction(i, val);
                break;
            }
        }
        if (i == IN_COUNT) {
            fprintf(stderr, "port/input: unknown action '%s' in %s\n", name, path);
        }
    }
    fclose(f);
    fprintf(stderr, "port/input: bindings loaded from %s\n", path);
}

static int actionDown(const Uint8 *keys, Uint32 mousemask, int action)
{
    int i;

    if (keys != NULL) {
        for (i = 0; i < sBind[action].numkeys; i++) {
            if (keys[sBind[action].keys[i]]) {
                return 1;
            }
        }
    }
    return (sBind[action].mousemask & mousemask) != 0;
}

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

    bindLoad(); /* keybindings + mouse settings; env vars below override */

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
        if (getenv("PORT_MOUSE_INVERT") != NULL) {
            sMouseInvert = 1;
        }
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

    /* ---- keyboard + mouse buttons (configurable bindings) ------------- */
    {
        int dx = 0, dy = 0;
        Uint32 mb = 0;

        if (sMouselook > 0) {
            mb = SDL_GetRelativeMouseState(&dx, &dy);

            /* grab toggle (edge-triggered) */
            {
                static int prevToggle;
                int t = actionDown(keys, 0, IN_GRAB_TOGGLE);

                if (t && !prevToggle) {
                    mouselookSetGrab(!sMouseGrabbed);
                }
                prevToggle = t;
            }
        }
        if (!sMouseGrabbed) {
            mb = 0; /* mouse buttons only act while the mouse is captured */
        }

        /* raw stick / buttons */
        if (actionDown(keys, mb, IN_STICK_LEFT))  x -= STICK_MAX;
        if (actionDown(keys, mb, IN_STICK_RIGHT)) x += STICK_MAX;
        if (actionDown(keys, mb, IN_STICK_DOWN))  y -= STICK_MAX;
        if (actionDown(keys, mb, IN_STICK_UP))    y += STICK_MAX;

        if (actionDown(keys, mb, IN_START))     buttons |= START_BUTTON;
        if (actionDown(keys, mb, IN_A))         buttons |= A_BUTTON;
        if (actionDown(keys, mb, IN_B))         buttons |= B_BUTTON;
        if (actionDown(keys, mb, IN_FIRE))      buttons |= Z_TRIG;
        if (actionDown(keys, mb, IN_AIM))       buttons |= R_TRIG;
        if (actionDown(keys, mb, IN_LSHOULDER)) buttons |= L_TRIG;
        if (actionDown(keys, mb, IN_C_UP))      buttons |= U_CBUTTONS;
        if (actionDown(keys, mb, IN_C_DOWN))    buttons |= D_CBUTTONS;
        if (actionDown(keys, mb, IN_C_LEFT))    buttons |= L_CBUTTONS;
        if (actionDown(keys, mb, IN_C_RIGHT))   buttons |= R_CBUTTONS;

        /* forward/back/left/right always mean "move", whatever moves in the
         * active style:
         * - mouselook grabbed: the port forces style 1.2 Solitaire, where
         *   the C buttons move (the stick is the mouse's look axis)
         * - otherwise: the game defaults to 1.1 Honey, where the analog
         *   stick moves/turns (tank-style, like the N64 original) and the
         *   C buttons would be look/strafe */
        if (sMouselook > 0 && sMouseGrabbed) {
            if (actionDown(keys, mb, IN_FORWARD)) buttons |= U_CBUTTONS;
            if (actionDown(keys, mb, IN_BACK))    buttons |= D_CBUTTONS;
            if (actionDown(keys, mb, IN_LEFT))    buttons |= L_CBUTTONS;
            if (actionDown(keys, mb, IN_RIGHT))   buttons |= R_CBUTTONS;
        } else {
            if (actionDown(keys, mb, IN_FORWARD)) y += STICK_MAX;
            if (actionDown(keys, mb, IN_BACK))    y -= STICK_MAX;
            if (actionDown(keys, mb, IN_LEFT))    x -= STICK_MAX;
            if (actionDown(keys, mb, IN_RIGHT))   x += STICK_MAX;
        }

        if (sMouseGrabbed) {
            if (sDirectAim < 0) {
                const char *e = getenv("PORT_DIRECT_AIM");
                sDirectAim = (e == NULL) || (e[0] != '0');
            }
            if (sDirectAim > 0 && !sAimMode) {
                /* accumulate in degrees for the bondview direct injection;
                 * dy > 0 (mouse down) pitches down = vv_verta decreases */
                sLookAccumX += (f32)dx * sMouseSens * DIRECT_AIM_DEG_PER_COUNT;
                sLookAccumY += (f32)(sMouseInvert ? -dy : dy) * sMouseSens * DIRECT_AIM_DEG_PER_COUNT;
            } else {
                /* virtual-stick path: dx > 0 (mouse right) turns right =
                 * +stick_x; dy > 0 (mouse down) pitches down: with GE's
                 * default (flight-style) pitch, stick +Y looks down. */
                x += (s32)((f32)dx * sMouseSens);
                y += (s32)((f32)(sMouseInvert ? -dy : dy) * sMouseSens);
            }
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
