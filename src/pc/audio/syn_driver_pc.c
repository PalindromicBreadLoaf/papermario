#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <PR/ultratypes.h>
#include <PR/abi.h>
#include <PR/libaudio.h>
#include "audio_mix.h"
#include "audio_pc.h"
#include "syn_driver_pc.h"

#define PC_SYN_N_VOICES 24
#define PC_SYN_N_BUSES  4

// Constant-power panning lookup (identical to AuEqPower in pull_voice.c).
// Index 0 = hard left, 127 = hard right.  Values are in [0, 0x7FFF].
#define EQ_MID 64
#define EQ_MAX 127

static const s16 sEqPower[128] = {
    32767, 32764, 32757, 32744, 32727, 32704, 32677, 32644,
    32607, 32564, 32517, 32464, 32407, 32344, 32277, 32205,
    32127, 32045, 31958, 31866, 31770, 31668, 31561, 31450,
    31334, 31213, 31087, 30957, 30822, 30682, 30537, 30388,
    30234, 30075, 29912, 29744, 29572, 29395, 29214, 29028,
    28838, 28643, 28444, 28241, 28033, 27821, 27605, 27385,
    27160, 26931, 26698, 26461, 26220, 25975, 25726, 25473,
    25216, 24956, 24691, 24423, 24151, 23875, 23596, 23313,
    23026, 22736, 22442, 22145, 21845, 21541, 21234, 20924,
    20610, 20294, 19974, 19651, 19325, 18997, 18665, 18331,
    17993, 17653, 17310, 16965, 16617, 16266, 15913, 15558,
    15200, 14840, 14477, 14113, 13746, 13377, 13006, 12633,
    12258, 11881, 11503, 11122, 10740, 10357,  9971,  9584,
     9196,  8806,  8415,  8023,  7630,  7235,  6839,  6442,
     6044,  5646,  5246,  4845,  4444,  4042,  3640,  3237,
     2833,  2429,  2025,  1620,  1216,   810,   405,     0,
};

#ifndef _AUDIO_H_
typedef struct Instrument {
    u8   *wavData;        /* 0x00 */
    u32   wavDataLength;  /* 0x04 */
    s16  *loopState;      /* 0x08  (ADPCM_STATE = s16[16]) */
    s32   loopStart;      /* 0x0C */
    s32   loopEnd;        /* 0x10 */
    s32   loopCount;      /* 0x14 */
    s16  *predictor;      /* 0x18 */
    u16   codebookSize;   /* 0x1C */
    u16   keyBase;        /* 0x1E */
    f32   pitchRatio;     /* 0x20 */
    u8    type;           /* 0x24 */
    /* remaining fields not needed for voice setup */
} Instrument;
#endif

// AuSynDriver and ALConfig are large game structs; forward-declare opaquely so
// au_driver_init can be stubbed without pulling in the full game headers.
#ifndef _AUDIO_H_
typedef struct AuSynDriver AuSynDriver;
typedef struct ALConfig    ALConfig;
#endif

typedef struct {
    u8   pan;     // 0..127 constant-power panning position
    u16  volume;  // squared volume in game units: (vol^2 >> 15), 0..0x7FFF
    s8   bus;     // FX bus index (-1 = unassigned / silent)
} SynVoiceState;

static PcVoiceInfo  sVoices[PC_SYN_N_VOICES];
static SynVoiceState sVoiceState[PC_SYN_N_VOICES];
static u16           sBusGain[PC_SYN_N_BUSES];
static bool          sStereoEnabled = true;
static bool          sUseGlobalVolume = false;
static u16           sGlobalVolume = 0x7FFF;

static PcAudioDriver sDriver;

__attribute__((weak)) void au_update_clients_for_video_frame(void) {}
__attribute__((weak)) void au_update_clients_for_audio_frame(void) {}

static void compute_vol_lr(u8 voiceIdx) {
    SynVoiceState *st = &sVoiceState[voiceIdx];
    PcVoiceInfo   *v  = &sVoices[voiceIdx];

    u16 bus_gain = (st->bus >= 0 && st->bus < PC_SYN_N_BUSES)
                 ? sBusGain[st->bus] : 0x7FFF;

    u32 effective_vol = st->volume;
    if (sUseGlobalVolume) {
        effective_vol = (u32)(effective_vol * sGlobalVolume) >> 15;
    }

    effective_vol = (u32)(effective_vol * bus_gain) >> 15;
    if (effective_vol > 0x7FFF) effective_vol = 0x7FFF;

    u16 ev16 = (u16)effective_vol;

    if (!sStereoEnabled) {
        s16 mono = (s16)(((u32)ev16 * sEqPower[EQ_MID]) >> 15);
        v->vol_l = v->vol_r = mono;
    } else {
        v->vol_l = (s16)(((u32)ev16 * sEqPower[st->pan])           >> 15);
        v->vol_r = (s16)(((u32)ev16 * sEqPower[EQ_MAX - st->pan])  >> 15);
    }
}

