#include <stdbool.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "asset_loader.h"
#include "gbi_interpreter.h"
#include "gl_backend.h"

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
    gl_backend_init("Paper Mario", 640, 480);
    gbi_init();
    gfx_dump_dl = dump_dl;

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        gl_backend_start_frame();
        //TODO: Game logic
        gl_backend_end_frame();
    }

    gl_backend_shutdown();
    asset_loader_shutdown();
    return 0;
}
