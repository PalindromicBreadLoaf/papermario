#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "glad/gl.h"
#include "gbi_interpreter.h"
#include "rdp_state.h"
#include "gl_backend.h"
#include "texture_cache.h"

void* pc_tlb_translate(void* vaddr);
void* pc_resolve_physical_addr(uintptr_t addr);
extern u8 gMapShapeData[];
extern u8 gPcShapeArena[];
extern u8 heap_collisionHead[];
extern u16 gFrameBuf0[];
extern u16 gFrameBuf1[];
extern u16 gFrameBuf2[];
extern u16 SpriteShadingPalette[16];
extern u32 gPcMapShapeDataSize;
extern u32 gPcMapShapePayloadShift;

// Bit-field extractors for GBI command words.
#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1u << (width)) - 1u))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1u << (width)) - 1u))
#define PC_SHAPE_KSEG_BASE  0x80210000u
#define PC_SHAPE_PHYS_BASE  0x00210000u
#define PC_SHAPE_HEADER_SIZE 0x20u
#define PC_SHAPE_SIZE_LIMIT 0x40000u
#define PC_COLLISION_HEAP_SIZE 0x18000u
#define PC_FRAMEBUFFER_WIDTH 320u
#define PC_FRAMEBUFFER_HEIGHT 240u
#define PC_FRAMEBUFFER_BYTES (PC_FRAMEBUFFER_WIDTH * PC_FRAMEBUFFER_HEIGHT * sizeof(u16))

static unsigned int s_texel0_id;
static unsigned int s_texel1_id;

extern bool gfx_trace_state;

static const u8 *gfx_tlut_for_tile(const TileDesc *td);

static inline u32 bswap32(u32 value) {
    return ((value & 0x000000FFu) << 24)
         | ((value & 0x0000FF00u) << 8)
         | ((value & 0x00FF0000u) >> 8)
         | ((value & 0xFF000000u) >> 24);
}

static inline u16 read_be16(const void *ptr) {
    const u8 *p = ptr;

    return (u16)(((u16)p[0] << 8) | p[1]);
}

static inline s16 read_be_s16(const void *ptr) {
    return (s16)read_be16(ptr);
}

static uintptr_t gfx_shape_data_size(void) {
    return gPcMapShapeDataSize != 0 ? gPcMapShapeDataSize : PC_SHAPE_SIZE_LIMIT;
}

static uintptr_t gfx_shape_source_size(void) {
    if (gPcMapShapeDataSize > gPcMapShapePayloadShift) {
        return gPcMapShapeDataSize - gPcMapShapePayloadShift;
    }
    return PC_SHAPE_SIZE_LIMIT;
}

static uintptr_t gfx_shape_offset(uintptr_t offset) {
    if (offset >= PC_SHAPE_HEADER_SIZE) {
        return offset + gPcMapShapePayloadShift;
    }
    return offset;
}

static bool ptr_in_map_shape(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)gPcShapeArena;
    uintptr_t shape_size = gfx_shape_data_size();

    return start >= base && size <= shape_size && start - base <= shape_size - size;
}

static bool gfx_is_sign_extended_32(uintptr_t addr) {
#if UINTPTR_MAX > 0xffffffffu
    return addr >= UINT64_C(0xffffffff80000000);
#else
    (void)addr;
    return false;
#endif
}

static bool gfx_is_guest_kseg_addr(uintptr_t addr) {
    u32 low = (u32)addr;

    return (addr <= 0xffffffffu || gfx_is_sign_extended_32(addr))
        && low >= 0x80000000u
        && low < 0xC0000000u;
}

static bool gbi_opcode_known(u8 opcode) {
    switch (opcode) {
        case G_NOOP:
        case G_SPNOOP:
        case G_ENDDL:
        case G_DL:
        case G_RDPHALF_1:
        case G_RDPHALF_2:
        case G_VTX:
        case G_MODIFYVTX:
        case G_CULLDL:
        case G_BRANCH_Z:
        case G_LINE3D:
        case G_TRI1:
        case G_TRI2:
        case G_QUAD:
        case G_MTX:
        case G_POPMTX:
        case G_GEOMETRYMODE:
        case G_TEXTURE:
        case G_MOVEMEM:
        case G_MOVEWORD:
        case G_LOAD_UCODE:
        case G_SETOTHERMODE_H:
        case G_SETOTHERMODE_L:
        case G_SETTIMG:
        case G_SETTILE:
        case G_LOADTILE:
        case G_LOADBLOCK:
        case G_SETTILESIZE:
        case G_LOADTLUT:
        case G_SETPRIMDEPTH:
        case G_SETCOMBINE:
        case G_SETENVCOLOR:
        case G_SETPRIMCOLOR:
        case G_SETBLENDCOLOR:
        case G_SETFOGCOLOR:
        case G_SETFILLCOLOR:
        case G_FILLRECT:
        case G_TEXRECT:
        case G_TEXRECTFLIP:
        case G_SETSCISSOR:
        case G_SETCIMG:
        case G_SETZIMG:
        case G_RDPFULLSYNC:
        case G_RDPTILESYNC:
        case G_RDPPIPESYNC:
        case G_RDPLOADSYNC:
            return true;
        default:
            return false;
    }
}

static bool gfx_ptr_range_readable(const void *ptr, size_t size);

static u16 gfx_pack_rgba5551(u8 r, u8 g, u8 b, u8 a) {
    return (u16)((((u16)(r >> 3) & 0x1Fu) << 11)
               | (((u16)(g >> 3) & 0x1Fu) << 6)
               | (((u16)(b >> 3) & 0x1Fu) << 1)
               | (a >= 128 ? 1u : 0u));
}

static void gfx_write_be16(u8 *dst, int index, u16 value) {
    dst[index * 2 + 0] = (u8)(value >> 8);
    dst[index * 2 + 1] = (u8)value;
}

static bool gfx_try_write_sprite_shading_palette(u8 tile) {
    const u8 *source_palette = g_rdp.tlut_pal[0] != NULL ? g_rdp.tlut_pal[0] : g_rdp.tlut;
    u8 *dst = (u8 *)g_rdp.color_buf_addr;

    if (tile != 2 || dst != (u8 *)SpriteShadingPalette) {
        return false;
    }
    if (!gfx_ptr_range_readable(source_palette, 0x20u)) {
        return false;
    }

    for (int i = 0; i < 16; i++) {
        u16 source = read_be16(source_palette + i * 2);
        bool opaque = (source & 1u) != 0;
        u8 r = opaque ? g_rdp.prim_r : g_rdp.env_r;
        u8 g = opaque ? g_rdp.prim_g : g_rdp.env_g;
        u8 b = opaque ? g_rdp.prim_b : g_rdp.env_b;

        gfx_write_be16(dst, i, gfx_pack_rgba5551(r, g, b, 255));
    }
    return true;
}

static bool gfx_addr_in_framebuffer(const u8 *addr) {
    const u8 *framebuffers[] = {
        (const u8 *)gFrameBuf0,
        (const u8 *)gFrameBuf1,
        (const u8 *)gFrameBuf2,
    };
    uintptr_t target = (uintptr_t)addr;

    for (size_t i = 0; i < sizeof(framebuffers) / sizeof(framebuffers[0]); i++) {
        uintptr_t start = (uintptr_t)framebuffers[i];
        uintptr_t end = start + PC_FRAMEBUFFER_BYTES;

        if (target >= start && target < end) {
            return true;
        }
    }
    return false;
}

// Decode one display-list command and report its byte stride.
// PC command words are pointer-sized. Decompressed map shape display lists
// remain packed as 8-byte N64 commands, with w0/w1 in the low/high halves
static Gfx gbi_read_cmd(const Gfx *src, int *stride_bytes) {
    Gfx cmd = *src;
    u32 packed_w0 = (u32)cmd.words.w0;
    u32 packed_w1;
    u8 opcode = (u8)(cmd.words.w0 >> 24);
    u8 swapped_opcode = (u8)cmd.words.w0;

#if UINTPTR_MAX > 0xffffffffu
    packed_w1 = (u32)(cmd.words.w0 >> 32);
#else
    packed_w1 = (u32)cmd.words.w1;
#endif

    if (ptr_in_map_shape(src, sizeof(u32) * 2)) {
        opcode = (u8)(packed_w0 >> 24);
        if (gbi_opcode_known(opcode)) {
            cmd.words.w0 = packed_w0;
            cmd.words.w1 = packed_w1;
            *stride_bytes = (int)(sizeof(u32) * 2);
            return cmd;
        }

        swapped_opcode = (u8)packed_w0;
        if (gbi_opcode_known(swapped_opcode)) {
            cmd.words.w0 = bswap32(packed_w0);
            cmd.words.w1 = bswap32(packed_w1);
            *stride_bytes = (int)(sizeof(u32) * 2);
            return cmd;
        }
    }

    if (!gbi_opcode_known(opcode) && gbi_opcode_known(swapped_opcode)) {
        cmd.words.w0 = bswap32(packed_w0);
        cmd.words.w1 = bswap32(packed_w1);
        *stride_bytes = 8;
    } else {
        *stride_bytes = (int)sizeof(Gfx);
    }
    return cmd;
}

static bool gfx_dl_has_end(const Gfx *dl, size_t max_bytes) {
    const u8 *pos = (const u8 *)dl;
    const u8 *end = pos + max_bytes;

    while (pos < end) {
        int stride;
        Gfx cmd;
        u8 opcode;

        if (!gfx_ptr_range_readable(pos, sizeof(u32) * 2)) {
            return false;
        }

        cmd = gbi_read_cmd((const Gfx *)pos, &stride);
        opcode = (u8)(cmd.words.w0 >> 24);
        if (!gbi_opcode_known(opcode) || stride <= 0) {
            return false;
        }
        if (opcode == G_ENDDL) {
            return true;
        }
        pos += stride;
    }

    return false;
}

static void *gfx_try_shape_shadow_dl_addr(uintptr_t addr) {
    uintptr_t collision_start = (uintptr_t)heap_collisionHead;
    uintptr_t collision_end = collision_start + PC_COLLISION_HEAP_SIZE;
    uintptr_t offset;
    uintptr_t source_size;
    u8 *candidate;

    if (addr < collision_start || addr >= collision_end) {
        return NULL;
    }

    offset = addr - collision_start;
    source_size = gfx_shape_source_size();
    if (offset >= source_size) {
        return NULL;
    }

    candidate = gPcShapeArena + gfx_shape_offset(offset);
    if (!ptr_in_map_shape(candidate, sizeof(u32) * 2)) {
        return NULL;
    }

    if (!gfx_dl_has_end((const Gfx *)candidate, 0x1000)
            || gfx_dl_has_end((const Gfx *)addr, 0x1000)) {
        return NULL;
    }

    return candidate;
}

static void *gfx_default_segment_base(u8 segment) {
    if (segment == 1) {
        return ((void **)gPcShapeArena)[1];
    }
    return NULL;
}

// Cache readable process ranges so display-list pointer checks do not parse
// /proc/self/maps for every command.
typedef struct {
    uintptr_t start;
    uintptr_t end;
} PcReadableRange;

#define PC_READABLE_MAX_RANGES 256
static PcReadableRange s_readable_ranges[PC_READABLE_MAX_RANGES];
static int             s_readable_range_count = 0;