static void syn_pre_video_frame(void) {
    au_update_clients_for_video_frame();
}

static void syn_pre_audio_frame(void) {
    au_update_clients_for_audio_frame();
}

void pc_syn_init(void) {
    for (int i = 0; i < PC_SYN_N_VOICES; i++) {
        sVoices[i].frame_cache_idx = -1;
        sVoiceState[i].pan         = EQ_MID;
        sVoiceState[i].volume      = 0;
        sVoiceState[i].bus         = 0;
    }
    for (int i = 0; i < PC_SYN_N_BUSES; i++) {
        sBusGain[i] = 0x7FFF;
    }

    sDriver.voices          = sVoices;
    sDriver.n_voices        = PC_SYN_N_VOICES;
    sDriver.pre_video_frame = syn_pre_video_frame;
    sDriver.pre_audio_frame = syn_pre_audio_frame;
    audio_mix_set_driver(&sDriver);
}

Acmd *alAudioFrame(Acmd *cmdList, s32 *cmdLen, s16 *outBuf, s32 outLen) {
    *cmdLen = 0;
    pc_audio_frame(outBuf, outLen);
    return cmdList;
}

void au_pvoice_set_bus(u8 voiceIdx, s8 busID) {
    sVoiceState[voiceIdx].bus = busID;
    compute_vol_lr(voiceIdx);
}

void au_syn_start_voice(u8 voiceIdx) {
    sVoices[voiceIdx].is_playing = true;
}

void au_syn_stop_voice(u8 voiceIdx) {
    PcVoiceInfo *v = &sVoices[voiceIdx];
    v->is_playing = false;
    audio_mix_reset_voice(v);
}

void au_syn_start_voice_params(u8 voiceIdx, u8 busID, Instrument *instrument,
                               f32 pitchRatio, s16 vol, u8 pan, u8 fxMix, s32 delta) {
    (void)fxMix;  // TODO: reverb
    (void)delta;  // TODO: volume ramping

    PcVoiceInfo  *v  = &sVoices[voiceIdx];
    SynVoiceState *st = &sVoiceState[voiceIdx];

    audio_mix_reset_voice(v);

    v->wave_type     = instrument->type;
    v->wav_data      = instrument->wavData;
    v->wav_data_len  = instrument->wavDataLength;
    v->pitch_ratio   = pitchRatio;

    if (instrument->type == AL_ADPCM_WAVE) {
        v->predictor     = (const u8 *)instrument->predictor;
        v->codebook_size = instrument->codebookSize;
        v->loop_state    = instrument->loopState;

        if (instrument->loopEnd != 0) {
            v->loop_start = instrument->loopStart;
            v->loop_end   = instrument->loopEnd;
            // 0 = infinite
            v->loop_rem   = (instrument->loopCount == 0) ? -1 : instrument->loopCount;
        } else {
            v->loop_start = 0;
            v->loop_end   = 0;
            v->loop_rem   = 0;
        }
    } else {
        // AL_RAW16_WAVE
        v->predictor     = NULL;
        v->codebook_size = 0;
        v->loop_state    = NULL;

        if (instrument->loopEnd != 0) {
            v->loop_start = instrument->loopStart;
            v->loop_end   = instrument->loopEnd;
            v->loop_rem   = (instrument->loopCount == 0) ? -1 : instrument->loopCount;
        } else {
            v->loop_start = 0;
            v->loop_end   = 0;
            v->loop_rem   = 0;
        }
    }

    st->bus    = (s8)busID;
    st->pan    = pan;
    st->volume = (u16)((u32)vol * vol >> 15);

    compute_vol_lr(voiceIdx);

    v->is_playing = true;
}

void au_syn_set_wavetable(u8 voiceIdx, Instrument *instrument) {
    PcVoiceInfo *v = &sVoices[voiceIdx];

    audio_mix_reset_voice(v);

    v->wave_type    = instrument->type;
    v->wav_data     = instrument->wavData;
    v->wav_data_len = instrument->wavDataLength;

    if (instrument->type == AL_ADPCM_WAVE) {
        v->predictor     = (const u8 *)instrument->predictor;
        v->codebook_size = instrument->codebookSize;
        v->loop_state    = instrument->loopState;

        if (instrument->loopEnd != 0) {
            v->loop_start = instrument->loopStart;
            v->loop_end   = instrument->loopEnd;
            v->loop_rem   = (instrument->loopCount == 0) ? -1 : instrument->loopCount;
        } else {
            v->loop_start = v->loop_end = v->loop_rem = 0;
        }
    } else {
        v->predictor = NULL; v->codebook_size = 0; v->loop_state = NULL;
        if (instrument->loopEnd != 0) {
            v->loop_start = instrument->loopStart;
            v->loop_end   = instrument->loopEnd;
            v->loop_rem   = (instrument->loopCount == 0) ? -1 : instrument->loopCount;
        } else {
            v->loop_start = v->loop_end = v->loop_rem = 0;
        }
    }
}

