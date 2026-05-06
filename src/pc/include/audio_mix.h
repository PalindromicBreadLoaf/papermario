#ifndef PC_AUDIO_MIX_H
#define PC_AUDIO_MIX_H

#include <PR/ultratypes.h>
#include <stdbool.h>
#include "vadpcm.h"

// Platform-neutral description of one playback voice.
// The game side fills in the top section when starting/updating a voice.
// The mixer owns the bottom section; the game must not modify those fields.
typedef struct {
    bool      is_playing;
    u8        wave_type;        // 0 = ADPCM (AL_ADPCM_WAVE), 1 = raw s16 (AL_RAW16_WAVE)
    const u8 *wav_data;
    u32       wav_data_len;     // bytes
    const u8 *predictor;        // big-endian s16 codebook bytes (ADPCM only)
    u16       codebook_size;    // predictor byte count
    s32       loop_start;       // input-sample index where the loop restarts
    s32       loop_end;         // input-sample index that triggers a loop (0 = no loop)
    s32       loop_count;       // -1 = infinite; 0 = one-shot; >0 = N repeats
    const s16 *loop_state;      // s16[16] predictor state at loop_start frame, or NULL
    f32       pitch_ratio;      // input samples consumed per output sample (1.0 = natural)
    s16       vol_l;            // left  volume  0..0x7FFF
    s16       vol_r;            // right volume  0..0x7FFF
    s32       adpcm_state[16];  // inter-frame s32 predictor state
    f32       sample_frac;      // fractional input-sample position
    int       frame_cache_idx;  // which ADPCM frame is in frame_cache (-1 = none)
    s16       frame_cache[16];  // decoded ADPCM samples for the cached frame
    s32       loop_rem;         // remaining loop iterations (-1 = infinite)
    VadpcmBook *book;           // lazily built codebook
    const u8 *last_predictor;   // detect instrument change
    u16       last_cbsize;
} PcVoiceInfo;

// Callback interface to the game's audio system.
typedef struct {
    PcVoiceInfo *voices;
    int          n_voices;
    void       (*pre_video_frame)(void);
    void       (*pre_audio_frame)(void);
} PcAudioDriver;

// Register the game's audio driver. Pass NULL to output silence.
void audio_mix_set_driver(PcAudioDriver *driver);

// Audio block size: matches the game's AUDIO_SAMPLES constant (184 samples per RSP frame).
#define PC_AUDIO_SAMPLES 184

// Generate n_samples of interleaved stereo s16 PCM.
// n_samples is processed in PC_AUDIO_SAMPLES-sized blocks; pre_audio_frame fires once per block.
// Called from the audio thread.
void pc_audio_frame(s16 *out_stereo, int n_samples);

// Reset all mixer-owned fields of a voice (call when stopping a voice).
void audio_mix_reset_voice(PcVoiceInfo *v);

#endif /* PC_AUDIO_MIX_H */
