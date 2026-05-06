#include <stdlib.h>
#include <string.h>
#include "texture_cache.h"
#include "tex_conv.h"
#include "rdp_state.h"
#include "glad/gl.h"

#define TEX_CACHE_SLOTS 512u

typedef struct {
    const u8 *addr;
    u8        fmt;
    u8        siz;
    u32       size_bytes;
    const u8 *tlut;
    u16       width;
    u16       height;
    u8        cms;
    u8        cmt;
    GLuint    tex_id;
} TexCacheEntry;

static TexCacheEntry s_cache[TEX_CACHE_SLOTS];

void texture_cache_init(void) {
    memset(s_cache, 0, sizeof(s_cache));
}

static unsigned int cache_hash(const u8 *addr, u8 fmt, u8 siz, u32 size_bytes,
                                const u8 *tlut, u16 width, u16 height) {
    uintptr_t h = (uintptr_t)addr * 2654435761u;
    h ^= (uintptr_t)tlut * 40503u;
    h ^= (uintptr_t)size_bytes * 2246822519u;
    h ^= (uintptr_t)((u32)fmt | ((u32)siz << 8) | ((u32)width << 16) | ((u32)height << 24))
         * 3266489917u;
    return (unsigned int)(h & (TEX_CACHE_SLOTS - 1u));
}

static GLenum wrap_mode(u8 flag) {
    if (flag & G_TX_CLAMP)  return GL_CLAMP_TO_EDGE;
    if (flag & G_TX_MIRROR) return GL_MIRRORED_REPEAT;
    return GL_REPEAT;
}

unsigned int texture_cache_get(const u8 *addr, u8 fmt, u8 siz,
                                u32 size_bytes, const u8 *tlut,
                                u16 width, u16 height,
                                u8 cms, u8 cmt) {
    if (!addr || !size_bytes || !width || !height) return 0;

    unsigned int idx = cache_hash(addr, fmt, siz, size_bytes, tlut, width, height);

    unsigned int free_slot = TEX_CACHE_SLOTS;
    for (unsigned int i = 0; i < TEX_CACHE_SLOTS; i++) {
        unsigned int slot = (idx + i) & (TEX_CACHE_SLOTS - 1u);
        TexCacheEntry *e = &s_cache[slot];

        if (e->tex_id == 0) {
            if (free_slot == TEX_CACHE_SLOTS) free_slot = slot;
            break;
        }

        if (e->addr == addr && e->fmt == fmt && e->siz == siz &&
            e->size_bytes == size_bytes && e->tlut == tlut &&
            e->width == width && e->height == height) {
            return e->tex_id;
        }
    }

    if (free_slot == TEX_CACHE_SLOTS) {
        // Table full: evict at the home slot.
        free_slot = idx;
        glDeleteTextures(1, &s_cache[free_slot].tex_id);
        s_cache[free_slot].tex_id = 0;
    }

    u32 dst_bytes = tex_conv_rgba8_size(fmt, siz, size_bytes);
    u8 *buf = (u8 *)malloc(dst_bytes);
    if (!buf) return 0;

    if (tex_conv_to_rgba8(fmt, siz, addr, size_bytes, tlut, buf) != TEX_CONV_OK) {
        free(buf);
        return 0;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, buf);
    free(buf);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap_mode(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap_mode(cmt));

    GLenum filter = ((g_rdp.other_mode_h & (3u << G_MDSFT_TEXTFILT)) == (u32)G_TF_POINT)
                    ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);

    TexCacheEntry *e = &s_cache[free_slot];
    e->addr       = addr;
    e->fmt        = fmt;
    e->siz        = siz;
    e->size_bytes = size_bytes;
    e->tlut       = tlut;
    e->width      = width;
    e->height     = height;
    e->cms        = cms;
    e->cmt        = cmt;
    e->tex_id     = tex;

    return tex;
}

void texture_cache_flush(void) {
    for (unsigned int i = 0; i < TEX_CACHE_SLOTS; i++) {
        if (s_cache[i].tex_id) {
            glDeleteTextures(1, &s_cache[i].tex_id);
        }
    }
    memset(s_cache, 0, sizeof(s_cache));
}
