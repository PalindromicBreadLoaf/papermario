#include "vadpcm.h"
#include <stdlib.h>
#include <string.h>

static s16 read_be_s16(const u8 *p) {
    return (s16)(((u16)p[0] << 8) | p[1]);
}

static s32 inner_product(int length, const s32 *v1, const s32 *v2) {
    s32 out = 0;
    for (int i = 0; i < length; i++) {
        out += v1[i] * v2[i];
    }
    s32 dout  = out / (1 << 11);
    s32 fiout = dout * (1 << 11);
    return (out - fiout < 0) ? dout - 1 : dout;
}

VadpcmBook *vadpcm_book_create(const u8 *raw_be, int order, int npredictors) {
    if (!raw_be || order <= 0 || order > VADPCM_MAX_ORDER || npredictors <= 0) {
        return NULL;
    }

    VadpcmBook *book = malloc(sizeof(VadpcmBook));
    if (!book) return NULL;

    book->predictors = malloc((size_t)npredictors * sizeof(VadpcmPredictor));
    if (!book->predictors) {
        free(book);
        return NULL;
    }

    book->order        = order;
    book->npredictors  = npredictors;

    const u8 *p = raw_be;
    for (int pred = 0; pred < npredictors; pred++) {
        s32 (*te)[VADPCM_MAX_ORDER + 8] = book->predictors[pred].coef;

        // Raw layout in the file: [order][8] — for each tap j, read 8 big-endian s16 values.
        for (int j = 0; j < order; j++) {
            for (int k = 0; k < 8; k++) {
                te[k][j] = read_be_s16(p);
                p += 2;
            }
        }

        for (int k = 1; k < 8; k++) {
            te[k][order] = te[k - 1][order - 1];
        }
        te[0][order] = 1 << 11;

        for (int k = 1; k < 8; k++) {
            for (int j = 0; j < k; j++) {
                te[j][k + order] = 0;
            }
            for (int j = k; j < 8; j++) {
                te[j][k + order] = te[j - k][order];
            }
        }
    }

    return book;
}

void vadpcm_book_free(VadpcmBook *book) {
    if (!book) return;
    free(book->predictors);
    free(book);
}

void vadpcm_decode_frame(const u8 *src, const VadpcmBook *book, s32 state[16], s16 dst[16]) {
    u8  header  = *src++;
    s32 scale   = 1 << (header >> 4);
    s32 optimalp = header & 0xf;
    if (optimalp >= book->npredictors) {
        optimalp = 0;
    }

    // Unpack 16 4-bit residuals from the 8 packed bytes, applying scale and sign-extend.
    s32 ix[16];
    for (int i = 0; i < 16; i += 2) {
        u8  c  = *src++;
        s32 hi = (s32)(c >> 4);
        s32 lo = (s32)(c & 0xf);
        ix[i]     = (hi <= 7) ? hi * scale : (-0x10 + hi) * scale;
        ix[i + 1] = (lo <= 7) ? lo * scale : (-0x10 + lo) * scale;
    }

    int  order = book->order;
    const s32 (*te)[VADPCM_MAX_ORDER + 8] = book->predictors[optimalp].coef;

    // Decode in two 8-sample halves to match the N64 ADPCM microcode structure.
    for (int j = 0; j < 2; j++) {
        s32 in_vec[VADPCM_MAX_ORDER + 8];

        // Load history: last 'order' samples from the previous half (or previous frame for j=0).
        if (j == 0) {
            for (int i = 0; i < order; i++) {
                in_vec[i] = state[16 - order + i];
            }
        } else {
            for (int i = 0; i < order; i++) {
                in_vec[i] = state[j * 8 - order + i];
            }
        }

        // Load residuals for this 8-sample block.
        for (int i = 0; i < 8; i++) {
            in_vec[i + order] = ix[j * 8 + i];
        }

        // Compute 8 output samples via the predictor inner product.
        for (int i = 0; i < 8; i++) {
            s32 sample = inner_product(order + 8, te[i], in_vec);
            state[i + j * 8] = sample;
            if (sample < -0x7fff) sample = -0x7fff;
            if (sample >  0x7fff) sample =  0x7fff;
            dst[i + j * 8] = (s16)sample;
        }
    }
}

s16 *vadpcm_decode(const u8 *adpcm, int adpcm_bytes, const VadpcmBook *book, int *out_n_samples) {
    if (!adpcm || !book || adpcm_bytes <= 0) {
        if (out_n_samples) *out_n_samples = 0;
        return NULL;
    }

    int n_frames  = adpcm_bytes / VADPCM_BYTES_PER_FRAME;
    int n_samples = n_frames * VADPCM_SAMPLES_PER_FRAME;

    s16 *pcm = malloc((size_t)n_samples * sizeof(s16));
    if (!pcm) {
        if (out_n_samples) *out_n_samples = 0;
        return NULL;
    }

    s32 state[16] = {0};
    for (int f = 0; f < n_frames; f++) {
        vadpcm_decode_frame(adpcm + f * VADPCM_BYTES_PER_FRAME, book, state,
                            pcm + f * VADPCM_SAMPLES_PER_FRAME);
    }

    if (out_n_samples) *out_n_samples = n_samples;
    return pcm;
}
