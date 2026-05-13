#include <PR/os_ai.h>
#include <PR/ultratypes.h>
#include <stdint.h>
#include "audio_pc.h"

void *pc_resolve_physical_addr(uintptr_t addr);

s32 osAiSetFrequency(u32 freq) {
    return (s32)audio_pc_open_device(freq);
}

s32 osAiSetNextBuffer(void *buf, u32 size) {
    const s16 *samples = pc_resolve_physical_addr((uintptr_t)buf);

    audio_pc_push_samples(samples, (int)(size / (2 * sizeof(s16))));
    return 0;
}

u32 osAiGetStatus(void) {
    return 0;
}

u32 osAiGetLength(void) {
    return 0;
}
