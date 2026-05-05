#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_message.h>
#include <PR/os_vi.h>
#include "pc_window.h"

#define PC_WINDOW_TITLE  "Paper Mario"
#define PC_WIN_W         640
#define PC_WIN_H         480

SDL_Window   *gPcWindow   = NULL;
SDL_GLContext gPcGlContext = NULL;

// N64 video clock (NTSC DAC rate). Referenced by the audio system.
s32 osViClock = 48681812;

// Dummy VI mode table. All entries are zero. On PC the mode only controls
// which SDL window size we open: a fixed 640x480 window.
// TODO: Support resizing windows and higher resolutions
OSViMode osViModeTable[56];

// osViSetMode receives a pointer to one of these; on PC this is ignored.
OSViMode osViModeNtscLpn1, osViModeNtscLpf1, osViModeNtscLan1, osViModeNtscLaf1;
OSViMode osViModeNtscLpn2, osViModeNtscLpf2, osViModeNtscLan2, osViModeNtscLaf2;
OSViMode osViModeNtscHpn1, osViModeNtscHpf1, osViModeNtscHan1, osViModeNtscHaf1;
OSViMode osViModeNtscHpn2, osViModeNtscHpf2;
OSViMode osViModePalLpn1,  osViModePalLpf1,  osViModePalLan1,  osViModePalLaf1;
OSViMode osViModePalLpn2,  osViModePalLpf2,  osViModePalLan2,  osViModePalLaf2;
OSViMode osViModePalHpn1,  osViModePalHpf1,  osViModePalHan1,  osViModePalHaf1;
OSViMode osViModePalHpn2,  osViModePalHpf2;
OSViMode osViModeMpalLpn1, osViModeMpalLpf1, osViModeMpalLan1, osViModeMpalLaf1;
OSViMode osViModeMpalLpn2, osViModeMpalLpf2, osViModeMpalLan2, osViModeMpalLaf2;
OSViMode osViModeMpalHpn1, osViModeMpalHpf1, osViModeMpalHan1, osViModeMpalHaf1;
OSViMode osViModeMpalHpn2, osViModeMpalHpf2;
OSViMode osViModeFpalLpn1, osViModeFpalLpf1, osViModeFpalLan1, osViModeFpalLaf1;
OSViMode osViModeFpalLpn2, osViModeFpalLpf2, osViModeFpalLan2, osViModeFpalLaf2;
OSViMode osViModeFpalHpn1, osViModeFpalHpf1, osViModeFpalHan1, osViModeFpalHaf1;
OSViMode osViModeFpalHpn2, osViModeFpalHpf2;

// Framebuffer pointers tracked across osViSwapBuffer calls.
static void *sCurrentFb = NULL;
static void *sNextFb    = NULL;

static void create_window(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    gPcWindow = SDL_CreateWindow(
        PC_WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        PC_WIN_W, PC_WIN_H,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    gPcGlContext = SDL_GL_CreateContext(gPcWindow);
    SDL_GL_SetSwapInterval(1);
}

void osViSetMode(OSViMode *mode) {
    (void)mode;
    if (gPcWindow == NULL) {
        create_window();
    }
}

void osViSetSpecialFeatures(u32 func) { (void)func; }
void osViSetXScale(f32 scale)         { (void)scale; }
void osViSetYScale(f32 scale)         { (void)scale; }
void osViExtendVStart(u32 v)          { (void)v; }
void osViRepeatLine(u8 flag)          { (void)flag; }
void osViFade(u8 flag, u16 value)     { (void)flag; (void)value; }
void osCreateViManager(OSPri pri)     { (void)pri; }

void osViBlack(u8 active) {
    (void)active;
    // The GBI renderer handles screen clearing.
}

void osViSwapBuffer(void *fb) {
    sCurrentFb = sNextFb;
    sNextFb    = fb;
    if (gPcWindow) {
        SDL_GL_SwapWindow(gPcWindow);
    }
}

void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount) {
    // VI retrace events are delivered by the NuSystem scheduler stub.
    (void)mq;
    (void)msg;
    (void)retraceCount;
}

void *osViGetCurrentFramebuffer(void) { return sCurrentFb; }
void *osViGetNextFramebuffer(void)    { return sNextFb; }

u32  osViGetStatus(void)      { return 0; }
u32  osViGetCurrentMode(void) { return 0; }
u32  osViGetCurrentLine(void) { return 0; }
u32  osViGetCurrentField(void){ return 0; }
