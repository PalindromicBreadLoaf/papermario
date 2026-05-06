#ifndef PC_AUDIO_VADPCM_H
#define PC_AUDIO_VADPCM_H

#include <PR/ultratypes.h>

#define VADPCM_SAMPLES_PER_FRAME 16
#define VADPCM_BYTES_PER_FRAME   9
#define VADPCM_MAX_ORDER         8

typedef struct {
    s32 coef[8][VADPCM_MAX_ORDER + 8];
} VadpcmPredictor;

typedef struct {
    VadpcmPredictor *predictors; // allocated; length = npredictors
    int npredictors;
    int order;
} VadpcmBook;

// Build the expanded codebook from raw big-endian s16 values stored in BK files.
// raw_be: pointer to npredictors * order * 8 consecutive big-endian s16 values.
// Returns NULL on allocation failure.
VadpcmBook *vadpcm_book_create(const u8 *raw_be, int order, int npredictors);
void        vadpcm_book_free(VadpcmBook *book);

// Decode one 9-byte ADPCM frame into 16 PCM s16 samples.
// state[16]: inter-frame s32 predictor history, updated in-place (zero-init for first frame).
// src: 9-byte ADPCM frame (1 header byte + 8 packed-nibble bytes).
// dst: 16 s16 output samples.
void vadpcm_decode_frame(const u8 *src, const VadpcmBook *book, s32 state[16], s16 dst[16]);

// Decode a complete in-memory ADPCM stream to a malloc'd s16 buffer.
// adpcm_bytes must be a multiple of VADPCM_BYTES_PER_FRAME.
// *out_n_samples receives the number of output samples.
// Caller must free the returned buffer. Returns NULL on failure.
s16 *vadpcm_decode(const u8 *adpcm, int adpcm_bytes,
                   const VadpcmBook *book, int *out_n_samples);

#endif /* PC_AUDIO_VADPCM_H */
