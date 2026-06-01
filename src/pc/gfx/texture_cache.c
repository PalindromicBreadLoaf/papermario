#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "texture_cache.h"
#include "tex_conv.h"
#include "rdp_state.h"
#include "gbi_interpreter.h"
#include "glad/gl.h"

#define TEX_CACHE_SLOTS 512u

typedef struct {
    const u8 *addr;
    u8        fmt;
    u8        siz;
    u32       size_bytes;
    u32       stride_bytes;
    u32       data_hash;
    const u8 *tlut;
    u32       tlut_hash;
    u16       width;
    u16       height;
    u8        cms;
    u8        cmt;
    u8        masks;
    u8        maskt;
    bool      linear_filter;
    bool      alpha_edge_bleed;
    GLuint    tex_id;
} TexCacheEntry;

static TexCacheEntry s_cache[TEX_CACHE_SLOTS];
static volatile bool s_cache_invalidated;
static int s_debug_logging = -1;

static bool texture_cache_debug_enabled(void) {
    if (s_debug_logging < 0) {
        const char *env = getenv("PM_TEXCACHE_DEBUG");
        s_debug_logging = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }
    return s_debug_logging != 0;
}

#define TEXCACHE_DEBUG_LOG(...) \
    do { \
        if (texture_cache_debug_enabled()) fprintf(stderr, __VA_ARGS__); \
    } while (0)

void texture_cache_init(void) {
    memset(s_cache, 0, sizeof(s_cache));
    s_cache_invalidated = false;
}

