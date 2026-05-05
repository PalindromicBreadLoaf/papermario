#ifndef TEX_CONV_H
#define TEX_CONV_H

#include <PR/ultratypes.h>

typedef enum {
    TEX_CONV_OK = 0,
    TEX_CONV_ERR_UNSUPPORTED,
    TEX_CONV_ERR_NULL_TLUT,
} TexConvResult;

/// Returns the required destination buffer size in bytes for tex_conv_to_rgba8.
u32 tex_conv_rgba8_size(u8 fmt, u8 siz, u32 src_bytes);

/// Converts raw N64 texture data to packed RGBA8888 (one byte per channel).
///
/// @param fmt       G_IM_FMT_* constant from PR/gbi.h
/// @param siz       G_IM_SIZ_* constant from PR/gbi.h
/// @param src       source texel data (big-endian, as DMA'd from ROM)
/// @param src_bytes number of bytes in src
/// @param tlut      RGBA5551 palette in big-endian byte order; required for
///                  CI formats, ignored (may be NULL) for all other formats
/// @param dst       caller-allocated output; must hold tex_conv_rgba8_size() bytes
TexConvResult tex_conv_to_rgba8(u8 fmt, u8 siz, const u8 *src, u32 src_bytes,
                                const u8 *tlut, u8 *dst);

#endif /* TEX_CONV_H */