void au_syn_set_pitch(u8 voiceIdx, f32 pitch) {
    sVoices[voiceIdx].pitch_ratio = pitch;
}

void au_syn_set_mixer_params(u8 voiceIdx, s16 volume, s32 delta, u8 pan, u8 fxMix) {
    (void)fxMix;
    (void)delta;
    SynVoiceState *st = &sVoiceState[voiceIdx];
    st->pan    = pan;
    st->volume = (u16)((u32)volume * volume >> 15);
    compute_vol_lr(voiceIdx);
}

void au_syn_set_pan_fxmix(u8 voiceIdx, u8 pan, u8 fxMix) {
    (void)fxMix;
    sVoiceState[voiceIdx].pan = pan;
    compute_vol_lr(voiceIdx);
}

void au_syn_set_volume_delta(u8 voiceIdx, s16 vol, s32 delta) {
    (void)delta;
    sVoiceState[voiceIdx].volume = (u16)((u32)vol * vol >> 15);
    compute_vol_lr(voiceIdx);
}

void au_syn_set_pan(u8 voiceIdx, u8 pan) {
    sVoiceState[voiceIdx].pan = pan;
    compute_vol_lr(voiceIdx);
}

void au_syn_set_fxmix(u8 voiceIdx, u8 fxMix) {
    (void)voiceIdx;
    (void)fxMix;
    // Reverb not implemented; fxmix is ignored.
}

s32 au_syn_get_playing(u8 voiceIdx) {
    return sVoices[voiceIdx].is_playing ? 1 : 0;
}

s32 au_syn_get_bus(u8 voiceIdx) {
    return sVoiceState[voiceIdx].bus;
}

f32 au_syn_get_pitch(u8 voiceIdx) {
    return sVoices[voiceIdx].pitch_ratio;
}

u8 au_syn_get_pan(u8 voiceIdx) {
    return sVoiceState[voiceIdx].pan;
}

s16 au_syn_get_dryamt(u8 voiceIdx) {
    (void)voiceIdx;
    return 0x7FFF;
}

s16 au_syn_get_wetamt(u8 voiceIdx) {
    (void)voiceIdx;
    return 0;
}

s32 au_syn_get_volume_left(u8 voiceIdx) {
    return sVoices[voiceIdx].vol_l;
}

s32 au_syn_get_volume_right(u8 voiceIdx) {
    return sVoices[voiceIdx].vol_r;
}

void au_bus_set_volume(u8 busID, u16 value) {
    if (busID < PC_SYN_N_BUSES) {
        sBusGain[busID] = value & 0x7FFF;
        // Recompute volumes for all voices on this bus.
        for (int i = 0; i < PC_SYN_N_VOICES; i++) {
            if (sVoiceState[i].bus == (s8)busID) compute_vol_lr(i);
        }
    }
}

u16 au_bus_get_volume(u8 busID) {
    return (busID < PC_SYN_N_BUSES) ? sBusGain[busID] : 0;
}

void au_bus_set_effect(u8 busID, u8 effectType) {
    (void)busID;
    (void)effectType;
    // Reverb not implemented.
}

void au_bus_set_fx_params(u8 busID, s16 delayIndex, s16 paramID, s32 value) {
    (void)busID; (void)delayIndex; (void)paramID; (void)value;
}

void au_set_stereo_enabled(s8 enabled) {
    sStereoEnabled = (enabled != 0);
    for (int i = 0; i < PC_SYN_N_VOICES; i++) compute_vol_lr(i);
}

void au_use_global_volume(void) {
    sUseGlobalVolume = true;
}

void au_set_global_volume(s16 volume) {
    sGlobalVolume = (u16)volume;
    for (int i = 0; i < PC_SYN_N_VOICES; i++) compute_vol_lr(i);
}

s16 au_get_global_volume(void) {
    return (s16)sGlobalVolume;
}

void au_set_delay_time(s32 arg0)          { (void)arg0; }
void au_delay_left_channel(u8 arg0)       { (void)arg0; }
void au_delay_right_channel(u8 arg0)      { (void)arg0; }
void au_disable_channel_delay(void)        {}
void au_init_delay_channel(s16 arg0)      { (void)arg0; }

