#include <SDL2/SDL.h>
#include <PR/ultratypes.h>
#include "audio_pc.h"
#include <stdio.h>

static SDL_AudioDeviceID sDevice;
static u32               sFreq;

void audio_pc_init(void) {
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "audio_pc: SDL audio init failed: %s\n", SDL_GetError());
    }
}

u32 audio_pc_open_device(u32 freq) {
    if (sDevice) {
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = (int)freq;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = NULL;

    sDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!sDevice) {
        fprintf(stderr, "audio_pc: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        sFreq = freq;
        return freq;
    }

    sFreq = (u32)have.freq;
    SDL_PauseAudioDevice(sDevice, 0);
    return sFreq;
}

void audio_pc_push_samples(const s16 *buf, int n_stereo_frames) {
    if (!sDevice || !buf || n_stereo_frames <= 0) return;

    // Keep SDL's queue near two audio frames so nuAuMgr is paced by the
    // device instead of running ahead and dropping already-advanced BGM frames.
    const u32 target_bytes = (u32)(1200 * 2 * sizeof(s16));
    while (SDL_GetQueuedAudioSize(sDevice) > target_bytes) {
        SDL_Delay(1);
    }
    SDL_QueueAudio(sDevice, buf, (u32)(n_stereo_frames * 2 * sizeof(s16)));
}

u32 audio_pc_get_freq(void) {
    return sFreq;
}

void audio_pc_shutdown(void) {
    if (sDevice) {
        SDL_CloseAudioDevice(sDevice);
        sDevice = 0;
    }
    sFreq = 0;
}
