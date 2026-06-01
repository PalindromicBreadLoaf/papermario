#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "audio_mix.h"
#include "vadpcm.h"

// N64 standard ADPCM uses order 2. npredictors = codebook_size / (order * 8 * sizeof(s16)).
#define ADPCM_ORDER 2

static PcAudioDriver *sDriver;

#if defined(__linux__)
typedef struct {
    uintptr_t start;
    uintptr_t end;
} PcAudioReadableRange;

#define PC_AUDIO_READABLE_MAX_RANGES 256
static PcAudioReadableRange sReadableRanges[PC_AUDIO_READABLE_MAX_RANGES];
static int sReadableRangeCount;

static void pc_audio_refresh_readable_ranges(void) {
    char line[256];
    FILE *maps = fopen("/proc/self/maps", "r");

    sReadableRangeCount = 0;
    if (maps == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long long mapStart;
        unsigned long long mapEnd;
        char perms[5];

        if (sscanf(line, "%llx-%llx %4s", &mapStart, &mapEnd, perms) != 3 || perms[0] != 'r') {
            continue;
        }

        if (sReadableRangeCount > 0 && sReadableRanges[sReadableRangeCount - 1].end == (uintptr_t)mapStart) {
            sReadableRanges[sReadableRangeCount - 1].end = (uintptr_t)mapEnd;
            continue;
        }

        if (sReadableRangeCount >= PC_AUDIO_READABLE_MAX_RANGES) {
            break;
        }
        sReadableRanges[sReadableRangeCount].start = (uintptr_t)mapStart;
        sReadableRanges[sReadableRangeCount].end = (uintptr_t)mapEnd;
        sReadableRangeCount++;
    }

    fclose(maps);
}

static bool pc_audio_range_lookup(uintptr_t start, uintptr_t end) {
    int lo = 0;
    int hi = sReadableRangeCount - 1;

    while (lo <= hi) {
        int mid = (lo + hi) >> 1;

        if (sReadableRanges[mid].end <= start) {
            lo = mid + 1;
        } else if (sReadableRanges[mid].start > start) {
            hi = mid - 1;
        } else {
            return end <= sReadableRanges[mid].end;
        }
    }

    return false;
}

static bool pc_audio_range_readable(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;

    if (ptr == NULL || size == 0 || end < start) {
        return false;
    }

    if (sReadableRangeCount == 0) {
        pc_audio_refresh_readable_ranges();
    }
    if (pc_audio_range_lookup(start, end)) {
        return true;
    }

    pc_audio_refresh_readable_ranges();
    return pc_audio_range_lookup(start, end);
}
#else
static bool pc_audio_range_readable(const void *ptr, size_t size) {
    return ptr != NULL && size != 0;
}
#endif

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
    v->vol_l          = 0;
    v->vol_r          = 0;
    v->cur_vol_l      = 0.0f;
    v->cur_vol_r      = 0.0f;
    v->vol_step_l     = 0.0f;
    v->vol_step_r     = 0.0f;
    v->vol_ramp_samples = 0;
}

void audio_mix_set_voice_volume(PcVoiceInfo *v, s16 vol_l, s16 vol_r, s32 ramp_samples) {
    v->vol_l = vol_l;
    v->vol_r = vol_r;

    if (!v->is_playing || ramp_samples <= 0) {
        v->cur_vol_l = (f32)vol_l;
        v->cur_vol_r = (f32)vol_r;
        v->vol_step_l = 0.0f;
        v->vol_step_r = 0.0f;
        v->vol_ramp_samples = 0;
        return;
    }

    v->vol_step_l = ((f32)vol_l - v->cur_vol_l) / (f32)ramp_samples;
    v->vol_step_r = ((f32)vol_r - v->cur_vol_r) / (f32)ramp_samples;
    v->vol_ramp_samples = ramp_samples;
}

static inline void advance_voice_volume(PcVoiceInfo *v) {
    if (v->vol_ramp_samples <= 0) {
        return;
    }

    v->cur_vol_l += v->vol_step_l;
    v->cur_vol_r += v->vol_step_r;
    v->vol_ramp_samples--;

    if (v->vol_ramp_samples == 0) {
        v->cur_vol_l = (f32)v->vol_l;
        v->cur_vol_r = (f32)v->vol_r;
        v->vol_step_l = 0.0f;
        v->vol_step_r = 0.0f;
    }
}

static bool decode_next_adpcm_frame_preview(PcVoiceInfo *v, int frame_idx, int max_frame, s16 out[16]) {
    if (frame_idx < 0 || frame_idx >= max_frame) {
        return false;
    }

    s32 state[16];
    memcpy(state, v->adpcm_state, sizeof(state));
    const u8 *fp = v->wav_data + (size_t)frame_idx * VADPCM_BYTES_PER_FRAME;
    vadpcm_decode_frame(fp, v->book, state, out);
    return true;
}