static void gfx_readable_refresh_locked(void) {
    char line[256];
    FILE *maps;

    s_readable_range_count = 0;
    maps = fopen("/proc/self/maps", "r");
    if (maps == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long long map_start;
        unsigned long long map_end;
        char perms[5];

        if (sscanf(line, "%llx-%llx %4s", &map_start, &map_end, perms) != 3) {
            continue;
        }
        if (perms[0] != 'r') {
            continue;
        }

        if (s_readable_range_count > 0
                && s_readable_ranges[s_readable_range_count - 1].end == (uintptr_t)map_start) {
            s_readable_ranges[s_readable_range_count - 1].end = (uintptr_t)map_end;
            continue;
        }

        if (s_readable_range_count >= PC_READABLE_MAX_RANGES) {
            break;
        }
        s_readable_ranges[s_readable_range_count].start = (uintptr_t)map_start;
        s_readable_ranges[s_readable_range_count].end   = (uintptr_t)map_end;
        s_readable_range_count++;
    }

    fclose(maps);
}

static bool gfx_readable_range_lookup(uintptr_t start, uintptr_t end) {
    int lo = 0;
    int hi = s_readable_range_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (s_readable_ranges[mid].end <= start) {
            lo = mid + 1;
        } else if (s_readable_ranges[mid].start > start) {
            hi = mid - 1;
        } else {
            return end <= s_readable_ranges[mid].end;
        }
    }
    return false;
}

static bool gfx_ptr_range_readable(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;

    if (ptr == NULL || size == 0 || end < start) {
        return false;
    }

    if (s_readable_range_count == 0) {
        gfx_readable_refresh_locked();
    }
    if (gfx_readable_range_lookup(start, end)) {
        return true;
    }
    // Cache miss: the mapping table is older than this pointer. Refresh and
    // retry once. If it's still unknown, the pointer is genuinely bad.
    gfx_readable_refresh_locked();
    return gfx_readable_range_lookup(start, end);
}

static void *gfx_resolve_addr(uintptr_t addr) {
    uintptr_t guest_addr = gfx_is_sign_extended_32(addr) ? (uintptr_t)(u32)addr : addr;
    uintptr_t resolved = guest_addr;
    bool is_guest_kseg = gfx_is_guest_kseg_addr(addr);
    u8 segment = (u8)(guest_addr >> 24);
    void *default_base;

    // Host pointers into the shape arena are already resolved.
    uintptr_t shape_start = (uintptr_t)gPcShapeArena;
    uintptr_t shape_size = gfx_shape_data_size();
    if (addr >= shape_start && addr < shape_start + shape_size) {
        return (void *)addr;
    }

    if (!is_guest_kseg) {
        // Host display lists may carry full pointers or low-32 physical values.
        // Skip this for guest KSEG addresses.
        void *host_ptr = pc_resolve_physical_addr(addr);
        if (gfx_ptr_range_readable(host_ptr, 1)) {
            return host_ptr;
        }
    }

    default_base = gfx_default_segment_base(segment);

    if (default_base != NULL) {
        return (u8 *)default_base + (guest_addr & 0x00FFFFFFu);
    }

    if (segment < 16 && g_rsp.segments[segment] != NULL) {
        return (u8 *)g_rsp.segments[segment] + (guest_addr & 0x00FFFFFFu);
    }

    uintptr_t shape_source_size = gfx_shape_source_size();
    if (guest_addr >= PC_SHAPE_KSEG_BASE && guest_addr - PC_SHAPE_KSEG_BASE < shape_source_size) {
        return gPcShapeArena + gfx_shape_offset(guest_addr - PC_SHAPE_KSEG_BASE);
    }

    if (guest_addr >= PC_SHAPE_PHYS_BASE && guest_addr - PC_SHAPE_PHYS_BASE < shape_source_size) {
        return gPcShapeArena + gfx_shape_offset(guest_addr - PC_SHAPE_PHYS_BASE);
    }

    if (is_guest_kseg) {
        resolved = (uintptr_t)((u32)guest_addr + 0x80000000u);
    }

    void *tlb_ptr = pc_tlb_translate((void *)resolved);

    if (tlb_ptr != (void *)resolved) {
        return tlb_ptr;
    }
    if (is_guest_kseg) {
        return (void *)resolved;
    }
    return pc_resolve_physical_addr(resolved);
}

// True when an address falls inside the collision (hit) heap.
// Should never be true, but there was a crash related to this.
static bool gfx_addr_in_collision_heap(uintptr_t addr) {
    uintptr_t start = (uintptr_t)heap_collisionHead;

    return addr >= start && addr < start + PC_COLLISION_HEAP_SIZE;
}

static bool gfx_dl_target_renderable(const void *target) {
    if (target == NULL) {
        return false;
    }
    if (gfx_addr_in_collision_heap((uintptr_t)target)) {
        return false;
    }
    return gfx_ptr_range_readable(target, sizeof(u32) * 2);
}

// Row-major 4x4 multiply. res may alias a or b.
static void gfx_matrix_mul(float res[4][4], const float a[4][4], const float b[4][4]) {
    float tmp[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] +
                        a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(res, tmp, sizeof(tmp));
}

