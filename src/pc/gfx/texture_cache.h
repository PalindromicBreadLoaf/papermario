#ifndef TEXTURE_CACHE_H
#define TEXTURE_CACHE_H

#include <PR/ultratypes.h>

// Initialise the cache. Must be called after the GL context exists.
void texture_cache_init(void);

// Look up or create a GL texture for the given N64 texture data.
// Returns the GL texture object name (unsigned int), or 0 on failure.
// tlut: required for CI formats; NULL for all others.
// cms/cmt: tile wrap/clamp flags from TileDesc.
unsigned int texture_cache_get(const u8 *addr, u8 fmt, u8 siz,
                                u32 size_bytes, const u8 *tlut,
                                u8 cms, u8 cmt);

// Release all cached GL textures (call on context reset or scene change).
void texture_cache_flush(void);

#endif /* TEXTURE_CACHE_H */
