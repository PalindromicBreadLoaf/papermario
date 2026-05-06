#ifndef TEX_CONV_H
#define TEX_CONV_H

#include <PR/ultratypes.h>

typedef enum {
    TEX_CONV_OK = 0,
    TEX_CONV_ERR_UNSUPPORTED,
    TEX_CONV_ERR_NULL_TLUT,
} TexConvResult;

/// Returns bytes required for the RGBA8 output of a given N64 texture.
u32 tex_conv_rgba8_size(u8 fmt, u8 siz, u32 src_bytes);

/// Convert an N64 texture to RGBA8. dst must be at least tex_conv_rgba8_size bytes.
/// tlut is required for G_IM_FMT_CI; pass NULL for all other formats.
TexConvResult tex_conv_to_rgba8(u8 fmt, u8 siz, const u8 *src, u32 src_bytes,
                                const u8 *tlut, u8 *dst);

#endif /* TEX_CONV_H */