static void gfx_sp_matrix(u8 params, const s32 *addr) {
    float matrix[4][4];

    // The load reads 16 s32s (64 bytes), so bail if that range
    // is not mapped rather than faulting.
    if (!gfx_ptr_range_readable(addr, 16 * sizeof(s32))) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            s32 int_part  = addr[i * 2 + j / 2];
            u32 frac_part = (u32)addr[8 + i * 2 + j / 2];
            matrix[i][j]     = (s32)((int_part  & 0xffff0000u) | (frac_part >> 16))         / 65536.0f;
            matrix[i][j + 1] = (s32)(((u32)int_part << 16)     | (frac_part & 0x0000ffffu)) / 65536.0f;
        }
    }

    if (params & G_MTX_PROJECTION) {
        if (params & G_MTX_LOAD) {
            memcpy(g_rsp.P_matrix, matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(g_rsp.P_matrix, matrix, g_rsp.P_matrix);
        }
    } else {
        if ((params & G_MTX_PUSH) && g_rsp.modelview_depth < 11) {
            memcpy(g_rsp.modelview_stack[g_rsp.modelview_depth],
                   g_rsp.modelview_stack[g_rsp.modelview_depth - 1],
                   sizeof(matrix));
            g_rsp.modelview_depth++;
        }
        if (params & G_MTX_LOAD) {
            memcpy(g_rsp.modelview_stack[g_rsp.modelview_depth - 1], matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(g_rsp.modelview_stack[g_rsp.modelview_depth - 1],
                           matrix,
                           g_rsp.modelview_stack[g_rsp.modelview_depth - 1]);
        }
        g_rsp.lights_dirty = true;
    }
    gfx_matrix_mul(g_rsp.MP_matrix,
                   g_rsp.modelview_stack[g_rsp.modelview_depth - 1],
                   g_rsp.P_matrix);
}

static void gfx_sp_pop_matrix(u32 count) {
    while (count-- > 0 && g_rsp.modelview_depth > 1) {
        g_rsp.modelview_depth--;
    }
    gfx_matrix_mul(g_rsp.MP_matrix,
                   g_rsp.modelview_stack[g_rsp.modelview_depth - 1],
                   g_rsp.P_matrix);
}

static void gfx_normalize_vector(float v[3]) {
    float s = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (s > 0.0f) { v[0] /= s; v[1] /= s; v[2] /= s; }
}

static void gfx_clamp_num_lights(void) {
    if (g_rsp.num_lights < 1) {
        g_rsp.num_lights = 1;
    } else if (g_rsp.num_lights > GFX_MAX_LIGHTS + 1) {
        g_rsp.num_lights = GFX_MAX_LIGHTS + 1;
    }
}

static void gfx_transposed_matrix_mul(float res[3], const float a[3], const float b[4][4]) {
    res[0] = a[0]*b[0][0] + a[1]*b[0][1] + a[2]*b[0][2];
    res[1] = a[0]*b[1][0] + a[1]*b[1][1] + a[2]*b[1][2];
    res[2] = a[0]*b[2][0] + a[1]*b[2][1] + a[2]*b[2][2];
}

static void gfx_update_light_coeffs(void) {
    gfx_clamp_num_lights();

    for (int i = 0; i < g_rsp.num_lights - 1; i++) {
        float dir[3] = {
            g_rsp.lights[i].dir[0] / 127.0f,
            g_rsp.lights[i].dir[1] / 127.0f,
            g_rsp.lights[i].dir[2] / 127.0f,
        };
        gfx_transposed_matrix_mul(g_rsp.light_coeffs[i], dir,
                                  g_rsp.modelview_stack[g_rsp.modelview_depth - 1]);
        gfx_normalize_vector(g_rsp.light_coeffs[i]);
    }
    static const float lookat_x[3] = {1.0f, 0.0f, 0.0f};
    static const float lookat_y[3] = {0.0f, 1.0f, 0.0f};
    gfx_transposed_matrix_mul(g_rsp.lookat_coeffs[0], lookat_x,
                              g_rsp.modelview_stack[g_rsp.modelview_depth - 1]);
    gfx_transposed_matrix_mul(g_rsp.lookat_coeffs[1], lookat_y,
                              g_rsp.modelview_stack[g_rsp.modelview_depth - 1]);
    gfx_normalize_vector(g_rsp.lookat_coeffs[0]);
    gfx_normalize_vector(g_rsp.lookat_coeffs[1]);
    g_rsp.lights_dirty = false;
}

static void gfx_sp_vertex(int n, int dest, const Vtx *src) {
    gfx_clamp_num_lights();

    if (n <= 0 || dest < 0 || dest + n > GFX_MAX_VERTICES || !gfx_ptr_range_readable(src, (size_t)n * sizeof(*src))) {
        return;
    }

    if (g_rsp.lights_dirty && (g_rsp.geometry_mode & G_LIGHTING)) {
        gfx_update_light_coeffs();
    }

    for (int i = 0; i < n; i++) {
        const Vtx_t  *v  = &src[i].v;
        const Vtx_tn *vn = &src[i].n;
        LoadedVertex *d  = &g_rsp.loaded_vertices[dest + i];
        bool be_vertex = ptr_in_map_shape(v, sizeof(*v));
        s16 ob0 = be_vertex ? read_be_s16(&v->ob[0]) : v->ob[0];
        s16 ob1 = be_vertex ? read_be_s16(&v->ob[1]) : v->ob[1];
        s16 ob2 = be_vertex ? read_be_s16(&v->ob[2]) : v->ob[2];
        s16 tc0 = be_vertex ? read_be_s16(&v->tc[0]) : v->tc[0];
        s16 tc1 = be_vertex ? read_be_s16(&v->tc[1]) : v->tc[1];

        float x = ob0 * g_rsp.MP_matrix[0][0] + ob1 * g_rsp.MP_matrix[1][0] +
                  ob2 * g_rsp.MP_matrix[2][0] + g_rsp.MP_matrix[3][0];
        float y = ob0 * g_rsp.MP_matrix[0][1] + ob1 * g_rsp.MP_matrix[1][1] +
                  ob2 * g_rsp.MP_matrix[2][1] + g_rsp.MP_matrix[3][1];
        float z = ob0 * g_rsp.MP_matrix[0][2] + ob1 * g_rsp.MP_matrix[1][2] +
                  ob2 * g_rsp.MP_matrix[2][2] + g_rsp.MP_matrix[3][2];
        float w = ob0 * g_rsp.MP_matrix[0][3] + ob1 * g_rsp.MP_matrix[1][3] +
                  ob2 * g_rsp.MP_matrix[2][3] + g_rsp.MP_matrix[3][3];
        d->x = x; d->y = y; d->z = z; d->w = w;

        d->u = (float)((tc0 * (s32)g_rsp.tex_scale.s) >> 16);
        d->v = (float)((tc1 * (s32)g_rsp.tex_scale.t) >> 16);

        if (g_rsp.geometry_mode & G_LIGHTING) {
            gfx_clamp_num_lights();

            int cr = g_rsp.lights[g_rsp.num_lights - 1].col[0];
            int cg = g_rsp.lights[g_rsp.num_lights - 1].col[1];
            int cb = g_rsp.lights[g_rsp.num_lights - 1].col[2];
            for (int j = 0; j < g_rsp.num_lights - 1; j++) {
                float intensity = (vn->n[0] * g_rsp.light_coeffs[j][0] +
                                   vn->n[1] * g_rsp.light_coeffs[j][1] +
                                   vn->n[2] * g_rsp.light_coeffs[j][2]) / 127.0f;
                if (intensity > 0.0f) {
                    cr += (int)(intensity * g_rsp.lights[j].col[0]);
                    cg += (int)(intensity * g_rsp.lights[j].col[1]);
                    cb += (int)(intensity * g_rsp.lights[j].col[2]);
                }
            }
            d->r = (u8)(cr > 255 ? 255 : cr);
            d->g = (u8)(cg > 255 ? 255 : cg);
            d->b = (u8)(cb > 255 ? 255 : cb);

            if (g_rsp.geometry_mode & G_TEXTURE_GEN) {
                float dotx = vn->n[0] * g_rsp.lookat_coeffs[0][0] +
                             vn->n[1] * g_rsp.lookat_coeffs[0][1] +
                             vn->n[2] * g_rsp.lookat_coeffs[0][2];
                float doty = vn->n[0] * g_rsp.lookat_coeffs[1][0] +
                             vn->n[1] * g_rsp.lookat_coeffs[1][1] +
                             vn->n[2] * g_rsp.lookat_coeffs[1][2];
                d->u = (dotx / 127.0f + 1.0f) / 4.0f * g_rsp.tex_scale.s;
                d->v = (doty / 127.0f + 1.0f) / 4.0f * g_rsp.tex_scale.t;
            }
        } else {
            d->r = v->cn[0]; d->g = v->cn[1]; d->b = v->cn[2];
        }

        if (g_rsp.geometry_mode & G_FOG) {
            float fw = (fabsf(w) < 0.001f) ? 0.001f : w;
            float fz = (z / fw) * g_rsp.fog_mul + g_rsp.fog_offset;
            if (fz < 0.0f) fz = 0.0f;
            if (fz > 255.0f) fz = 255.0f;
            d->a = (u8)fz;
        } else {
            d->a = v->cn[3];
        }

        d->clip_rej = 0;
        if (x < -w) d->clip_rej |= 1;
        if (x >  w) d->clip_rej |= 2;
        if (y < -w) d->clip_rej |= 4;
        if (y >  w) d->clip_rej |= 8;
        if (z < -w) d->clip_rej |= 16;
        if (z >  w) d->clip_rej |= 32;
    }
}

// Apply current other_mode_l to the GL depth and blend state
// Must be called after gfx_flush()
static bool gfx_blender_cycle_passthrough(u32 oml, int cycle) {
    u32 clr_in_1;
    u32 a_in_1;
    u32 clr_in_2;
    u32 a_in_2;

    if (cycle == 2) {
        clr_in_1 = (oml >> 28) & 3u;
        a_in_1 = (oml >> 24) & 3u;
        clr_in_2 = (oml >> 20) & 3u;
        a_in_2 = (oml >> 16) & 3u;
    } else {
        clr_in_1 = (oml >> 30) & 3u;
        a_in_1 = (oml >> 26) & 3u;
        clr_in_2 = (oml >> 22) & 3u;
        a_in_2 = (oml >> 18) & 3u;
    }

    return clr_in_1 == G_BL_CLR_IN && a_in_1 == G_BL_0
        && clr_in_2 == G_BL_CLR_IN && a_in_2 == G_BL_1;
}

static bool gfx_uses_alpha_blend(u32 oml) {
    u32 cycle_type = g_rdp.other_mode_h & (3u << G_MDSFT_CYCLETYPE);

    if ((oml & FORCE_BL) == 0) {
        return false;
    }

    return !gfx_blender_cycle_passthrough(oml, cycle_type == G_CYC_2CYCLE ? 2 : 1);
}

static bool gfx_uses_decal_depth(u32 oml) {
    return (oml & ZMODE_DEC) == ZMODE_DEC;
}

static void gfx_apply_render_state(void) {
    u32 oml = g_rdp.other_mode_l;
    bool z_compare = (oml & Z_CMP) != 0;
    bool z_update = (oml & Z_UPD) != 0;
    bool z_buffer = (g_rsp.geometry_mode & G_ZBUFFER) != 0;

    if (z_buffer && (z_compare || z_update)) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(z_compare ? GL_LEQUAL : GL_ALWAYS);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    glDepthMask(z_update ? GL_TRUE : GL_FALSE);

    if (gfx_uses_decal_depth(oml)) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    bool texture_edge = (oml & CVG_X_ALPHA) != 0;
    bool use_alpha = texture_edge || gfx_uses_alpha_blend(oml);

    if (use_alpha) {
        gfx_alpha_test = texture_edge ? 2 : (z_update ? 0 : 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        gfx_alpha_test = 1;
        glDisable(GL_BLEND);
    }
}

static float gfx_prim_depth_ndc(void) {
    return ((float)g_rdp.prim_depth / (float)G_MAXFBZ) * 2.0f - 1.0f;
}

// If other_mode_l changed since the last draw flush then switch
static void gfx_ensure_blend_state(void) {
    if (!g_rdp.blend_dirty) return;
    gfx_flush();
    g_rdp.blend_dirty = false;
    gfx_apply_render_state();
}

static u16 gfx_tile_width(const TileDesc *td) {
    return (td->lrs > td->uls) ? (u16)((td->lrs - td->uls) / 4 + 1) : 1;
}

static u16 gfx_tile_height(const TileDesc *td) {
    return (td->lrt > td->ult) ? (u16)((td->lrt - td->ult) / 4 + 1) : 1;
}

static float gfx_tile_shift_scale(u8 shift) {
    if (shift == 0) {
        return 1.0f;
    }
    if (shift <= 10) {
        return 1.0f / (float)(1u << shift);
    }
    return (float)(1u << (16u - shift));
}

static u32 gfx_bytes_to_texels(u8 siz, u32 bytes) {
    switch (siz) {
        case G_IM_SIZ_4b:  return bytes * 2u;
        case G_IM_SIZ_8b:  return bytes;
        case G_IM_SIZ_16b: return bytes / 2u;
        case G_IM_SIZ_32b: return bytes / 4u;
        default:           return bytes / 2u;
    }
}

static TmemTextureLoad *gfx_find_tmem_load(u32 tmem) {
    TmemTextureLoad *best = NULL;

    for (int i = 0; i < GFX_RDP_TILE_COUNT; i++) {
        TmemTextureLoad *load = &g_rdp.tmem_loads[i];

        if (!load->valid || tmem < load->tmem_offset) {
            continue;
        }

        u32 delta_bytes = (tmem - load->tmem_offset) * 8u;

        if (delta_bytes >= load->size_bytes) {
            continue;
        }
        if (best == NULL || load->tmem_offset > best->tmem_offset) {
            best = load;
        }
    }

    return best;
}

static TmemTextureLoad *gfx_tmem_load_slot(u32 tmem) {
    for (int i = 0; i < GFX_RDP_TILE_COUNT; i++) {
        if (g_rdp.tmem_loads[i].valid && g_rdp.tmem_loads[i].tmem_offset == tmem) {
            return &g_rdp.tmem_loads[i];
        }
    }

    for (int i = 0; i < GFX_RDP_TILE_COUNT; i++) {
        if (!g_rdp.tmem_loads[i].valid) {
            return &g_rdp.tmem_loads[i];
        }
    }

    return &g_rdp.tmem_loads[0];
}

static void gfx_store_tmem_load(u32 tmem, const u8 *addr, u32 size_bytes, u32 stride_bytes, u16 width, u16 height) {
    TmemTextureLoad *load = gfx_tmem_load_slot(tmem);

    load->addr = addr;
    load->size_bytes = size_bytes;
    load->stride_bytes = stride_bytes;
    load->width = width;
    load->height = height;
    load->tmem_offset = tmem;
    load->framebuffer_copy = gfx_addr_in_framebuffer(addr);
    load->valid = true;
}

static void gfx_assign_tmem_load_to_tile(u8 tile, u32 tmem) {
    TmemTextureLoad *load = gfx_find_tmem_load(tmem);

    if (tile >= GFX_RDP_TILE_COUNT || load == NULL) {
        return;
    }

    u32 byte_offset = (tmem - load->tmem_offset) * 8u;
    LoadedTexture *loaded = &g_rdp.loaded_tiles[tile];

    loaded->addr = load->addr + byte_offset;
    loaded->size_bytes = load->size_bytes - byte_offset;
    loaded->stride_bytes = load->stride_bytes;
    loaded->width = load->width;
    loaded->height = load->height;
    loaded->tile = tile;
    loaded->framebuffer_copy = load->framebuffer_copy;
    loaded->tex_id = 0;
    g_rdp.tile_dirty[tile] = true;
}

static void gfx_upload_tile(u8 tile) {
    if (tile >= GFX_RDP_TILE_COUNT || !g_rdp.tile_dirty[tile]) {
        return;
    }

    g_rdp.tile_dirty[tile] = false;

    LoadedTexture *loaded = &g_rdp.loaded_tiles[tile];
    TileDesc *td = &g_rdp.tile[tile];
    u16 width = gfx_tile_width(td);
    u16 height = gfx_tile_height(td);
    
    if (loaded->stride_bytes > 0) {
        u16 data_width = (u16)gfx_bytes_to_texels(td->siz, loaded->stride_bytes);
        if (data_width > 0 && data_width < width) {
            width = data_width;
        }
    }

    loaded->tile = tile;
    loaded->width = width;
    loaded->height = height;
    loaded->tex_id = 0;

    if (loaded->framebuffer_copy) {
        unsigned int tex_id = gl_backend_previous_frame_texture();

        if (tex_id != 0) {
            loaded->width = PC_FRAMEBUFFER_WIDTH;
            loaded->height = PC_FRAMEBUFFER_HEIGHT;
            loaded->tex_id = tex_id;
        }
        return;
    }

    if (!loaded->addr || !loaded->size_bytes) {
        return;
    }

    const u8 *tlut = gfx_tlut_for_tile(td);

    unsigned int tex_id = texture_cache_get(loaded->addr,
                                            td->fmt, td->siz,
                                            loaded->size_bytes,
                                            loaded->stride_bytes,
                                            tlut, width, height,
                                            td->cms, td->cmt,
                                            td->masks, td->maskt);
    if (tex_id) {
        loaded->tex_id = tex_id;
    }
}

static void gfx_bind_active_textures(void) {
    // TEXEL0 samples the active render tile; TEXEL1 samples the following tile.
    unsigned int tex0_id = g_rdp.loaded[0].tex_id;
    unsigned int tex1_id = g_rdp.loaded[1].tex_id;

    if (s_texel0_id == tex0_id && s_texel1_id == tex1_id) {
        return;
    }

    gfx_flush();
    s_texel0_id = tex0_id;
    s_texel1_id = tex1_id;

    if (tex0_id != 0) {
        gfx_bind_texture(0, tex0_id);
    }
    if (tex1_id != 0) {
        gfx_bind_texture(1, tex1_id);
    }

    // Let tex1 remain valid even when tex0 is empty.
    gfx_use_tex = (tex0_id != 0 ? 1 : 0) | (tex1_id != 0 ? 2 : 0);
}

static void gfx_prepare_active_textures(void) {
    u8 tex_tiles[GFX_SHADER_TEXTURES];
    u8 base_tile = g_rdp.active_texture_tile < GFX_RDP_TILE_COUNT ? g_rdp.active_texture_tile : 0;
    bool needs_flush = false;

    tex_tiles[0] = base_tile;
    tex_tiles[1] = (u8)((base_tile + 1u) % GFX_RDP_TILE_COUNT);

    for (int i = 0; i < GFX_SHADER_TEXTURES; i++) {
        u8 tile = tex_tiles[i];

        if (g_rdp.bound_texture_tile[i] != tile || g_rdp.tile_dirty[tile]) {
            needs_flush = true;
        }
    }

    if (needs_flush) {
        gfx_flush();
    }

    for (int i = 0; i < GFX_SHADER_TEXTURES; i++) {
        u8 tile = tex_tiles[i];

        gfx_upload_tile(tile);
        g_rdp.loaded[i] = g_rdp.loaded_tiles[tile];
        g_rdp.bound_texture_tile[i] = tile;
    }

    gfx_bind_active_textures();
}

static void gfx_ensure_viewport(void) {
    if (!g_rdp.viewport_dirty) return;
    gfx_flush();
    g_rdp.viewport_dirty = false;
    glViewport((GLint)g_rdp.viewport.x, (GLint)g_rdp.viewport.y,
               (GLsizei)g_rdp.viewport.w, (GLsizei)g_rdp.viewport.h);
    if (g_rdp.scissor.w > 0.0f && g_rdp.scissor.h > 0.0f) {
        glScissor((GLint)g_rdp.scissor.x, (GLint)g_rdp.scissor.y,
                  (GLsizei)g_rdp.scissor.w, (GLsizei)g_rdp.scissor.h);
    }
}

static void gfx_sp_tri1(u8 v0, u8 v1, u8 v2) {
    LoadedVertex *lv[3] = {
        &g_rsp.loaded_vertices[v0],
        &g_rsp.loaded_vertices[v1],
        &g_rsp.loaded_vertices[v2],
    };

    if (lv[0]->clip_rej & lv[1]->clip_rej & lv[2]->clip_rej) return;

    if (g_rsp.geometry_mode & G_CULL_BOTH) {
        float dx1 = lv[0]->x / lv[0]->w - lv[1]->x / lv[1]->w;
        float dy1 = lv[0]->y / lv[0]->w - lv[1]->y / lv[1]->w;
        float dx2 = lv[2]->x / lv[2]->w - lv[1]->x / lv[1]->w;
        float dy2 = lv[2]->y / lv[2]->w - lv[1]->y / lv[1]->w;
        float cross = dx1 * dy2 - dy1 * dx2;

        if (((lv[0]->w < 0) ? 1 : 0) ^ ((lv[1]->w < 0) ? 1 : 0) ^ ((lv[2]->w < 0) ? 1 : 0)) {
            cross = -cross;
        }

        switch (g_rsp.geometry_mode & G_CULL_BOTH) {
            case G_CULL_FRONT: if (cross <= 0.0f) return; break;
            case G_CULL_BACK:  if (cross >= 0.0f) return; break;
            case G_CULL_BOTH:  return;
        }
    }

    gfx_ensure_viewport();
    gfx_ensure_blend_state();

    gfx_prepare_active_textures();
    if (gfx_buf_vbo_num_tris == GFX_MAX_BUFFERED) gfx_flush();

    float linear_offset = ((g_rdp.other_mode_h & (3u << G_MDSFT_TEXTFILT)) == (u32)G_TF_POINT) ? 0.0f : 16.0f;

    // Build UVs per TMEM slot because TEXEL0 and TEXEL1 can use different tile origins.
    float inv_w[2], inv_h[2], uls_s105[2], ult_s105[2], shift_s[2], shift_t[2];
    for (int s = 0; s < 2; s++) {
        u8 t = g_rdp.loaded[s].tile < 8 ? g_rdp.loaded[s].tile : (u8)s;
        const TileDesc *tds = &g_rdp.tile[t];
        float w = (float)g_rdp.loaded[s].width;
        float h = (float)g_rdp.loaded[s].height;
        if (w <= 0.0f && tds->lrs >= tds->uls) {
            w = (float)((u32)(tds->lrs - tds->uls) / 4u + 1u);
        }
        if (h <= 0.0f && tds->lrt >= tds->ult) {
            h = (float)((u32)(tds->lrt - tds->ult) / 4u + 1u);
        }
        if (g_rdp.loaded[s].framebuffer_copy) {
            inv_w[s] = 1.0f / (PC_FRAMEBUFFER_WIDTH * 32.0f);
            inv_h[s] = 1.0f / (PC_FRAMEBUFFER_HEIGHT * 32.0f);
            uls_s105[s] = 0.0f;
            ult_s105[s] = 0.0f;
            shift_s[s] = 1.0f;
            shift_t[s] = 1.0f;
        } else {
            inv_w[s]    = w > 0.0f ? 1.0f / (w * 32.0f) : 1.0f;
            inv_h[s]    = h > 0.0f ? 1.0f / (h * 32.0f) : 1.0f;
            uls_s105[s] = (float)tds->uls * 8.0f;
            ult_s105[s] = (float)tds->ult * 8.0f;
            shift_s[s]  = gfx_tile_shift_scale(tds->shifts);
            shift_t[s]  = gfx_tile_shift_scale(tds->shiftt);
        }
    }

    for (int i = 0; i < 3; i++) {
        float u0 = (lv[i]->u * shift_s[0] - uls_s105[0] + linear_offset) * inv_w[0];
        float v0 = (lv[i]->v * shift_t[0] - ult_s105[0] + linear_offset) * inv_h[0];
        float u1 = (lv[i]->u * shift_s[1] - uls_s105[1] + linear_offset) * inv_w[1];
        float v1 = (lv[i]->v * shift_t[1] - ult_s105[1] + linear_offset) * inv_h[1];
        float z = lv[i]->z;

        if (g_rdp.loaded[0].framebuffer_copy) {
            v0 = 1.0f - v0;
        }
        if (g_rdp.loaded[1].framebuffer_copy) {
            v1 = 1.0f - v1;
        }

        if (g_rdp.other_mode_l & G_ZS_PRIM) {
            z = gfx_prim_depth_ndc() * lv[i]->w;
        }

        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->x;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->y;
        gfx_buf_vbo[gfx_buf_vbo_len++] = z;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->w;
        gfx_buf_vbo[gfx_buf_vbo_len++] = u0;
        gfx_buf_vbo[gfx_buf_vbo_len++] = v0;
        gfx_buf_vbo[gfx_buf_vbo_len++] = u1;
        gfx_buf_vbo[gfx_buf_vbo_len++] = v1;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->r / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->g / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->b / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->a / 255.0f;
    }
    gfx_buf_vbo_num_tris++;
}

// Map G_CCMUX_* / G_ACMUX_* constants to the shader source-array index:
//   0=tex0    1=tex1    2=shade    3=prim    4=env    5=zero   6=one
//   7=tex0.a  8=tex1.a  9=shade.a  10=prim.a 11=env.a
//
// Unmodelled muxes fall back to zero or one.
static int cc_rgb_to_idx(int mux) {
    switch (mux) {
        case G_CCMUX_TEXEL0:          return 0;
        case G_CCMUX_TEXEL1:          return 1;
        case G_CCMUX_SHADE:           return 2;
        case G_CCMUX_PRIMITIVE:       return 3;
        case G_CCMUX_ENVIRONMENT:     return 4;
        // G_CCMUX_1 = G_CCMUX_CENTER = G_CCMUX_SCALE = 6.
        case 6:                       return 6;
        case G_CCMUX_TEXEL0_ALPHA:    return 7;
        case G_CCMUX_TEXEL1_ALPHA:    return 8;
        case G_CCMUX_SHADE_ALPHA:     return 9;
        case G_CCMUX_PRIMITIVE_ALPHA: return 10;
        case G_CCMUX_ENV_ALPHA:       return 11;
        default:                      return 5;  // zero
    }
}

static int cc_alpha_to_idx(int mux) {
    switch (mux) {
        case G_ACMUX_TEXEL0:      return 0;
        case G_ACMUX_TEXEL1:      return 1;
        case G_ACMUX_SHADE:       return 2;
        case G_ACMUX_PRIMITIVE:   return 3;
        case G_ACMUX_ENVIRONMENT: return 4;
        // G_ACMUX_1 = G_ACMUX_PRIM_LOD_FRAC = 6.
        case 6:                   return 6;
        default:                  return 5;  // zero (includes G_ACMUX_0 = 7)
    }
}

static void gfx_sp_texture(const Gfx *cmd) {
    g_rsp.tex_scale.s = (u16)C1(16, 16);
    g_rsp.tex_scale.t = (u16)C1(0, 16);

    if ((cmd->words.w0 & 0xFFu) != 0) {
        u8 tile = (u8)C0(8, 3);

        g_rdp.active_texture_tile = tile;
    }
}

static u32 gfx_sp_tri1_word(const Gfx *cmd) {
    return (cmd->words.w0 & 0x00FFFFFFu) != 0 ? (u32)cmd->words.w0 : (u32)cmd->words.w1;
}

// TODO: GBI handlers
static void gfx_sp_modify_vertex(const Gfx *cmd)     { (void)cmd; }
static void gfx_sp_cull_dl(const Gfx *cmd)           { (void)cmd; }
static void gfx_sp_branch_z(const Gfx *cmd)          { (void)cmd; }

static void gfx_sp_geometry_mode(const Gfx *cmd) {
    // F3DEX2 encodes ~clear_mask in w0[23:0] and set_mask in w1.
    g_rsp.geometry_mode = (g_rsp.geometry_mode & (u32)C0(0, 24)) | cmd->words.w1;
}

static void gfx_sp_move_mem(const Gfx *cmd) {
    // F3DEX2 has index = w0[7:0], offset = w0[15:8] * 8, addr = w1.
    u8         index  = (u8)C0(0, 8);
    int        offset = (int)C0(8, 8) * 8;
    const void *data  = gfx_resolve_addr((uintptr_t)cmd->words.w1);

    if (index == G_MV_VIEWPORT) {
        const Vp_t *vp = (const Vp_t *)data;
        if (!gfx_ptr_range_readable(vp, sizeof(*vp))) {
            return;
        }
        float rx = (gl_window_width  > 0) ? (float)gl_window_width  / 320.0f : 2.0f;
        float ry = (gl_window_height > 0) ? (float)gl_window_height / 240.0f : 2.0f;
        float w  = 2.0f * vp->vscale[0] / 4.0f;
        float h  = 2.0f * vp->vscale[1] / 4.0f;
        float x  = vp->vtrans[0] / 4.0f - w / 2.0f;
        float y  = 240.0f - (vp->vtrans[1] / 4.0f + h / 2.0f);
        g_rdp.viewport.x = x * rx;
        g_rdp.viewport.y = y * ry;
        g_rdp.viewport.w = w * rx;
        g_rdp.viewport.h = h * ry;
        g_rdp.viewport_dirty = true;
    } else if (index == G_MV_LIGHT) {
        // offset 0 and 24 are the lookat entries; lights start at offset 48.
        int slot = offset / 24 - 2;
        if (slot >= 0 && slot <= GFX_MAX_LIGHTS && gfx_ptr_range_readable(data, sizeof(Light_t))) {
            memcpy(&g_rsp.lights[slot], data, sizeof(Light_t));
            g_rsp.lights_dirty = true;
        }
    }
}

static void gfx_sp_move_word(const Gfx *cmd) {
    // F3DEX2: index = w0[23:16], offset = w0[15:0], data = w1.
    u8  index = (u8)C0(16, 8);
    u32 data  = cmd->words.w1;

    switch (index) {
        case G_MW_NUMLIGHT:
            // F3DEX2: data = NUML(n) = n * 24; +1 to include ambient slot.
            g_rsp.num_lights = (int)(data / 24) + 1;
            if (g_rsp.num_lights < 1) {
                g_rsp.num_lights = 1;
            } else if (g_rsp.num_lights > GFX_MAX_LIGHTS + 1) {
                g_rsp.num_lights = GFX_MAX_LIGHTS + 1;
            }
            g_rsp.lights_dirty = true;
            break;
        case G_MW_FOG:
            g_rsp.fog_mul    = (s16)(data >> 16);
            g_rsp.fog_offset = (s16)(data & 0xFFFF);
            break;
        case G_MW_SEGMENT: {
            u32 segment = (u32)cmd->words.w0 & 0xFFFFu;

            segment /= 4u;
            if (segment < 16u) {
                void *default_base = gfx_default_segment_base((u8)segment);

                g_rsp.segments[segment] = default_base != NULL ? default_base : gfx_resolve_addr(data);
            }
            break;
        }
    }
}

static void gfx_rdp_set_other_mode_h(const Gfx *cmd) {
    u32 shift = 31u - C0(8, 8) - C0(0, 8);
    u32 count = C0(0, 8) + 1u;
    u32 mask  = ((1u << count) - 1u) << shift;
    u32 new_omh = (g_rdp.other_mode_h & ~mask) | (cmd->words.w1 & mask);
    extern bool gfx_trace_state;
    if (gfx_trace_state && (mask & (3u << G_MDSFT_CYCLETYPE))) {
        fprintf(stderr, "SOMH cycle: old=%u new=%u (shift=%u count=%u w1=0x%08X tris_buf=%d)\n",
                (g_rdp.other_mode_h >> G_MDSFT_CYCLETYPE) & 3,
                (new_omh >> G_MDSFT_CYCLETYPE) & 3,
                shift, count, (u32)cmd->words.w1, (int)gfx_buf_vbo_num_tris);
    }

    if ((new_omh ^ g_rdp.other_mode_h) & (3u << G_MDSFT_CYCLETYPE)) {
        gfx_flush();
    }
    g_rdp.other_mode_h = new_omh;
}

static void gfx_rdp_set_other_mode_l(const Gfx *cmd) {
    u32 shift = 31u - C0(8, 8) - C0(0, 8);
    u32 count = C0(0, 8) + 1u;
    u32 mask  = ((1u << count) - 1u) << shift;
    g_rdp.other_mode_l = (g_rdp.other_mode_l & ~mask) | (cmd->words.w1 & mask);
    g_rdp.blend_dirty  = true;
}

static void gfx_rdp_set_texture_image(const Gfx *cmd) {
    g_rdp.tex_to_load.fmt       = (u8)C0(21, 3);
    g_rdp.tex_to_load.siz       = (u8)C0(19, 2);
    g_rdp.tex_to_load.width     = (u16)(C0(0, 12) + 1u);
    g_rdp.tex_to_load.addr      = gfx_resolve_addr((uintptr_t)cmd->words.w1);
}

static void gfx_rdp_set_tile(const Gfx *cmd) {
    u8  fmt  = (u8)C0(21, 3);
    u8  siz  = (u8)C0(19, 2);
    u32 line = C0(9, 9);
    u32 tmem = C0(0, 9);
    int tile = (int)C1(24, 3);
    u8  palette = (u8)C1(20, 4);
    u8  cmt  = (u8)C1(18, 2);
    u8  maskt = (u8)C1(14, 4);
    u8  shiftt = (u8)C1(10, 4);
    u8  cms  = (u8)C1(8,  2);
    u8  masks = (u8)C1(4,  4);
    u8  shifts = (u8)C1(0,  4);

    g_rdp.tile[tile].fmt         = fmt;
    g_rdp.tile[tile].siz         = siz;
    g_rdp.tile[tile].line_bytes  = line * 8;
    g_rdp.tile[tile].tmem_offset = tmem;
    g_rdp.tile[tile].palette     = palette;
    g_rdp.tile[tile].cms         = cms;
    g_rdp.tile[tile].cmt         = cmt;
    g_rdp.tile[tile].masks       = masks;
    g_rdp.tile[tile].maskt       = maskt;
    g_rdp.tile[tile].shifts      = shifts;
    g_rdp.tile[tile].shiftt      = shiftt;

    if (tile == G_TX_LOADTILE) {
        g_rdp.tex_to_load.tmem_offset = tmem;
    } else {
        gfx_assign_tmem_load_to_tile((u8)tile, tmem);
        g_rdp.tile_dirty[tile] = true;
    }
}

// size_bytes shift per siz for G_LOADBLOCK (Thank you SM64-port)
// For each siz, lrs is in units of 16-bit TMEM words (except 4b uses bytes).
static u32 load_block_shift(u8 siz) {
    switch (siz) {
        case G_IM_SIZ_4b:  return 0;
        case G_IM_SIZ_8b:  return 1;
        case G_IM_SIZ_16b: return 1;
        case G_IM_SIZ_32b: return 2;
        default:           return 1;
    }
}

static void gfx_rdp_load_block(const Gfx *cmd) {
    u32 lrs     = C1(12, 12);
    u32 n_bytes = (lrs + 1u) << load_block_shift(g_rdp.tex_to_load.siz);

    // LoadBlock dimensions are finalised later from the render-tile descriptor.
    gfx_store_tmem_load(g_rdp.tex_to_load.tmem_offset, g_rdp.tex_to_load.addr, n_bytes, 0, 0, 0);
}

static u32 texels_to_bytes(u8 siz, u32 texels) {
    switch (siz) {
        case G_IM_SIZ_4b:  return (texels + 1u) / 2u;
        case G_IM_SIZ_8b:  return texels;
        case G_IM_SIZ_16b: return texels * 2u;
        case G_IM_SIZ_32b: return texels * 4u;
        default:           return texels * 2u;
    }
}

static void gfx_rdp_load_tile(const Gfx *cmd) {
    u32 uls  = C0(12, 12);
    u32 ult  = C0(0,  12);
    u32 lrs  = C1(12, 12);
    u32 lrt  = C1(0,  12);
    u32 src_x = uls / 4u;
    u32 src_y = ult / 4u;
    u32 w    = (lrs - uls) / 4u + 1u;
    u32 h    = (lrt - ult) / 4u + 1u;
    u32 img_w = g_rdp.tex_to_load.width;

    u32 row_bytes = (img_w > 0)
        ? texels_to_bytes(g_rdp.tex_to_load.siz, img_w)
        : texels_to_bytes(g_rdp.tex_to_load.siz, w);
    u32 leading_bytes = texels_to_bytes(g_rdp.tex_to_load.siz, src_x);
    u32 tile_row_bytes = texels_to_bytes(g_rdp.tex_to_load.siz, w);
    u32 byte_offset = src_y * row_bytes + leading_bytes;

    gfx_store_tmem_load(g_rdp.tex_to_load.tmem_offset,
                        g_rdp.tex_to_load.addr + byte_offset,
                        tile_row_bytes * h, row_bytes,
                        (u16)w, (u16)h);
}

static void gfx_rdp_set_tile_size(const Gfx *cmd) {
    int  tile = (int)C1(24, 3);
    u16  uls  = (u16)C0(12, 12);
    u16  ult  = (u16)C0(0,  12);
    u16  lrs  = (u16)C1(12, 12);
    u16  lrt  = (u16)C1(0,  12);

    g_rdp.tile[tile].uls = uls;
    g_rdp.tile[tile].ult = ult;
    g_rdp.tile[tile].lrs = lrs;
    g_rdp.tile[tile].lrt = lrt;

    if (tile < GFX_RDP_TILE_COUNT) {
        g_rdp.tile_dirty[tile] = true;
    }
}

static const u8 *gfx_tlut_for_tile(const TileDesc *td) {
    if (td->fmt != G_IM_FMT_CI) {
        return NULL;
    }

    if (td->siz == G_IM_SIZ_4b) {
        u8 palette = td->palette & 0xF;
        const u8 *result;
        const char *source;

        if (g_rdp.tlut_pal[palette] != NULL) {
            result = g_rdp.tlut_pal[palette];
            source = "tlut_pal";
        } else if (g_rdp.tlut != NULL) {
            result = g_rdp.tlut + palette * 0x20;
            source = "tlut+off";
        } else {
            result = NULL;
            source = "none";
        }

        if (getenv("PM_TRACE_TLUT")) {
            fprintf(stderr,
                    "[tlut] CI4 bank=%u src=%s result=%p tlut=%p tlut_pal[%u]=%p\n",
                    (unsigned)palette, source, (const void *)result,
                    (const void *)g_rdp.tlut, (unsigned)palette,
                    (const void *)g_rdp.tlut_pal[palette]);
        }
        return result;
    }

    if (getenv("PM_TRACE_TLUT")) {
        fprintf(stderr, "[tlut] CI8 result=%p\n", (const void *)g_rdp.tlut);
    }
    return g_rdp.tlut;
}

static void gfx_rdp_load_tlut(const Gfx *cmd) {
    u8 tile = (u8)C1(24, 3);
    u32 count = C1(14, 10) + 1u;
    TileDesc *td = &g_rdp.tile[tile];
    u32 first_palette = td->tmem_offset >= 256u ? (td->tmem_offset - 256u) / 16u : 0u;

    gfx_flush();

    if (count >= 256u) {
        g_rdp.tlut = g_rdp.tex_to_load.addr;
        for (u32 i = 0; i < 16u; i++) {
            g_rdp.tlut_pal[i] = g_rdp.tex_to_load.addr + i * 0x20u;
        }
    } else {
        for (u32 offset = 0; offset < count && first_palette < 16u; offset += 16u, first_palette++) {
            g_rdp.tlut_pal[first_palette] = g_rdp.tex_to_load.addr + offset * sizeof(u16);
        }
    }

    for (int i = 0; i < GFX_RDP_TILE_COUNT; i++) {
        if (g_rdp.tile[i].fmt == G_IM_FMT_CI) {
            g_rdp.loaded_tiles[i].tex_id = 0;
            g_rdp.tile_dirty[i] = true;
        }
    }
}

// Convert U10.2 rectangle coordinates to NDC
static void gfx_draw_rectangle(s32 ulx, s32 uly, s32 lrx, s32 lry) {
    u32 saved_omh  = g_rdp.other_mode_h;
    u32 cycle_type = g_rdp.other_mode_h & (3u << G_MDSFT_CYCLETYPE);
    if (cycle_type == G_CYC_COPY) {
        g_rdp.other_mode_h = (g_rdp.other_mode_h & ~(3u << G_MDSFT_TEXTFILT)) | G_TF_POINT;
    }

    float ulxf = (float)ulx / (4.0f * 160.0f) - 1.0f;
    float ulyf = -((float)uly / (4.0f * 120.0f)) + 1.0f;
    float lrxf = (float)lrx / (4.0f * 160.0f) - 1.0f;
    float lryf = -((float)lry / (4.0f * 120.0f)) + 1.0f;

    LoadedVertex *ul = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 0];
    LoadedVertex *ll = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 1];
    LoadedVertex *lr = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 2];
    LoadedVertex *ur = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 3];

    ul->x = ulxf; ul->y = ulyf; ul->z = -1.0f; ul->w = 1.0f; ul->clip_rej = 0;
    ll->x = ulxf; ll->y = lryf; ll->z = -1.0f; ll->w = 1.0f; ll->clip_rej = 0;
    lr->x = lrxf; lr->y = lryf; lr->z = -1.0f; lr->w = 1.0f; lr->clip_rej = 0;
    ur->x = lrxf; ur->y = ulyf; ur->z = -1.0f; ur->w = 1.0f; ur->clip_rej = 0;

    float saved_vp[4];
    memcpy(saved_vp, &g_rdp.viewport, sizeof(saved_vp));
    u32 saved_geom = g_rsp.geometry_mode;
    g_rdp.viewport.x = 0.0f;
    g_rdp.viewport.y = 0.0f;
    g_rdp.viewport.w = (float)gl_window_width;
    g_rdp.viewport.h = (float)gl_window_height;
    g_rdp.viewport_dirty = true;

    // Rectangles have no per-vertex depth
    g_rsp.geometry_mode = (g_rdp.other_mode_l & G_ZS_PRIM) ? (saved_geom & G_ZBUFFER) : 0;

    gfx_sp_tri1(GFX_MAX_VERTICES + 0, GFX_MAX_VERTICES + 1, GFX_MAX_VERTICES + 3);
    gfx_sp_tri1(GFX_MAX_VERTICES + 1, GFX_MAX_VERTICES + 2, GFX_MAX_VERTICES + 3);

    gfx_flush();

    g_rsp.geometry_mode = saved_geom;
    memcpy(&g_rdp.viewport, saved_vp, sizeof(saved_vp));
    g_rdp.viewport_dirty = true;
    if (cycle_type == G_CYC_COPY) {
        g_rdp.other_mode_h = saved_omh;
    }
}

