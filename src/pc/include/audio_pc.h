#ifndef PC_AUDIO_PC_H
#define PC_AUDIO_PC_H

#include <PR/ultratypes.h>

// Initialize SDL2 audio subsystem.
void audio_pc_init(void);

// Open (or reopen) the SDL2 audio device at the requested sample rate.
u32 audio_pc_open_device(u32 freq);

// Queue interleaved stereo s16 samples for playback.
// n_stereo_frames: number of L+R pairs to enqueue.
void audio_pc_push_samples(const s16 *buf, int n_stereo_frames);

// Current device sample rate (0 if device is not open).
u32 audio_pc_get_freq(void);

// Close the audio device and shut down SDL2 audio.
void audio_pc_shutdown(void);

#endif /* PC_AUDIO_PC_H */
