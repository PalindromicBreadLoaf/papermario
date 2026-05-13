#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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
static volatile bool s_cache_invalidated;

void texture_cache_init(void) {
    memset(s_cache, 0, sizeof(s_cache));
    s_cache_invalidated = false;
}

static unsigned int cache_hash(const u8 *addr, u8 fmt, u8 siz, u32 size_bytes,
                                const u8 *tlut, u16 width, u16 height, u8 cms, u8 cmt) {
    uintptr_t h = (uintptr_t)addr * 2654435761u;
    h ^= (uintptr_t)tlut * 40503u;
    h ^= (uintptr_t)size_bytes * 2246822519u;
    h ^= (uintptr_t)((u32)fmt | ((u32)siz << 8) | ((u32)width << 16) | ((u32)height << 24))
         * 3266489917u;
    h ^= (uintptr_t)((u32)cms | ((u32)cmt << 8)) * 668265263u;
    return (unsigned int)(h & (TEX_CACHE_SLOTS - 1u));
}

static GLenum wrap_mode(u8 flag) {
    if (flag & G_TX_CLAMP)  return GL_CLAMP_TO_EDGE;
    if (flag & G_TX_MIRROR) return GL_MIRRORED_REPEAT;
    return GL_REPEAT;
}

static bool ptr_range_readable(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;
    uintptr_t covered = start;
    char line[256];
    FILE *maps;

    if (ptr == NULL || size == 0 || end < start) {
        return false;
    }

    maps = fopen("/proc/self/maps", "r");
    if (maps == NULL) {
        return true;
    }

    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long long map_start;
        unsigned long long map_end;
        char perms[5];

        if (sscanf(line, "%llx-%llx %4s", &map_start, &map_end, perms) != 3) {
            continue;
        }

        if (perms[0] != 'r' || map_end <= covered || map_start > covered) {
            continue;
        }

        covered = (uintptr_t)map_end;
        if (covered >= end) {
            fclose(maps);
            return true;
        }
    }

    fclose(maps);
    return false;
}

unsigned int texture_cache_get(const u8 *addr, u8 fmt, u8 siz,
                                u32 size_bytes, const u8 *tlut,
                                u16 width, u16 height,
                                u8 cms, u8 cmt) {
    if (s_cache_invalidated) {
        texture_cache_flush();
    }

    if (!addr || !size_bytes || !width || !height) return 0;
    if (!ptr_range_readable(addr, size_bytes)) return 0;
    if (fmt == G_IM_FMT_CI && !ptr_range_readable(tlut, 0x200)) return 0;

    unsigned int idx = cache_hash(addr, fmt, siz, size_bytes, tlut, width, height, cms, cmt);

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
            e->width == width && e->height == height &&
            e->cms == cms && e->cmt == cmt) {
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
    s_cache_invalidated = false;
}

void texture_cache_invalidate_all(void) {
    s_cache_invalidated = true;
    for (int i = 0; i < 2; i++) {
        g_rdp.loaded[i].tex_id = 0;
        g_rdp.textures_dirty[i] = true;
    }
}