static void gfx_dp_texture_rectangle(s32 ulx, s32 uly, s32 lrx, s32 lry,
                                      u8 tile, s16 uls, s16 ult,
                                      s16 dsdx, s16 dtdy, bool flip) {
    u8 saved_active_texture_tile = g_rdp.active_texture_tile;
    int saved_cc[8] = {
        g_rdp.cc_rgb_a, g_rdp.cc_rgb_b, g_rdp.cc_rgb_c, g_rdp.cc_rgb_d,
        g_rdp.cc_a_a,   g_rdp.cc_a_b,   g_rdp.cc_a_c,   g_rdp.cc_a_d,
    };

    if (gfx_try_write_sprite_shading_palette(tile)) {
        return;
    }

    if (tile < 8) {
        g_rdp.active_texture_tile = tile;
    }

    if ((g_rdp.other_mode_h & (3u << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {
        // COPY mode: 4 texels/pixel → divide step by 4 to get 1:1 mapping.
        dsdx >>= 2;
        g_rdp.cc_rgb_a = 5; g_rdp.cc_rgb_b = 5; g_rdp.cc_rgb_c = 5; g_rdp.cc_rgb_d = 0;
        g_rdp.cc_a_a   = 5; g_rdp.cc_a_b   = 5; g_rdp.cc_a_c   = 5; g_rdp.cc_a_d   = 0;
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    if (flip) {
        dsdx = -dsdx;
        dtdy = -dtdy;
    }

    s32 width  = !flip ? lrx - ulx : lry - uly;
    s32 height = !flip ? lry - uly : lrx - ulx;
    float ul_u = (float)uls;
    float ul_v = (float)ult;
    float lr_u = (float)(((s32)uls << 7) + (s32)dsdx * width)  / 128.0f;
    float lr_v = (float)(((s32)ult << 7) + (s32)dtdy * height) / 128.0f;

    LoadedVertex *ul = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 0];
    LoadedVertex *ll = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 1];
    LoadedVertex *lr = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 2];
    LoadedVertex *ur = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 3];

    ul->r = ll->r = lr->r = ur->r = 255;
    ul->g = ll->g = lr->g = ur->g = 255;
    ul->b = ll->b = lr->b = ur->b = 255;
    ul->a = ll->a = lr->a = ur->a = 255;

    ul->u = ul_u; ul->v = ul_v;
    lr->u = lr_u; lr->v = lr_v;
    if (!flip) {
        ll->u = ul_u; ll->v = lr_v;
        ur->u = lr_u; ur->v = ul_v;
    } else {
        ll->u = lr_u; ll->v = ul_v;
        ur->u = ul_u; ur->v = lr_v;
    }

    gfx_draw_rectangle(ulx, uly, lrx, lry);

    g_rdp.cc_rgb_a = saved_cc[0]; g_rdp.cc_rgb_b = saved_cc[1];
    g_rdp.cc_rgb_c = saved_cc[2]; g_rdp.cc_rgb_d = saved_cc[3];
    g_rdp.cc_a_a   = saved_cc[4]; g_rdp.cc_a_b   = saved_cc[5];
    g_rdp.cc_a_c   = saved_cc[6]; g_rdp.cc_a_d   = saved_cc[7];
    g_rdp.active_texture_tile = saved_active_texture_tile;
}

