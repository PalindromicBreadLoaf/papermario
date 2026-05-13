#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_cont.h>
#include <PR/os_message.h>
#include "pc_cont.h"

static SDL_GameController *sControllers[MAXCONTROLLERS];
static SDL_SpinLock sPadLock;
static OSContPad sCurrentPads[MAXCONTROLLERS];

static const struct { SDL_GameControllerButton btn; u16 mask; } kButtonMap[] = {
    { SDL_CONTROLLER_BUTTON_A,             CONT_A     },
    { SDL_CONTROLLER_BUTTON_B,             CONT_B     },
    { SDL_CONTROLLER_BUTTON_X,             CONT_G     },   // Z trigger
    { SDL_CONTROLLER_BUTTON_START,         CONT_START },
    { SDL_CONTROLLER_BUTTON_DPAD_UP,       CONT_UP    },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     CONT_DOWN  },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     CONT_LEFT  },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    CONT_RIGHT },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  CONT_L     },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, CONT_R     },
};

static const struct { SDL_Scancode key; u16 mask; } kKeyMap[] = {
    { SDL_SCANCODE_Z,      CONT_A     },
    { SDL_SCANCODE_X,      CONT_B     },
    { SDL_SCANCODE_C,      CONT_G     },   // Z trigger
    { SDL_SCANCODE_RETURN, CONT_START },
    { SDL_SCANCODE_LSHIFT, CONT_L     },
    { SDL_SCANCODE_RSHIFT, CONT_R     },
    { SDL_SCANCODE_I,      CONT_E     },   // C-Up
    { SDL_SCANCODE_K,      CONT_D     },   // C-Down
    { SDL_SCANCODE_J,      CONT_C     },   // C-Left
    { SDL_SCANCODE_L,      CONT_F     },   // C-Right
};

#define CSTICK_THRESHOLD 16000
#define KBD_STICK_VAL    70

static s8 clamp_stick(Sint32 v) {
    if (v >  80) return  80;
    if (v < -80) return -80;
    return (s8)v;
}

static void fill_from_gamepad(int i, OSContPad *pad) {
    SDL_GameController *gc = sControllers[i];
    SDL_memset(pad, 0, sizeof *pad);
    if (!gc) return;

    for (int j = 0; j < (int)SDL_arraysize(kButtonMap); j++) {
        if (SDL_GameControllerGetButton(gc, kButtonMap[j].btn))
            pad->button |= kButtonMap[j].mask;
    }

    Sint16 rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
    Sint16 ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);
    if (rx >  CSTICK_THRESHOLD) pad->button |= CONT_F;   // C-Right
    if (rx < -CSTICK_THRESHOLD) pad->button |= CONT_C;   // C-Left
    if (ry < -CSTICK_THRESHOLD) pad->button |= CONT_E;   // C-Up
    if (ry >  CSTICK_THRESHOLD) pad->button |= CONT_D;   // C-Down

    Sint16 lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    Sint16 ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    pad->stick_x = clamp_stick(lx * 80 / 32767);
    pad->stick_y = clamp_stick(-ly * 80 / 32767);
}

static void overlay_keyboard(OSContPad *pad) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    for (int i = 0; i < (int)SDL_arraysize(kKeyMap); i++) {
        if (keys[kKeyMap[i].key])
            pad->button |= kKeyMap[i].mask;
    }

    if (pad->stick_x == 0 && pad->stick_y == 0) {
        if (keys[SDL_SCANCODE_LEFT])  pad->stick_x = -KBD_STICK_VAL;
        if (keys[SDL_SCANCODE_RIGHT]) pad->stick_x =  KBD_STICK_VAL;
        if (keys[SDL_SCANCODE_UP])    pad->stick_y =  KBD_STICK_VAL;
        if (keys[SDL_SCANCODE_DOWN])  pad->stick_y = -KBD_STICK_VAL;
    }
}

void pc_cont_update(void) {
    OSContPad pads[MAXCONTROLLERS];

    for (int i = 0; i < MAXCONTROLLERS; i++) {
        fill_from_gamepad(i, &pads[i]);
    }
    overlay_keyboard(&pads[0]);

    SDL_AtomicLock(&sPadLock);
    for (int i = 0; i < MAXCONTROLLERS; i++) {
        sCurrentPads[i] = pads[i];
    }
    SDL_AtomicUnlock(&sPadLock);
}

void pc_cont_poll(OSContPad *pads, int count) {
    if (count > MAXCONTROLLERS) {
        count = MAXCONTROLLERS;
    }

    SDL_AtomicLock(&sPadLock);
    for (int i = 0; i < count; i++) {
        pads[i] = sCurrentPads[i];
    }
    SDL_AtomicUnlock(&sPadLock);
}

static OSContStatus sLastStatus[MAXCONTROLLERS];
static OSContPad    sPadState[MAXCONTROLLERS];

s32 osContInit(OSMesgQueue *mq, u8 *pattern, OSContStatus *status) {
    *pattern = 0;
    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < MAXCONTROLLERS; i++) {
        if (i < numJoysticks && SDL_IsGameController(i))
            sControllers[i] = SDL_GameControllerOpen(i);

        if (sControllers[i]) {
            status[i].type   = CONT_TYPE_NORMAL;
            status[i].status = 0;
            status[i].errno  = 0;
            *pattern |= (u8)(1 << i);
        } else {
            status[i].type   = 0;
            status[i].status = 0;
            status[i].errno  = 1;   // No controller
        }
        sLastStatus[i] = status[i];
    }
    // Always expose channel 0 so keyboard-only play works.
    if (!(*pattern & 1)) {
        *pattern |= 1;
        status[0].type   = CONT_TYPE_NORMAL;
        status[0].status = 0;
        status[0].errno  = 0;
        sLastStatus[0]   = status[0];
    }
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

s32 osContReset(OSMesgQueue *mq, OSContStatus *status) {
    for (int i = 0; i < MAXCONTROLLERS; i++)
        status[i] = sLastStatus[i];
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

s32 osContStartQuery(OSMesgQueue *mq) {
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

void osContGetQuery(OSContStatus *status) {
    for (int i = 0; i < MAXCONTROLLERS; i++)
        status[i] = sLastStatus[i];
}

s32 osContStartReadData(OSMesgQueue *mq) {
    pc_cont_poll(sPadState, MAXCONTROLLERS);
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

void osContGetReadData(OSContPad *pad) {
    for (int i = 0; i < MAXCONTROLLERS; i++)
        pad[i] = sPadState[i];
}

s32 osContSetCh(u8 chnls) { (void)chnls; return 0; }
