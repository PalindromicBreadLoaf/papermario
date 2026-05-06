#include <PR/os_ai.h>
#include <PR/ultratypes.h>
#include "audio_pc.h"

s32 osAiSetFrequency(u32 freq) {
    return (s32)audio_pc_open_device(freq);
}

s32 osAiSetNextBuffer(void *buf, u32 size) {
    (void)buf;
    (void)size;
    return 0;
}

u32 osAiGetStatus(void) {
    return 0;
}

u32 osAiGetLength(void) {
    return 0;
}