static u32 hash_bytes(const u8 *data, u32 size) {
    u32 hash = 2166136261u;

    for (u32 i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static u32 hash_texture_rows(const u8 *data, u32 row_bytes, u32 stride_bytes, u16 height) {
    u32 hash = 2166136261u;

    for (u16 y = 0; y < height; y++) {
        const u8 *row = data + (u32)y * stride_bytes;

        for (u32 x = 0; x < row_bytes; x++) {
            hash ^= row[x];
            hash *= 16777619u;
        }
    }
    return hash;
}

static unsigned int cache_hash(const u8 *addr, u8 fmt, u8 siz, u32 size_bytes, u32 stride_bytes,
                                const u8 *tlut, u32 tlut_hash, u16 width, u16 height, u8 cms, u8 cmt,
                                u8 masks, u8 maskt, bool linear_filter, bool alpha_edge_bleed) {
    uintptr_t h = (uintptr_t)addr * 2654435761u;
    h ^= (uintptr_t)tlut * 40503u;
    h ^= (uintptr_t)tlut_hash * 2654435761u;
    h ^= (uintptr_t)size_bytes * 2246822519u;
    h ^= (uintptr_t)stride_bytes * 374761393u;
    h ^= (uintptr_t)((u32)fmt | ((u32)siz << 8) | ((u32)width << 16) | ((u32)height << 24))
         * 3266489917u;
    h ^= (uintptr_t)((u32)cms | ((u32)cmt << 8)) * 668265263u;
    h ^= (uintptr_t)((u32)masks | ((u32)maskt << 8)) * 2246822519u;
    h ^= (uintptr_t)((linear_filter ? 1u : 0u) | (alpha_edge_bleed ? 2u : 0u)) * 3266489917u;
    return (unsigned int)(h & (TEX_CACHE_SLOTS - 1u));
}

static void bleed_transparent_edges(u8 *rgba, u16 width, u16 height) {
    u32 texels = (u32)width * (u32)height;
    u8 *src = (u8 *)malloc(texels * 4u);

    if (src == NULL) {
        return;
    }

    memcpy(src, rgba, texels * 4u);

    for (u16 y = 0; y < height; y++) {
        for (u16 x = 0; x < width; x++) {
            u32 idx = ((u32)y * width + x) * 4u;
            u32 r = 0;
            u32 g = 0;
            u32 b = 0;
            u32 count = 0;

            if (src[idx + 3] != 0) {
                continue;
            }

            for (s32 dy = -1; dy <= 1; dy++) {
                s32 ny = (s32)y + dy;

                if (ny < 0 || ny >= height) {
                    continue;
                }

                for (s32 dx = -1; dx <= 1; dx++) {
                    s32 nx = (s32)x + dx;
                    u32 nidx;

                    if ((dx == 0 && dy == 0) || nx < 0 || nx >= width) {
                        continue;
                    }

                    nidx = ((u32)ny * width + (u32)nx) * 4u;
                    if (src[nidx + 3] == 0) {
                        continue;
                    }

                    r += src[nidx + 0];
                    g += src[nidx + 1];
                    b += src[nidx + 2];
                    count++;
                }
            }

            if (count != 0) {
                rgba[idx + 0] = (u8)(r / count);
                rgba[idx + 1] = (u8)(g / count);
                rgba[idx + 2] = (u8)(b / count);
            }
        }
    }

    free(src);
}

static GLenum wrap_mode(u8 flag, u8 mask) {
    if (mask == G_TX_NOMASK) return GL_CLAMP_TO_EDGE;
    if (flag & G_TX_CLAMP)  return GL_CLAMP_TO_EDGE;
    if (flag & G_TX_MIRROR) return GL_MIRRORED_REPEAT;
    return GL_REPEAT;
}

static u32 tex_row_bytes(u8 siz, u16 width) {
    switch (siz) {
        case G_IM_SIZ_4b:  return ((u32)width + 1u) / 2u;
        case G_IM_SIZ_8b:  return width;
        case G_IM_SIZ_16b: return (u32)width * 2u;
        case G_IM_SIZ_32b: return (u32)width * 4u;
        default:           return 0;
    }
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
                                u32 size_bytes, u32 stride_bytes,
                                const u8 *tlut,
                                u16 width, u16 height,
                                u8 cms, u8 cmt,
                                u8 masks, u8 maskt) {
    if (s_cache_invalidated) {
        texture_cache_flush();
    }

    if (!addr || !size_bytes || !width || !height) {
        TEXCACHE_DEBUG_LOG("[texcache] reject zero-arg addr=%p size=%u w=%u h=%u fmt=%u siz=%u\n",
                           (const void *)addr, size_bytes, (unsigned)width, (unsigned)height,
                           (unsigned)fmt, (unsigned)siz);
        return 0;
    }
    u32 row_bytes = tex_row_bytes(siz, width);
    if (!row_bytes) {
        TEXCACHE_DEBUG_LOG("[texcache] reject row_bytes=0 siz=%u w=%u\n", (unsigned)siz, (unsigned)width);
        return 0;
    }
    if (stride_bytes == 0) {
        stride_bytes = row_bytes;
    }
    if (stride_bytes < row_bytes) {
        TEXCACHE_DEBUG_LOG("[texcache] reject stride<row stride=%u row=%u\n", stride_bytes, row_bytes);
        return 0;
    }
    u32 packed_bytes = row_bytes * (u32)height;
    if (size_bytes < packed_bytes) {
        u16 actual_h = (u16)(size_bytes / row_bytes);
        if (actual_h == 0) {
            TEXCACHE_DEBUG_LOG("[texcache] reject size<row size=%u row=%u w=%u h=%u fmt=%u siz=%u\n",
                               size_bytes, row_bytes, (unsigned)width, (unsigned)height,
                               (unsigned)fmt, (unsigned)siz);
            return 0;
        }
        height = actual_h;
        packed_bytes = row_bytes * (u32)height;
    }
    u32 footprint = row_bytes + ((u32)height - 1u) * stride_bytes;
    if (!ptr_range_readable(addr, footprint)) {
        TEXCACHE_DEBUG_LOG("[texcache] reject addr-unreadable addr=%p footprint=%u w=%u h=%u fmt=%u siz=%u\n",
                           (const void *)addr, footprint, (unsigned)width, (unsigned)height,
                           (unsigned)fmt, (unsigned)siz);
        return 0;
    }
    if (fmt == G_IM_FMT_CI) {
        u32 tlut_bytes = siz == G_IM_SIZ_4b ? 0x20u : 0x200u;

        if (!ptr_range_readable(tlut, tlut_bytes)) {
            TEXCACHE_DEBUG_LOG("[texcache] reject tlut-unreadable tlut=%p bytes=%u addr=%p siz=%u\n",
                               (const void *)tlut, tlut_bytes, (const void *)addr, (unsigned)siz);
            return 0;
        }
    }
    u32 tlut_hash = fmt == G_IM_FMT_CI
        ? hash_bytes(tlut, siz == G_IM_SIZ_4b ? 0x20u : 0x200u)
        : 0u;
    u32 data_hash = hash_texture_rows(addr, row_bytes, stride_bytes, height);
    bool linear_filter = ((g_rdp.other_mode_h & (3u << G_MDSFT_TEXTFILT)) != (u32)G_TF_POINT);
    bool alpha_edge_bleed = linear_filter && ((g_rdp.other_mode_l & (CVG_X_ALPHA | FORCE_BL)) != 0);

    unsigned int idx = cache_hash(addr, fmt, siz, packed_bytes, stride_bytes, tlut, tlut_hash, width, height, cms, cmt,
                                  masks, maskt, linear_filter, alpha_edge_bleed);

    unsigned int free_slot = TEX_CACHE_SLOTS;
    unsigned int replace_slot = TEX_CACHE_SLOTS;
    for (unsigned int i = 0; i < TEX_CACHE_SLOTS; i++) {
        unsigned int slot = (idx + i) & (TEX_CACHE_SLOTS - 1u);
        TexCacheEntry *e = &s_cache[slot];

        if (e->tex_id == 0) {
            if (free_slot == TEX_CACHE_SLOTS) free_slot = slot;
            break;
        }

        if (e->addr == addr && e->fmt == fmt && e->siz == siz &&
            e->size_bytes == packed_bytes && e->stride_bytes == stride_bytes &&
            e->tlut == tlut && e->tlut_hash == tlut_hash &&
            e->width == width && e->height == height &&
            e->cms == cms && e->cmt == cmt &&
            e->masks == masks && e->maskt == maskt &&
            e->linear_filter == linear_filter &&
            e->alpha_edge_bleed == alpha_edge_bleed) {
            if (e->data_hash == data_hash) {
                return e->tex_id;
            }
            replace_slot = slot;
            break;
        }
    }

    if (replace_slot != TEX_CACHE_SLOTS) {
        free_slot = replace_slot;
        glDeleteTextures(1, &s_cache[free_slot].tex_id);
        s_cache[free_slot].tex_id = 0;
    } else if (free_slot == TEX_CACHE_SLOTS) {
        // Table full: evict at the home slot.
        free_slot = idx;
        glDeleteTextures(1, &s_cache[free_slot].tex_id);
        s_cache[free_slot].tex_id = 0;
    }

    const u8 *src = addr;
    u8 *packed = NULL;
    if (stride_bytes != row_bytes) {
        packed = (u8 *)malloc(packed_bytes);
        if (!packed) return 0;
        for (u32 y = 0; y < height; y++) {
            memcpy(packed + y * row_bytes, addr + y * stride_bytes, row_bytes);
        }
        src = packed;
    }

    u32 dst_bytes = tex_conv_rgba8_size(fmt, siz, packed_bytes);
    u8 *buf = (u8 *)malloc(dst_bytes);
    if (!buf) {
        free(packed);
        return 0;
    }

    if (tex_conv_to_rgba8(fmt, siz, src, packed_bytes, tlut, buf) != TEX_CONV_OK) {
        TEXCACHE_DEBUG_LOG("[texcache] reject conv-fail fmt=%u siz=%u src=%p tlut=%p bytes=%u w=%u h=%u\n",
                           (unsigned)fmt, (unsigned)siz, (const void *)src, (const void *)tlut,
                           packed_bytes, (unsigned)width, (unsigned)height);
        free(packed);
        free(buf);
        return 0;
    }

    if (alpha_edge_bleed) {
        bleed_transparent_edges(buf, width, height);
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, buf);
    free(packed);
    free(buf);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap_mode(cms, masks));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap_mode(cmt, maskt));

    GLenum filter = linear_filter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);

    TexCacheEntry *e = &s_cache[free_slot];
    e->addr       = addr;
    e->fmt        = fmt;
    e->siz        = siz;
    e->size_bytes = packed_bytes;
    e->stride_bytes = stride_bytes;
    e->data_hash = data_hash;
    e->tlut       = tlut;
    e->tlut_hash  = tlut_hash;
    e->width      = width;
    e->height     = height;
    e->cms        = cms;
    e->cmt        = cmt;
    e->masks      = masks;
    e->maskt      = maskt;
    e->linear_filter = linear_filter;
    e->alpha_edge_bleed = alpha_edge_bleed;
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
    gbi_invalidate_texture_bindings();
}

void texture_cache_invalidate_all(void) {
    s_cache_invalidated = true;
    gbi_invalidate_texture_bindings();
    for (int i = 0; i < GFX_RDP_TILE_COUNT; i++) {
        g_rdp.loaded_tiles[i].tex_id = 0;
        g_rdp.tile_dirty[i] = true;
    }
    for (int i = 0; i < GFX_SHADER_TEXTURES; i++) {
        g_rdp.loaded[i].tex_id = 0;
    }
}
