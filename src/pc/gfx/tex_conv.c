#include <string.h>
#include <PR/gbi.h>
#include "tex_conv.h"

// Expand n-bit unsigned value to 8 bits by replication/scale.
#define SCALE_5_8(v) (((v) * 255) / 31)
#define SCALE_4_8(v) ((v) * 0x11)
#define SCALE_3_8(v) ((v) * 0x24)

// Unpack the i-th 4-bit nibble from src (high nibble first).
#define NIBBLE(src, i) (((src)[(i) / 2] >> (((i) % 2) == 0 ? 4 : 0)) & 0xF)

// Decode one RGBA5551 big-endian word from a palette at index idx.
static void tlut_entry(const u8 *tlut, u32 idx, u8 *r, u8 *g, u8 *b, u8 *a) {
    u16 col = ((u16)tlut[idx * 2] << 8) | tlut[idx * 2 + 1];
    *r = SCALE_5_8((col >> 11) & 0x1F);
    *g = SCALE_5_8((col >>  6) & 0x1F);
    *b = SCALE_5_8((col >>  1) & 0x1F);
    *a = (col & 1) ? 255 : 0;
}

static void conv_rgba16(const u8 *src, u32 src_bytes, u8 *dst) {
    u32 n = src_bytes / 2;
    for (u32 i = 0; i < n; i++) {
        u16 col = ((u16)src[2 * i] << 8) | src[2 * i + 1];
        dst[4 * i + 0] = SCALE_5_8((col >> 11) & 0x1F);
        dst[4 * i + 1] = SCALE_5_8((col >>  6) & 0x1F);
        dst[4 * i + 2] = SCALE_5_8((col >>  1) & 0x1F);
        dst[4 * i + 3] = (col & 1) ? 255 : 0;
    }
}

static void conv_rgba32(const u8 *src, u32 src_bytes, u8 *dst) {
    memcpy(dst, src, src_bytes);
}

static void conv_ia4(const u8 *src, u32 src_bytes, u8 *dst) {
    u32 n = src_bytes * 2;
    for (u32 i = 0; i < n; i++) {
        u8 nibble    = NIBBLE(src, i);
        u8 intensity = SCALE_3_8(nibble >> 1);
        u8 alpha     = (nibble & 1) ? 255 : 0;
        dst[4 * i + 0] = intensity;
        dst[4 * i + 1] = intensity;
        dst[4 * i + 2] = intensity;
        dst[4 * i + 3] = alpha;
    }
}

static void conv_ia8(const u8 *src, u32 src_bytes, u8 *dst) {
    for (u32 i = 0; i < src_bytes; i++) {
        u8 intensity = SCALE_4_8(src[i] >> 4);
        u8 alpha     = SCALE_4_8(src[i] & 0xF);
        dst[4 * i + 0] = intensity;
        dst[4 * i + 1] = intensity;
        dst[4 * i + 2] = intensity;
        dst[4 * i + 3] = alpha;
    }
}

static void conv_ia16(const u8 *src, u32 src_bytes, u8 *dst) {
    u32 n = src_bytes / 2;
    for (u32 i = 0; i < n; i++) {
        u8 intensity = src[2 * i];
        u8 alpha     = src[2 * i + 1];
        dst[4 * i + 0] = intensity;
        dst[4 * i + 1] = intensity;
        dst[4 * i + 2] = intensity;
        dst[4 * i + 3] = alpha;
    }
}

// I-format alpha matches intensity on N64.
static void conv_i4(const u8 *src, u32 src_bytes, u8 *dst) {
    u32 n = src_bytes * 2;
    for (u32 i = 0; i < n; i++) {
        u8 intensity = SCALE_4_8(NIBBLE(src, i));
        dst[4 * i + 0] = intensity;
        dst[4 * i + 1] = intensity;
        dst[4 * i + 2] = intensity;
        dst[4 * i + 3] = intensity;
    }
}

static void conv_i8(const u8 *src, u32 src_bytes, u8 *dst) {
    for (u32 i = 0; i < src_bytes; i++) {
        dst[4 * i + 0] = src[i];
        dst[4 * i + 1] = src[i];
        dst[4 * i + 2] = src[i];
        dst[4 * i + 3] = src[i];
    }
}

static void conv_ci4(const u8 *src, u32 src_bytes, const u8 *tlut, u8 *dst) {
    u32 n = src_bytes * 2;
    for (u32 i = 0; i < n; i++) {
        u8 idx = NIBBLE(src, i);
        tlut_entry(tlut, idx, &dst[4*i+0], &dst[4*i+1], &dst[4*i+2], &dst[4*i+3]);
    }
}

static void conv_ci8(const u8 *src, u32 src_bytes, const u8 *tlut, u8 *dst) {
    for (u32 i = 0; i < src_bytes; i++) {
        tlut_entry(tlut, src[i], &dst[4*i+0], &dst[4*i+1], &dst[4*i+2], &dst[4*i+3]);
    }
}

u32 tex_conv_rgba8_size(u8 fmt, u8 siz, u32 src_bytes) {
    (void)fmt;
    switch (siz) {
        case G_IM_SIZ_4b:  return src_bytes * 8;
        case G_IM_SIZ_8b:  return src_bytes * 4;
        case G_IM_SIZ_16b: return src_bytes * 2;
        case G_IM_SIZ_32b: return src_bytes;
        default:           return 0;
    }
}

TexConvResult tex_conv_to_rgba8(u8 fmt, u8 siz, const u8 *src, u32 src_bytes,
                                const u8 *tlut, u8 *dst) {
    switch (fmt) {
        case G_IM_FMT_RGBA:
            if (siz == G_IM_SIZ_16b) { conv_rgba16(src, src_bytes, dst);        return TEX_CONV_OK; }
            if (siz == G_IM_SIZ_32b) { conv_rgba32(src, src_bytes, dst);        return TEX_CONV_OK; }
            break;
        case G_IM_FMT_IA:
            if (siz == G_IM_SIZ_4b)  { conv_ia4(src, src_bytes, dst);           return TEX_CONV_OK; }
            if (siz == G_IM_SIZ_8b)  { conv_ia8(src, src_bytes, dst);           return TEX_CONV_OK; }
            if (siz == G_IM_SIZ_16b) { conv_ia16(src, src_bytes, dst);          return TEX_CONV_OK; }
            break;
        case G_IM_FMT_I:
            if (siz == G_IM_SIZ_4b)  { conv_i4(src, src_bytes, dst);            return TEX_CONV_OK; }
            if (siz == G_IM_SIZ_8b)  { conv_i8(src, src_bytes, dst);            return TEX_CONV_OK; }
            break;
        case G_IM_FMT_CI:
            if (!tlut)                return TEX_CONV_ERR_NULL_TLUT;
            if (siz == G_IM_SIZ_4b)  { conv_ci4(src, src_bytes, tlut, dst);     return TEX_CONV_OK; }
            if (siz == G_IM_SIZ_8b)  { conv_ci8(src, src_bytes, tlut, dst);     return TEX_CONV_OK; }
            break;
        default:
            break;
    }
    return TEX_CONV_ERR_UNSUPPORTED;
}
