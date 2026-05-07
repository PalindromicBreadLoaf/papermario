#include <SDL2/SDL.h>
#include <nu/nusys.h>

// Minimal display list: sync the RDP then end.
static Gfx sMinimalDL[] = {
    gsDPFullSync(),
    gsSPEndDisplayList(),
};

static void pc_retrace_cb(s32 taskNum) {
    (void)taskNum;
    nuGfxTaskStart(sMinimalDL, sizeof(sMinimalDL),
                   NU_GFX_UCODE_F3DEX, NU_SC_SWAPBUFFER);
}

/// Minimal boot_main stub used until the full game source is linked in.
void boot_main(void *arg) {
    (void)arg;
    nuGfxInit();
    nuGfxFuncSet((NUGfxFunc)pc_retrace_cb);
    nuGfxDisplayOn();
    while (1) { SDL_Delay(1000); }
}