static void mix_voice(PcVoiceInfo *v, s32 *acc_l, s32 *acc_r, int n) {
    if (v->wav_data == NULL || v->wav_data_len == 0 || !isfinite(v->pitch_ratio) || v->pitch_ratio <= 0.0f) {
        v->is_playing = false;
        return;
    }

    if (v->wave_type == 0) {
        u32 usable_len = (v->wav_data_len / VADPCM_BYTES_PER_FRAME) * VADPCM_BYTES_PER_FRAME;

        if (usable_len == 0
            || v->predictor == NULL
            || v->codebook_size < ADPCM_ORDER * 8 * sizeof(s16)
            || (v->codebook_size % (ADPCM_ORDER * 8 * sizeof(s16))) != 0
            || !pc_audio_range_readable(v->wav_data, usable_len)
            || !pc_audio_range_readable(v->predictor, v->codebook_size)) {
            v->is_playing = false;
            return;
        }

        if (v->predictor != v->last_predictor || v->codebook_size != v->last_cbsize) {
            vadpcm_book_free(v->book);
            v->book = NULL;
            int npred = (int)v->codebook_size / (ADPCM_ORDER * 8 * sizeof(s16));
            if (npred > 0)
                v->book = vadpcm_book_create((const s16 *)v->predictor, ADPCM_ORDER, npred);
            v->last_predictor = v->predictor;
            v->last_cbsize    = v->codebook_size;
        }
        if (!v->book) { v->is_playing = false; return; }
    } else if (!pc_audio_range_readable(v->wav_data, v->wav_data_len & ~1u)) {
        v->is_playing = false;
        return;
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
            if (v->loop_state && pc_audio_range_readable(v->loop_state, 16 * sizeof(*v->loop_state))) {
                for (int k = 0; k < 16; k++) v->adpcm_state[k] = (s32)v->loop_state[k];
            }
        }

        if (in_pos >= wav_samples) { v->is_playing = false; break; }

        s32 sample;
        f32 frac = v->sample_frac - (f32)in_pos;
        if (v->wave_type == 0) {
            int frame_idx = in_pos / VADPCM_SAMPLES_PER_FRAME;
            int frame_off = in_pos % VADPCM_SAMPLES_PER_FRAME;
            // Decode frames sequentially up to the needed frame.
            int max_frame = (int)(v->wav_data_len / VADPCM_BYTES_PER_FRAME);
            if (frame_idx >= max_frame) {
                v->is_playing = false;
                return;
            }
            while (v->frame_cache_idx < frame_idx) {
                v->frame_cache_idx++;
                if (v->frame_cache_idx < 0 || v->frame_cache_idx >= max_frame) {
                    v->is_playing = false;
                    return;
                }
                const u8 *fp = v->wav_data + (size_t)v->frame_cache_idx * VADPCM_BYTES_PER_FRAME;
                vadpcm_decode_frame(fp, v->book, v->adpcm_state, v->frame_cache);
            }
            sample = v->frame_cache[frame_off];
            if (frac > 0.0f) {
                int interp_limit = (v->loop_end > 0) ? v->loop_end : wav_samples;
                if (in_pos + 1 < interp_limit) {
                    s32 next;
                    bool have_next = true;

                    if (frame_off + 1 < VADPCM_SAMPLES_PER_FRAME) {
                        next = v->frame_cache[frame_off + 1];
                    } else {
                        s16 next_frame[16];
                        have_next = decode_next_adpcm_frame_preview(v, frame_idx + 1, max_frame, next_frame);
                        next = have_next ? next_frame[0] : 0;
                    }

                    if (have_next) {
                        sample += (s32)((next - sample) * frac);
                    }
                }
            }
        } else {
            // Raw s16 in ROM byte order (big-endian); swap on little-endian hosts.
            const u8 *p = v->wav_data + (size_t)in_pos * 2;
            sample = (s16)(((u16)p[0] << 8) | p[1]);
            if (frac > 0.0f) {
                int interp_limit = (v->loop_end > 0) ? v->loop_end : wav_samples;
                if (in_pos + 1 < interp_limit) {
                    p = v->wav_data + (size_t)(in_pos + 1) * 2;
                    s32 next = (s16)(((u16)p[0] << 8) | p[1]);
                    sample += (s32)((next - sample) * frac);
                }
            }
        }

        acc_l[i] += (sample * (s32)v->cur_vol_l) >> 15;
        acc_r[i] += (sample * (s32)v->cur_vol_r) >> 15;
        advance_voice_volume(v);
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