static void gfx_rdp_fill_rect(const Gfx *cmd) {
    if (g_rdp.z_buf_addr != NULL && g_rdp.z_buf_addr == g_rdp.color_buf_addr) {
        return;
    }

    s32 ulx = (s32)((cmd->words.w1 >> 14) & 0x3FF);
    s32 uly = (s32)((cmd->words.w1 >>  2) & 0x3FF);
    s32 lrx = (s32)((cmd->words.w0 >> 14) & 0x3FF);
    s32 lry = (s32)((cmd->words.w0 >>  2) & 0x3FF);

    u32 mode = g_rdp.other_mode_h & (3u << G_MDSFT_CYCLETYPE);
    if (mode == G_CYC_COPY || mode == G_CYC_FILL) {
        lrx++;
        lry++;
    }

    LoadedVertex *ul = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 0];
    LoadedVertex *ll = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 1];
    LoadedVertex *lr = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 2];
    LoadedVertex *ur = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 3];

    int saved_cc[8] = {
        g_rdp.cc_rgb_a, g_rdp.cc_rgb_b, g_rdp.cc_rgb_c, g_rdp.cc_rgb_d,
        g_rdp.cc_a_a,   g_rdp.cc_a_b,   g_rdp.cc_a_c,   g_rdp.cc_a_d,
    };

    if (mode == G_CYC_FILL) {
        ul->r = ll->r = lr->r = ur->r = g_rdp.fill_r;
        ul->g = ll->g = lr->g = ur->g = g_rdp.fill_g;
        ul->b = ll->b = lr->b = ur->b = g_rdp.fill_b;
        ul->a = ll->a = lr->a = ur->a = g_rdp.fill_a;
        g_rdp.cc_rgb_a = 5; g_rdp.cc_rgb_b = 5; g_rdp.cc_rgb_c = 5; g_rdp.cc_rgb_d = 2;
        g_rdp.cc_a_a   = 5; g_rdp.cc_a_b   = 5; g_rdp.cc_a_c   = 5; g_rdp.cc_a_d   = 2;
    } else {
        ul->r = ll->r = lr->r = ur->r = 0;
        ul->g = ll->g = lr->g = ur->g = 0;
        ul->b = ll->b = lr->b = ur->b = 0;
        ul->a = ll->a = lr->a = ur->a = 255;
    }

    gfx_draw_rectangle(ulx << 2, uly << 2, lrx << 2, lry << 2);

    g_rdp.cc_rgb_a = saved_cc[0]; g_rdp.cc_rgb_b = saved_cc[1];
    g_rdp.cc_rgb_c = saved_cc[2]; g_rdp.cc_rgb_d = saved_cc[3];
    g_rdp.cc_a_a   = saved_cc[4]; g_rdp.cc_a_b   = saved_cc[5];
    g_rdp.cc_a_c   = saved_cc[6]; g_rdp.cc_a_d   = saved_cc[7];
}

