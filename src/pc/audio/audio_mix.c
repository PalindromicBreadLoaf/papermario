#include <string.h>
#include <stddef.h>
#include "audio_mix.h"
#include "vadpcm.h"

// N64 standard ADPCM uses order 2. npredictors = codebook_size / (order * 8 * sizeof(s16)).
#define ADPCM_ORDER 2

static PcAudioDriver *sDriver;

static inline s16 clamp_s16(s32 x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return (s16)x;
}

void audio_mix_set_driver(PcAudioDriver *driver) {
    sDriver = driver;
}

void audio_mix_reset_voice(PcVoiceInfo *v) {
    vadpcm_book_free(v->book);
    v->book           = NULL;
    v->last_predictor = NULL;
    v->last_cbsize    = 0;
    for (int i = 0; i < 16; i++) v->adpcm_state[i] = 0;
    v->sample_frac    = 0.0f;
    v->frame_cache_idx = -1;
    v->loop_rem       = 0;
}

static void mix_voice(PcVoiceInfo *v, s32 *acc_l, s32 *acc_r, int n) {
    if (v->wave_type == 0) {
        if (v->predictor != v->last_predictor || v->codebook_size != v->last_cbsize) {
            vadpcm_book_free(v->book);
            v->book = NULL;
            if (v->predictor && v->codebook_size > 0) {
                int npred = (int)v->codebook_size / (ADPCM_ORDER * 8 * 2);
                if (npred > 0)
                    v->book = vadpcm_book_create((const s16 *)v->predictor, ADPCM_ORDER, npred);
            }
            v->last_predictor = v->predictor;
            v->last_cbsize    = v->codebook_size;
        }
        if (!v->book) { v->is_playing = false; return; }
    }

    int wav_samples = (v->wave_type == 0)
        ? (int)(v->wav_data_len / VADPCM_BYTES_PER_FRAME) * VADPCM_SAMPLES_PER_FRAME
        : (int)(v->wav_data_len / 2);

    for (int i = 0; i < n; i++) {
        int in_pos = (int)v->sample_frac;

        if (v->loop_end > 0 && in_pos >= v->loop_end) {
            if (v->loop_rem == 0) { v->is_playing = false; break; }
            if (v->loop_rem > 0) v->loop_rem--;
            v->sample_frac     = (f32)v->loop_start;
            in_pos             = v->loop_start;
            // frame_cache_idx set to one before the loop frame so the while-loop below
            // decodes exactly frame (loop_start / 16) from the saved loop_state.
            v->frame_cache_idx = in_pos / VADPCM_SAMPLES_PER_FRAME - 1;
            if (v->loop_state) {
                for (int k = 0; k < 16; k++) v->adpcm_state[k] = (s32)v->loop_state[k];
            }
        }

        if (in_pos >= wav_samples) { v->is_playing = false; break; }

        s16 sample;
        if (v->wave_type == 0) {
            int frame_idx = in_pos / VADPCM_SAMPLES_PER_FRAME;
            // Decode frames sequentially up to the needed frame.
            while (v->frame_cache_idx < frame_idx) {
                v->frame_cache_idx++;
                const u8 *fp = v->wav_data + (size_t)v->frame_cache_idx * VADPCM_BYTES_PER_FRAME;
                vadpcm_decode_frame(fp, v->book, v->adpcm_state, v->frame_cache);
            }
            sample = v->frame_cache[in_pos % VADPCM_SAMPLES_PER_FRAME];
        } else {
            // Raw s16 in ROM byte order (big-endian); swap on little-endian hosts.
            const u8 *p = v->wav_data + (size_t)in_pos * 2;
            sample = (s16)(((u16)p[0] << 8) | p[1]);
        }

        acc_l[i] += ((s32)sample * (s32)v->vol_l) >> 15;
        acc_r[i] += ((s32)sample * (s32)v->vol_r) >> 15;
        v->sample_frac += v->pitch_ratio;
    }
}

void pc_audio_frame(s16 *out_stereo, int n_samples) {
    if (n_samples <= 0) return;

    if (!sDriver) {
        memset(out_stereo, 0, (size_t)n_samples * 2 * sizeof(s16));
        return;
    }

    if (sDriver->pre_video_frame) sDriver->pre_video_frame();

    int offset = 0;
    while (offset < n_samples) {
        int block = n_samples - offset;
        if (block > PC_AUDIO_SAMPLES) block = PC_AUDIO_SAMPLES;

        if (sDriver->pre_audio_frame) sDriver->pre_audio_frame();

        s32 acc_l[PC_AUDIO_SAMPLES];
        s32 acc_r[PC_AUDIO_SAMPLES];
        memset(acc_l, 0, sizeof(acc_l));
        memset(acc_r, 0, sizeof(acc_r));

        for (int vi = 0; vi < sDriver->n_voices; vi++) {
            PcVoiceInfo *v = &sDriver->voices[vi];
            if (v->is_playing) mix_voice(v, acc_l, acc_r, block);
        }

        for (int j = 0; j < block; j++) {
            out_stereo[(offset + j) * 2 + 0] = clamp_s16(acc_l[j]);
            out_stereo[(offset + j) * 2 + 1] = clamp_s16(acc_r[j]);
        }

        offset += block;
    }
}
