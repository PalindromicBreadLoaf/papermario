#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_message.h>
#include <PR/os_time.h>
#include <PR/os_reg.h>

// N64 CPU counter frequency is 46.875 MHz
#define N64_CPU_COUNTER 46875000ULL

static u64 pc_ticks(void) {
    u64 counter = SDL_GetPerformanceCounter();
    u64 freq    = SDL_GetPerformanceFrequency();
    return counter * N64_CPU_COUNTER / freq;
}

OSTime osGetTime(void) {
    return (OSTime)pc_ticks();
}

u32 osGetCount(void) {
    return (u32)pc_ticks();
}