// cmd points to the G_TEXRECT entry
static int gfx_rdp_tex_rect(const Gfx *raw_cmd, int stride) {
    int dummy;
    Gfx texrect = gbi_read_cmd(raw_cmd, &dummy);
    Gfx params  = gbi_read_cmd((const Gfx *)((const u8 *)raw_cmd + stride), &dummy);
    const Gfx *cmd = &texrect;
    bool flip = ((u8)(cmd->words.w0 >> 24) == G_TEXRECTFLIP);
    s32  lrx  = (s32)((cmd->words.w0 >> 12) & 0xFFF);
    s32  lry  = (s32)(cmd->words.w0 & 0xFFF);
    u8   tile = (u8)((cmd->words.w1 >> 24) & 0x7);
    s32  ulx  = (s32)((cmd->words.w1 >> 12) & 0xFFF);
    s32  uly  = (s32)(cmd->words.w1 & 0xFFF);
    s16  uls;
    s16  ult;
    s16  dsdx;
    s16  dtdy;

    if ((u8)(params.words.w0 >> 24) == G_RDPHALF_1) {
        Gfx params2 = gbi_read_cmd((const Gfx *)((const u8 *)raw_cmd + stride * 2), &dummy);

        uls = (s16)(params.words.w1 >> 16);
        ult = (s16)(params.words.w1 & 0xFFFF);
        dsdx = (s16)(params2.words.w1 >> 16);
        dtdy = (s16)(params2.words.w1 & 0xFFFF);
        gfx_dp_texture_rectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, flip);
        return stride * 2;
    }

    uls  = (s16)(params.words.w0 >> 16);
    ult  = (s16)(params.words.w0 & 0xFFFF);
    dsdx = (s16)(params.words.w1 >> 16);
    dtdy = (s16)(params.words.w1 & 0xFFFF);
    gfx_dp_texture_rectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, flip);
    return stride;
}

static void gfx_rdp_set_scissor(const Gfx *cmd) {
    float rx = (gl_window_width  > 0) ? (float)gl_window_width  / 320.0f : 2.0f;
    float ry = (gl_window_height > 0) ? (float)gl_window_height / 240.0f : 2.0f;
    float ulx = (float)C0(12, 12);
    float uly = (float)C0(0,  12);
    float lrx = (float)C1(12, 12);
    float lry = (float)C1(0,  12);
    g_rdp.scissor.x = ulx / 4.0f * rx;
    g_rdp.scissor.y = (240.0f - lry / 4.0f) * ry;
    g_rdp.scissor.w = (lrx - ulx) / 4.0f * rx;
    g_rdp.scissor.h = (lry - uly) / 4.0f * ry;
    g_rdp.viewport_dirty = true;
}

static void gfx_rdp_set_color_image(const Gfx *cmd) {
    g_rdp.color_buf_addr = gfx_resolve_addr((uintptr_t)cmd->words.w1);
}

static void gfx_rdp_set_z_image(const Gfx *cmd) {
    g_rdp.z_buf_addr = gfx_resolve_addr((uintptr_t)cmd->words.w1);
}

static void gfx_rdp_set_blend_color(const Gfx *cmd)  { (void)cmd; }

// w1 layout for all four: RRGGBBAA
static void gfx_rdp_set_env_color(const Gfx *cmd) {
    gfx_flush();
    g_rdp.env_r = (u8)(cmd->words.w1 >> 24);
    g_rdp.env_g = (u8)(cmd->words.w1 >> 16);
    g_rdp.env_b = (u8)(cmd->words.w1 >>  8);
    g_rdp.env_a = (u8)(cmd->words.w1);
}

