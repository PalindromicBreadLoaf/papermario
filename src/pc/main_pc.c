#include <stdbool.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "asset_loader.h"
#include "audio_pc.h"
#include "gbi_interpreter.h"
#include "gl_backend.h"
#include "pc_cont.h"

extern void nuBoot(void);

#define DEFAULT_ROM_PATH "ver/us/baserom.z64"

#if defined(__unix__) || defined(__APPLE__)
#include <ucontext.h>

#if defined(__linux__) && defined(__x86_64__) && !defined(REG_RIP)
#define REG_RIP 16
#endif

static uintptr_t pc_signal_pc(void *context) {
    ucontext_t *ucontext = (ucontext_t *)context;

#if defined(__linux__) && defined(__x86_64__)
    return (uintptr_t)ucontext->uc_mcontext.gregs[REG_RIP];
#elif defined(__linux__) && defined(__aarch64__)
    return (uintptr_t)ucontext->uc_mcontext.pc;
#elif defined(__APPLE__) && defined(__x86_64__)
    return (uintptr_t)ucontext->uc_mcontext->__ss.__rip;
#elif defined(__APPLE__) && defined(__aarch64__)
    return (uintptr_t)ucontext->uc_mcontext->__ss.__pc;
#else
    (void)ucontext;
    return 0;
#endif
}

static void pc_signal_handler(int sig, siginfo_t *info, void *context) {
    fprintf(stderr, "papermario: fatal signal %d at address %p", sig, info ? info->si_addr : NULL);

    uintptr_t pc = pc_signal_pc(context);
    if (pc != 0) {
        fprintf(stderr, " (pc=%p)", (void *)pc);
    }

    fputc('\n', stderr);
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void pc_install_signal_handlers(void) {
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = pc_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;

    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGILL, &action, NULL);
    sigaction(SIGFPE, &action, NULL);
}
#else
static void pc_install_signal_handlers(void) {
}
#endif

int main(int argc, char *argv[]) {
    const char *rom_path = DEFAULT_ROM_PATH;
    bool dump_dl = false;

    pc_install_signal_handlers();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-dl") == 0) {
            dump_dl = true;
        } else {
            rom_path = argv[i];
        }
    }

    asset_loader_init(rom_path);
    audio_pc_init();

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
        pc_cont_update();
        SDL_Delay(4);
    }

    audio_pc_shutdown();
    asset_loader_shutdown();
    SDL_Quit();
    return 0;
}
