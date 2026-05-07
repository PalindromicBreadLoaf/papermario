#include <stdbool.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "asset_loader.h"
#include "audio_pc.h"
#include "gbi_interpreter.h"
#include "gl_backend.h"

extern void nuBoot(void);
extern void nu_audio_init(void);

#define DEFAULT_ROM_PATH "ver/us/baserom.z64"

int main(int argc, char *argv[]) {
    const char *rom_path = DEFAULT_ROM_PATH;
    bool dump_dl = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-dl") == 0) {
            dump_dl = true;
        } else {
            rom_path = argv[i];
        }
    }

    asset_loader_init(rom_path);
    audio_pc_init();
    nu_audio_init();

    // Creates the SDL window and OpenGL 3.3 context, then releases the context
    // from this thread so the NuSystem gfx thread can claim it.
    gl_backend_init("Paper Mario", 640, 480);
    gbi_init();
    gfx_dump_dl = dump_dl;

    // Start the boot chain: idle thread → scheduler → game thread.
    // All rendering happens via nuGfxTaskStart in the NuSystem gfx thread.
    nuBoot();

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        SDL_Delay(4);
    }

    audio_pc_shutdown();
    asset_loader_shutdown();
    SDL_Quit();
    return 0;
}