// gDPSetPrimColor also encodes minlevel (w0[15:8]) and lodfrac (w0[7:0]);
// those affect LOD blending and are ignored on the first pass.
static void gfx_rdp_set_prim_color(const Gfx *cmd) {
    gfx_flush();
    g_rdp.prim_r = (u8)(cmd->words.w1 >> 24);
    g_rdp.prim_g = (u8)(cmd->words.w1 >> 16);
    g_rdp.prim_b = (u8)(cmd->words.w1 >>  8);
    g_rdp.prim_a = (u8)(cmd->words.w1);
    if (gfx_trace_state) {
        fprintf(stderr, "SetPrim rgba=%02X%02X%02X%02X\n",
                g_rdp.prim_r, g_rdp.prim_g, g_rdp.prim_b, g_rdp.prim_a);
    }
}

static void gfx_rdp_set_fog_color(const Gfx *cmd) {
    gfx_flush();
    g_rdp.fog_r = (u8)(cmd->words.w1 >> 24);
    g_rdp.fog_g = (u8)(cmd->words.w1 >> 16);
    g_rdp.fog_b = (u8)(cmd->words.w1 >>  8);
    g_rdp.fog_a = (u8)(cmd->words.w1);
}

static void gfx_rdp_set_prim_depth(const Gfx *cmd) {
    g_rdp.prim_depth = (u16)(cmd->words.w1 >> 16);
    g_rdp.prim_depth_delta = (u16)(cmd->words.w1 & 0xFFFF);
}

// Fill colour is two packed 16-bit RGBA5551 values
static void gfx_rdp_set_fill_color(const Gfx *cmd) {
    u16 packed = (u16)(cmd->words.w1 & 0xFFFF);
    u8  r5 = (u8)((packed >> 11) & 0x1F);
    u8  g5 = (u8)((packed >>  6) & 0x1F);
    u8  b5 = (u8)((packed >>  1) & 0x1F);
    u8  a1 = (u8)(packed & 1);
    // Expand 5-bit → 8-bit: replicate the high bits into the low bits.
    g_rdp.fill_r = (r5 << 3) | (r5 >> 2);
    g_rdp.fill_g = (g5 << 3) | (g5 >> 2);
    g_rdp.fill_b = (b5 << 3) | (b5 >> 2);
    g_rdp.fill_a = a1 ? 0xFF : 0x00;
}

// Decoded mux names for debug output
static const char *cc_rgb_mux_name(int mux) {
    switch (mux) {
        case 0:  return "COMBINED";
        case 1:  return "TEXEL0";
        case 2:  return "TEXEL1";
        case 3:  return "PRIMITIVE";
        case 4:  return "SHADE";
        case 5:  return "ENVIRONMENT";
        case 6:  return "ONE/CENTER";
        case 7:  return "COMBINED_A/NOISE/K4";
        case 8:  return "TEXEL0_ALPHA";
        case 9:  return "TEXEL1_ALPHA";
        case 10: return "PRIMITIVE_ALPHA";
        case 11: return "SHADE_ALPHA";
        case 12: return "ENV_ALPHA";
        case 13: return "LOD_FRACTION";
        case 14: return "PRIM_LOD_FRAC";
        case 15: return "K5";
        case 31: return "0";
        default: return "?";
    }
}
static const char *cc_alpha_mux_name(int mux) {
    switch (mux) {
        case 0: return "COMBINED/LOD";
        case 1: return "TEXEL0";
        case 2: return "TEXEL1";
        case 3: return "PRIMITIVE";
        case 4: return "SHADE";
        case 5: return "ENVIRONMENT";
        case 6: return "ONE/PRIM_LOD";
        case 7: return "0";
        default: return "?";
    }
}

// Print each combiner tuple once
static u64 cc_seen[64];
static int cc_seen_count = 0;

static void cc_log_if_new(int rgb_a, int rgb_b, int rgb_c, int rgb_d,
                          int a_a, int a_b, int a_c, int a_d) {
    if (!getenv("PM_TRACE_CC")) return;
    u64 key = ((u64)rgb_a << 0) | ((u64)rgb_b << 5) | ((u64)rgb_c << 10) | ((u64)rgb_d << 15)
            | ((u64)a_a << 20) | ((u64)a_b << 24) | ((u64)a_c << 28) | ((u64)a_d << 32);
    for (int i = 0; i < cc_seen_count; i++) {
        if (cc_seen[i] == key) return;
    }
    if (cc_seen_count < 64) cc_seen[cc_seen_count++] = key;
    fprintf(stderr,
            "[cc] rgb=(%s, %s, %s, %s)  a=(%s, %s, %s, %s)\n",
            cc_rgb_mux_name(rgb_a), cc_rgb_mux_name(rgb_b),
            cc_rgb_mux_name(rgb_c), cc_rgb_mux_name(rgb_d),
            cc_alpha_mux_name(a_a), cc_alpha_mux_name(a_b),
            cc_alpha_mux_name(a_c), cc_alpha_mux_name(a_d));
}

static void gfx_rdp_set_combine(const Gfx *cmd) {
    gfx_flush();

    int rgb_a = (int)((cmd->words.w0 >> 20) & 0xF);
    int rgb_c = (int)((cmd->words.w0 >> 15) & 0x1F);
    int a_a   = (int)((cmd->words.w0 >> 12) & 0x7);
    int a_c   = (int)((cmd->words.w0 >>  9) & 0x7);
    int rgb_b = (int)((cmd->words.w1 >> 28) & 0xF);
    int rgb_d = (int)((cmd->words.w1 >> 15) & 0x7);
    int a_b   = (int)((cmd->words.w1 >> 12) & 0x7);
    int a_d   = (int)((cmd->words.w1 >>  9) & 0x7);

    cc_log_if_new(rgb_a, rgb_b, rgb_c, rgb_d, a_a, a_b, a_c, a_d);

    g_rdp.cc_rgb_a = cc_rgb_to_idx(rgb_a);
    g_rdp.cc_rgb_b = cc_rgb_to_idx(rgb_b);
    g_rdp.cc_rgb_c = cc_rgb_to_idx(rgb_c);
    g_rdp.cc_rgb_d = cc_rgb_to_idx(rgb_d);
    g_rdp.cc_a_a   = cc_alpha_to_idx(a_a);
    g_rdp.cc_a_b   = cc_alpha_to_idx(a_b);
    g_rdp.cc_a_c   = cc_alpha_to_idx(a_c);
    g_rdp.cc_a_d   = cc_alpha_to_idx(a_d);
    if (gfx_trace_state) {
        fprintf(stderr, "SetCombine cc=(%d,%d,%d,%d) cycle=%u\n",
                g_rdp.cc_rgb_a, g_rdp.cc_rgb_b, g_rdp.cc_rgb_c, g_rdp.cc_rgb_d,
                (g_rdp.other_mode_h >> G_MDSFT_CYCLETYPE) & 3);
    }
}

bool gfx_dump_dl = false;
bool gfx_trace_state = false;

static void gbi_dump_cmd(u8 opcode, const Gfx *cmd) {
    switch (opcode) {
        case G_NOOP:      fprintf(stderr, "GBI G_NOOP\n"); break;
        case G_SPNOOP:    fprintf(stderr, "GBI G_SPNOOP\n"); break;
        case G_ENDDL:     fprintf(stderr, "GBI G_ENDDL\n"); break;
        case G_DL:
            fprintf(stderr, "GBI G_DL %s addr=0x%08X\n",
                    (cmd->words.w0 >> 16) & 0xFF ? "nopush" : "push",
                    cmd->words.w1);
            break;
        case G_VTX:
            fprintf(stderr, "GBI G_VTX n=%u v0=%u addr=0x%08X\n",
                    (cmd->words.w0 >> 12) & 0xFF,
                    (u8)((cmd->words.w0 >> 1) & 0x7F) - (u8)((cmd->words.w0 >> 12) & 0xFF),
                    cmd->words.w1);
            break;
        case G_TRI1: {
            u32 tri = gfx_sp_tri1_word(cmd);
            fprintf(stderr, "GBI G_TRI1 v0=%u v1=%u v2=%u\n",
                    (tri >> 16) & 0xFF,
                    (tri >>  8) & 0xFF,
                     tri        & 0xFF);
            break;
        }
        case G_TRI2:
        case G_QUAD:
            fprintf(stderr, "GBI G_TRI2 [%u %u %u] [%u %u %u]\n",
                    (cmd->words.w0 >> 16) & 0xFF, (cmd->words.w0 >> 8) & 0xFF, cmd->words.w0 & 0xFF,
                    (cmd->words.w1 >> 16) & 0xFF, (cmd->words.w1 >> 8) & 0xFF, cmd->words.w1 & 0xFF);
            break;
        case G_MTX:
            fprintf(stderr, "GBI G_MTX params=0x%02X addr=0x%08X\n",
                    cmd->words.w0 & 0xFF, cmd->words.w1);
            break;
        case G_POPMTX:
            fprintf(stderr, "GBI G_POPMTX n=%u\n", cmd->words.w1 / 64);
            break;
        case G_GEOMETRYMODE:
            fprintf(stderr, "GBI G_GEOMETRYMODE clr=0x%06X set=0x%06X\n",
                    (cmd->words.w0) & 0xFFFFFF, cmd->words.w1);
            break;
        case G_TEXTURE:
            fprintf(stderr, "GBI G_TEXTURE on=%u tile=%u s=%u t=%u\n",
                    (cmd->words.w0 >> 1) & 0x7F,
                    (cmd->words.w0 >> 8) & 0x7,
                    cmd->words.w1 >> 16, cmd->words.w1 & 0xFFFF);
            break;
        case G_MOVEMEM:
            fprintf(stderr, "GBI G_MOVEMEM idx=0x%02X off=%u addr=0x%08X\n",
                    cmd->words.w0 & 0xFF,
                    ((cmd->words.w0 >> 8) & 0xFF) * 8,
                    cmd->words.w1);
            break;
        case G_MOVEWORD:
            fprintf(stderr, "GBI G_MOVEWORD idx=0x%02X off=0x%04X data=0x%08X\n",
                    (cmd->words.w0 >> 16) & 0xFF,
                    cmd->words.w0 & 0xFFFF,
                    cmd->words.w1);
            break;
        case G_SETOTHERMODE_H:
            fprintf(stderr, "GBI G_SETOTHERMODE_H shift=%u len=%u data=0x%08X\n",
                    (cmd->words.w0 >> 8) & 0xFF,
                     cmd->words.w0       & 0xFF,
                     cmd->words.w1);
            break;
        case G_SETOTHERMODE_L:
            fprintf(stderr, "GBI G_SETOTHERMODE_L shift=%u len=%u data=0x%08X\n",
                    (cmd->words.w0 >> 8) & 0xFF,
                     cmd->words.w0       & 0xFF,
                     cmd->words.w1);
            break;
        case G_SETTIMG:
            fprintf(stderr, "GBI G_SETTIMG fmt=%u siz=%u addr=0x%08X\n",
                    (cmd->words.w0 >> 21) & 0x7,
                    (cmd->words.w0 >> 19) & 0x3,
                     cmd->words.w1);
            break;
        case G_SETTILE:
            fprintf(stderr, "GBI G_SETTILE tile=%u fmt=%u siz=%u\n",
                    (cmd->words.w1 >> 24) & 0x7,
                    (cmd->words.w0 >> 21) & 0x7,
                    (cmd->words.w0 >> 19) & 0x3);
            break;
        case G_LOADTILE:
        case G_LOADBLOCK:
        case G_SETTILESIZE:
        case G_LOADTLUT:
            fprintf(stderr, "GBI 0x%02X w0=0x%08X w1=0x%08X\n", opcode, cmd->words.w0, cmd->words.w1);
            break;
        case G_SETCOMBINE:
            fprintf(stderr, "GBI G_SETCOMBINE w0=0x%08X w1=0x%08X\n", cmd->words.w0, cmd->words.w1);
            break;
        case G_SETENVCOLOR:
            fprintf(stderr, "GBI G_SETENVCOLOR rgba=0x%08X\n", cmd->words.w1);
            break;
        case G_SETPRIMCOLOR:
            fprintf(stderr, "GBI G_SETPRIMCOLOR rgba=0x%08X\n", cmd->words.w1);
            break;
        case G_SETBLENDCOLOR:
            fprintf(stderr, "GBI G_SETBLENDCOLOR rgba=0x%08X\n", cmd->words.w1);
            break;
        case G_SETFOGCOLOR:
            fprintf(stderr, "GBI G_SETFOGCOLOR rgba=0x%08X\n", cmd->words.w1);
            break;
        case G_SETFILLCOLOR:
            fprintf(stderr, "GBI G_SETFILLCOLOR rgba=0x%08X\n", cmd->words.w1);
            break;
        case G_FILLRECT:
            fprintf(stderr, "GBI G_FILLRECT ulx=%u uly=%u lrx=%u lry=%u\n",
                    (cmd->words.w1 >> 12) & 0xFFF, cmd->words.w1 & 0xFFF,
                    (cmd->words.w0 >> 12) & 0xFFF, cmd->words.w0 & 0xFFF);
            break;
        case G_TEXRECT:
        case G_TEXRECTFLIP:
            fprintf(stderr, "GBI G_TEXRECT%s tile=%u ulx=%u uly=%u lrx=%u lry=%u\n",
                    opcode == G_TEXRECTFLIP ? "FLIP" : "",
                    (cmd->words.w1 >> 24) & 0x7,
                    (cmd->words.w1 >> 12) & 0xFFF, cmd->words.w1 & 0xFFF,
                    (cmd->words.w0 >> 12) & 0xFFF, cmd->words.w0 & 0xFFF);
            break;
        case G_SETSCISSOR:
            fprintf(stderr, "GBI G_SETSCISSOR ulx=%u uly=%u lrx=%u lry=%u\n",
                    (cmd->words.w0 >> 12) & 0xFFF, cmd->words.w0 & 0xFFF,
                    (cmd->words.w1 >> 12) & 0xFFF, cmd->words.w1 & 0xFFF);
            break;
        case G_SETCIMG:
            fprintf(stderr, "GBI G_SETCIMG addr=0x%08X\n", cmd->words.w1);
            break;
        case G_SETZIMG:
            fprintf(stderr, "GBI G_SETZIMG addr=0x%08X\n", cmd->words.w1);
            break;
        case G_RDPFULLSYNC:  fprintf(stderr, "GBI G_RDPFULLSYNC\n");  break;
        case G_RDPTILESYNC:  fprintf(stderr, "GBI G_RDPTILESYNC\n");  break;
        case G_RDPPIPESYNC:  fprintf(stderr, "GBI G_RDPPIPESYNC\n");  break;
        case G_RDPLOADSYNC:  fprintf(stderr, "GBI G_RDPLOADSYNC\n");  break;
        default:
            fprintf(stderr, "GBI 0x%02X w0=0x%08X w1=0x%08X\n", opcode, cmd->words.w0, cmd->words.w1);
            break;
    }
}

