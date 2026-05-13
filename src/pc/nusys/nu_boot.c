#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_host.h>
#include <nu/nusys.h>

extern void boot_main(void *);
extern void nu_audio_init(void);

static OSThread sIdleThread;
static OSThread sMainThread;
static u64      sIdleStack[NU_SC_STACK_SIZE / sizeof(u64)];
static u64      sMainStack[NU_SC_STACK_SIZE / sizeof(u64)];

static void boot_idle_pc(void *arg) {
    (void)arg;
    nuPiInit();
    nuScCreateScheduler(OS_VI_NTSC_LAN1, 1);
    nu_audio_init();
    osCreateThread(&sMainThread, NU_MAIN_THREAD_ID, boot_main, NULL,
                   &sMainStack[NU_SC_STACK_SIZE / sizeof(u64)], NU_MAIN_THREAD_PRI);
    osStartThread(&sMainThread);

    while (1) { SDL_Delay(100); }
}

void nuBoot(void) {
    osInitialize();
    osCreateThread(&sIdleThread, NU_IDLE_THREAD_ID, boot_idle_pc, NULL,
                   &sIdleStack[NU_SC_STACK_SIZE / sizeof(u64)], 10);
    osStartThread(&sIdleThread);
}
