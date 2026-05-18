#ifndef TEXTURE_CACHE_H
#define TEXTURE_CACHE_H

#include <PR/ultratypes.h>

// Initialise the cache. Must be called after the GL context exists.
void texture_cache_init(void);

// Look up or create a GL texture for the given N64 texture data.
// Returns the GL texture object name (unsigned int), or 0 on failure.
// tlut: required for CI formats; NULL for all others.
// width/height: dimensions in texels (from the tile descriptor).
// cms/cmt: tile wrap/clamp flags from TileDesc.
unsigned int texture_cache_get(const u8 *addr, u8 fmt, u8 siz,
                                u32 size_bytes, u32 stride_bytes,
                                const u8 *tlut,
                                u16 width, u16 height,
                                u8 cms, u8 cmt);

// Release all cached GL textures (call on context reset or scene change).
void texture_cache_flush(void);

// Mark cached GL textures stale after CPU-side texture memory is overwritten.
// The actual GL deletion is deferred until the next render-thread cache lookup.
void texture_cache_invalidate_all(void);

#endif /* TEXTURE_CACHE_H */