void gbi_init(void) {
    rdp_state_init();
    gbi_invalidate_texture_bindings();
}

void gbi_invalidate_texture_bindings(void) {
    s_texel0_id = 0;
    s_texel1_id = 0;
    gfx_use_tex = 0;
}

void gbi_run_dl(Gfx *dl) {
    Gfx *stack[GFX_DL_STACK_DEPTH];
    int  stack_depth = 0;
    Gfx *call_origin[GFX_DL_STACK_DEPTH];
    uintptr_t call_target[GFX_DL_STACK_DEPTH];
    Gfx *cmd = dl;
    Gfx *dl_origin = dl;
    static u32 unknown_opcode_warnings = 0;

    while (1) {
        int stride;
        if (!gfx_ptr_range_readable(cmd, sizeof(*cmd))) {
            return;
        }
        Gfx decoded_cmd = gbi_read_cmd(cmd, &stride);
        Gfx *raw_cmd = cmd;

        cmd = &decoded_cmd;
        u8 opcode = (u8)(cmd->words.w0 >> 24);
        if (gfx_dump_dl) gbi_dump_cmd(opcode, cmd);

        switch (opcode) {
            case G_NOOP:
            case G_SPNOOP:
                break;

            case G_DL: {
                Gfx *target = gfx_try_shape_shadow_dl_addr((uintptr_t)cmd->words.w1);
                if (target == NULL) {
                    target = gfx_resolve_addr((uintptr_t)cmd->words.w1);
                }
                bool is_push = (C0(16, 8) == G_DL_PUSH);

                if (!gfx_dl_target_renderable(target)) {
                    if (is_push) {
                        cmd = (Gfx *)((u8 *)raw_cmd + stride);
                        continue;
                    }
                    if (stack_depth == 0) return;
                    raw_cmd = stack[--stack_depth];
                    dl_origin = (stack_depth > 0) ? (Gfx *)call_target[stack_depth - 1] : dl;
                    cmd = raw_cmd;
                    continue;
                }

                if (is_push && stack_depth < GFX_DL_STACK_DEPTH) {
                    call_origin[stack_depth] = raw_cmd;
                    call_target[stack_depth] = (uintptr_t)cmd->words.w1;
                    stack[stack_depth++] = (Gfx *)((u8 *)raw_cmd + stride);
                }
                dl_origin = target;
                raw_cmd = target;
                cmd = raw_cmd;
                continue;
            }

            case G_ENDDL:
                if (stack_depth == 0) return;
                raw_cmd = stack[--stack_depth];
                dl_origin = (stack_depth > 0) ? (Gfx *)call_target[stack_depth - 1] : dl;
                cmd = raw_cmd;
                continue;

            // RDPHALF_1/2 encode the s/t and dsdx/dtdy for texture rectangles.
            case G_RDPHALF_1:
                g_rsp.saved_uls = (u16)(cmd->words.w1 >> 16);
                g_rsp.saved_ult = (u16)(cmd->words.w1 & 0xFFFFu);
                break;
            case G_RDPHALF_2:
                break;

            case G_VTX:
                gfx_sp_vertex((int)C0(12, 8), (int)(C0(1, 7) - C0(12, 8)),
                              gfx_resolve_addr((uintptr_t)cmd->words.w1));
                break;
            case G_MODIFYVTX:      gfx_sp_modify_vertex(cmd);     break;
            case G_CULLDL:         gfx_sp_cull_dl(cmd);           break;
            case G_BRANCH_Z:       gfx_sp_branch_z(cmd);          break;
            case G_TRI1: {
                u32 tri = gfx_sp_tri1_word(cmd);

                gfx_sp_tri1((u8)(((tri >> 16) & 0xFF) / 2), (u8)(((tri >> 8) & 0xFF) / 2),
                            (u8)((tri & 0xFF) / 2));
                break;
            }
            case G_TRI2:
            case G_QUAD:
                gfx_sp_tri1((u8)(C0(16, 8) / 2), (u8)(C0(8, 8) / 2), (u8)(C0(0, 8) / 2));
                gfx_sp_tri1((u8)(C1(16, 8) / 2), (u8)(C1(8, 8) / 2), (u8)(C1(0, 8) / 2));
                break;
            case G_MTX:    gfx_sp_matrix((u8)(C0(0, 8) ^ G_MTX_PUSH), gfx_resolve_addr((uintptr_t)cmd->words.w1)); break;
            case G_POPMTX: gfx_sp_pop_matrix(cmd->words.w1 / 64); break;
            case G_GEOMETRYMODE:   gfx_sp_geometry_mode(cmd);     break;
            case G_TEXTURE:        gfx_sp_texture(cmd);           break;
            case G_MOVEMEM:        gfx_sp_move_mem(cmd);          break;
            case G_MOVEWORD:       gfx_sp_move_word(cmd);         break;
            case G_SETOTHERMODE_H: gfx_rdp_set_other_mode_h(cmd); break;
            case G_SETOTHERMODE_L: gfx_rdp_set_other_mode_l(cmd); break;
            case G_SETTIMG:        gfx_rdp_set_texture_image(cmd); break;
            case G_SETTILE:        gfx_rdp_set_tile(cmd);         break;
            case G_LOADTILE:       gfx_rdp_load_tile(cmd);        break;
            case G_LOADBLOCK:      gfx_rdp_load_block(cmd);       break;
            case G_SETTILESIZE:    gfx_rdp_set_tile_size(cmd);    break;
            case G_LOADTLUT:       gfx_rdp_load_tlut(cmd);        break;
            case G_SETCOMBINE:     gfx_rdp_set_combine(cmd);      break;
            case G_SETENVCOLOR:    gfx_rdp_set_env_color(cmd);    break;
            case G_SETPRIMCOLOR:   gfx_rdp_set_prim_color(cmd);   break;
            case G_SETBLENDCOLOR:  gfx_rdp_set_blend_color(cmd);  break;
            case G_SETFOGCOLOR:    gfx_rdp_set_fog_color(cmd);    break;
            case G_SETFILLCOLOR:   gfx_rdp_set_fill_color(cmd);   break;
            case G_FILLRECT:       gfx_rdp_fill_rect(cmd);        break;

            // TEXRECT followed by either packed DP params or
            // the RDPHALF_1/RDPHALF_2 pair emitted by gSPTextureRectangle.
            case G_TEXRECT:
            case G_TEXRECTFLIP:
                raw_cmd = (Gfx *)((u8 *)raw_cmd + gfx_rdp_tex_rect(raw_cmd, stride));
                break;

            case G_SETSCISSOR:     gfx_rdp_set_scissor(cmd);      break;
            case G_SETCIMG:        gfx_rdp_set_color_image(cmd);  break;
            case G_SETZIMG:        gfx_rdp_set_z_image(cmd);      break;

            case G_RDPFULLSYNC:
            case G_RDPTILESYNC:
            case G_RDPPIPESYNC:
            case G_RDPLOADSYNC:
                gfx_flush();
                break;

            case G_SETPRIMDEPTH:   gfx_rdp_set_prim_depth(cmd);   break;

            case G_LINE3D:
            case G_LOAD_UCODE:
                break;

            default:
                if (unknown_opcode_warnings < 16) {
                    fprintf(stderr, "gbi: unknown opcode 0x%02X w0=0x%016llX w1=0x%016llX raw=%p stride=%d\n",
                            opcode,
                            (unsigned long long)cmd->words.w0,
                            (unsigned long long)cmd->words.w1,
                            (void *)raw_cmd, stride);
                    fprintf(stderr, "    dl_origin=%p depth=%d top_dl=%p root=%p\n",
                            (void *)dl_origin, stack_depth,
                            stack_depth > 0 ? (void *)call_target[stack_depth - 1] : NULL,
                            (void *)dl);
                    for (int d = stack_depth - 1; d >= 0; d--) {
                        fprintf(stderr, "        [%d] call from %p -> target 0x%016llX\n",
                                d, (void *)call_origin[d], (unsigned long long)call_target[d]);
                    }
                    unknown_opcode_warnings++;
                }
                return;
        }

        cmd = (Gfx *)((u8 *)raw_cmd + stride);
    }
}