void au_driver_init(AuSynDriver *driver, ALConfig *config) {
    (void)driver;
    (void)config;
}

void au_driver_release(void) {}

// Linear bump-allocator matching the game's ALHeap behaviour.
void alHeapInit(ALHeap *hp, u8 *base, s32 len) {
    hp->base  = base;
    hp->cur   = base;
    hp->len   = len;
    hp->count = 0;
}

void *alHeapDBAlloc(u8 *file, s32 line, ALHeap *hp, s32 num, s32 size) {
    (void)file; (void)line;
    s32 bytes = (num * size + 7) & ~7;
    if (hp->cur + bytes > hp->base + hp->len) {
        fprintf(stderr, "alHeapDBAlloc: out of heap memory\n");
        return NULL;
    }
    void *ptr = hp->cur;
    hp->cur  += bytes;
    hp->count++;
    memset(ptr, 0, (size_t)bytes);
    return ptr;
}

s32 alHeapCheck(ALHeap *hp) {
    (void)hp;
    return 0;
}

void alCopy(void *src, void *dest, s32 len) {
    memcpy(dest, src, (size_t)len);
}

void alLink(ALLink *element, ALLink *after) {
    element->next = after->next;
    element->prev = after;
    if (after->next) after->next->prev = element;
    after->next = element;
}

void alUnlink(ALLink *element) {
    if (element->next) element->next->prev = element->prev;
    if (element->prev) element->prev->next = element->next;
}

// Fix up in-ROM offsets in a sequence file so that seqArray[i].offset
// becomes an absolute pointer. On N64 these are ROM-relative; on PC the
// asset loader maps the ROM directly so the same fixup applies.
void alSeqFileNew(ALSeqFile *f, u8 *base) {
    for (int i = 0; i < f->seqCount; i++) {
        if (f->seqArray[i].offset != NULL) {
            f->seqArray[i].offset = base + (ptrdiff_t)(uintptr_t)f->seqArray[i].offset;
        }
    }
}

// Prelocate each bank pointer relative to the given table.
void alBnkfNew(ALBankFile *f, u8 *table) {
    if (!f || !table) return;

    for (int bi = 0; bi < f->bankCount; bi++) {
        ALBank *bank = (ALBank *)(table + (ptrdiff_t)(uintptr_t)f->bankArray[bi]);
        f->bankArray[bi] = bank;

        if (bank->percussion) {
            bank->percussion = (ALInstrument *)(table + (ptrdiff_t)(uintptr_t)bank->percussion);
        }
        for (int ii = 0; ii < bank->instCount; ii++) {
            if (!bank->instArray[ii]) continue;
            ALInstrument *inst = (ALInstrument *)(table + (ptrdiff_t)(uintptr_t)bank->instArray[ii]);
            bank->instArray[ii] = inst;

            for (int si = 0; si < inst->soundCount; si++) {
                if (!inst->soundArray[si]) continue;
                ALSound *snd = (ALSound *)(table + (ptrdiff_t)(uintptr_t)inst->soundArray[si]);
                inst->soundArray[si] = snd;

                if (snd->envelope) {
                    snd->envelope = (ALEnvelope *)(table + (ptrdiff_t)(uintptr_t)snd->envelope);
                }
                if (snd->keyMap) {
                    snd->keyMap = (ALKeyMap *)(table + (ptrdiff_t)(uintptr_t)snd->keyMap);
                }
                if (snd->wavetable) {
                    ALWaveTable *wt = (ALWaveTable *)(table + (ptrdiff_t)(uintptr_t)snd->wavetable);
                    snd->wavetable = wt;

                    if (wt->base) {
                        wt->base = table + (ptrdiff_t)(uintptr_t)wt->base;
                    }
                    if (wt->type == AL_ADPCM_WAVE) {
                        if (wt->waveInfo.adpcmWave.loop) {
                            wt->waveInfo.adpcmWave.loop = (ALADPCMloop *)(table + (ptrdiff_t)(uintptr_t)wt->waveInfo.adpcmWave.loop);
                        }
                        if (wt->waveInfo.adpcmWave.book) {
                            wt->waveInfo.adpcmWave.book = (ALADPCMBook *)(table + (ptrdiff_t)(uintptr_t)wt->waveInfo.adpcmWave.book);
                        }
                    } else {
                        if (wt->waveInfo.rawWave.loop) {
                            wt->waveInfo.rawWave.loop = (ALRawLoop *)(table + (ptrdiff_t)(uintptr_t)wt->waveInfo.rawWave.loop);
                        }
                    }
                }
            }
        }
    }
}
