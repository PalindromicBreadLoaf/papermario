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

// Bit-field extractors for GBI command words.  Both gbi_run_dl and all static
// handler functions receive a 'cmd' pointer; the macros expand using that name.
#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1u << (width)) - 1u))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1u << (width)) - 1u))
#define PC_SHAPE_KSEG_BASE  0x80210000u
#define PC_SHAPE_PHYS_BASE  0x00210000u
#define PC_SHAPE_SIZE_LIMIT 0x40000u

static int s_texel0_slot = -1;
static unsigned int s_texel0_id;
static unsigned int s_texel1_id;

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

static bool ptr_in_map_shape(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)gMapShapeData;

    return start >= base && size <= PC_SHAPE_SIZE_LIMIT && start - base <= PC_SHAPE_SIZE_LIMIT - size;
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

// Decode one display-list command and report its byte stride.
// PC-native commands are 16 bytes (two uintptr_t). N64 big-endian commands
// are 8 bytes: the N64's w0 sits in the low 32 bits of the PC w0, and the
// N64's w1 sits in the HIGH 32 bits of the PC w0 (not in PC w1).
// stride_bytes is set to 8 for N64 commands, sizeof(Gfx) for PC commands.
static Gfx gbi_read_cmd(const Gfx *src, int *stride_bytes) {
    Gfx cmd = *src;
    u8 opcode = (u8)(cmd.words.w0 >> 24);
    u8 swapped_opcode = (u8)cmd.words.w0;

    if (!gbi_opcode_known(opcode) && gbi_opcode_known(swapped_opcode)) {
        u32 n64_w1 = cmd.words.w1;
        cmd.words.w0 = bswap32(cmd.words.w0);
        cmd.words.w1 = bswap32(n64_w1);
        *stride_bytes = 8;
    } else {
        *stride_bytes = (int)sizeof(Gfx);
    }
    return cmd;
}

static void *gfx_default_segment_base(u8 segment) {
    if (segment == 1) {
        return ((void **)gMapShapeData)[1];
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
    uintptr_t resolved = addr;
    u8 segment = (u8)(addr >> 24);
    void *default_base;

    // PC host pointers into gMapShapeData (e.g. shape DLs) pass straight through;
    // they must not be decoded as N64 segment-relative addresses even if their
    // high byte accidentally matches a known segment number.
    uintptr_t shape_start = (uintptr_t)gMapShapeData;
    if (addr >= shape_start && addr < shape_start + PC_SHAPE_SIZE_LIMIT) {
        return (void *)addr;
    }

    // PC-generated display lists may carry either full host pointers or the
    // low 32 bits returned by osVirtualToPhysical(). Resolve those before
    // interpreting the value as an N64 segmented address.
    void *host_ptr = pc_resolve_physical_addr(addr);
    if (gfx_ptr_range_readable(host_ptr, 1)) {
        return host_ptr;
    }

    default_base = gfx_default_segment_base(segment);

    if (default_base != NULL) {
        return (u8 *)default_base + (addr & 0x00FFFFFFu);
    }

    if (segment < 16 && g_rsp.segments[segment] != NULL) {
        return (u8 *)g_rsp.segments[segment] + (addr & 0x00FFFFFFu);
    }

    if (addr >= PC_SHAPE_KSEG_BASE && addr < PC_SHAPE_KSEG_BASE + PC_SHAPE_SIZE_LIMIT) {
        return gMapShapeData + (addr - PC_SHAPE_KSEG_BASE);
    }

    if (addr >= PC_SHAPE_PHYS_BASE && addr < PC_SHAPE_PHYS_BASE + PC_SHAPE_SIZE_LIMIT) {
        return gMapShapeData + (addr - PC_SHAPE_PHYS_BASE);
    }

#if UINTPTR_MAX > 0xffffffffu
    if (addr >= UINT64_C(0xffffffff80000000)) {
        resolved = (uintptr_t)((u32)addr + 0x80000000u);
    } else
#endif
    if (addr >= 0x80000000u && addr < 0xC0000000u) {
        resolved = (uintptr_t)((u32)addr + 0x80000000u);
    }

    void *tlb_ptr = pc_tlb_translate((void *)resolved);

    if (tlb_ptr != (void *)resolved) {
        return tlb_ptr;
    }
    return pc_resolve_physical_addr(resolved);
}

// Row-major 4x4 multiply.  res may alias a or b.
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

// Apply current other_mode_l to the GL depth and blend state.
// Must be called after gfx_flush() so the state takes effect on the next batch.
static void gfx_apply_render_state(void) {
    u32 oml = g_rdp.other_mode_l;

    if ((oml & Z_CMP) && (g_rsp.geometry_mode & G_ZBUFFER))
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);

    glDepthMask((oml & Z_UPD) ? GL_TRUE : GL_FALSE);

    // FORCE_BL: the blender is active regardless of coverage.
    // CVG_X_ALPHA + ALPHA_CVG_SEL: coverage treated as alpha
    bool use_alpha = (oml & FORCE_BL) ||
                    ((oml & CVG_X_ALPHA) && (oml & ALPHA_CVG_SEL));
    if (use_alpha) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

// If other_mode_l changed since the last draw, flush pending tris (so they
// keep the old blend state) and then switch to the new GL state.
static void gfx_ensure_blend_state(void) {
    if (!g_rdp.blend_dirty) return;
    gfx_flush();
    g_rdp.blend_dirty = false;
    gfx_apply_render_state();
}

// If any texture slot is dirty, flush pending triangles (so they keep the old
// bindings) and then re-upload via the texture cache.
static void gfx_ensure_textures(void) {
    if (!g_rdp.textures_dirty[0] && !g_rdp.textures_dirty[1]) return;

    gfx_flush();

    for (int i = 0; i < 2; i++) {
        if (!g_rdp.textures_dirty[i]) continue;
        g_rdp.textures_dirty[i] = false;

        if (!g_rdp.loaded[i].addr || !g_rdp.loaded[i].size_bytes) continue;

        TileDesc *td   = &g_rdp.tile[i];
        u16 width  = (td->lrs > td->uls) ? (u16)((td->lrs - td->uls) / 4 + 1) : 1;
        u16 height = (td->lrt > td->ult) ? (u16)((td->lrt - td->ult) / 4 + 1) : 1;
        const u8 *tlut = (td->fmt == G_IM_FMT_CI) ? g_rdp.tlut : NULL;

        unsigned int tex_id = texture_cache_get(g_rdp.loaded[i].addr,
                                                td->fmt, td->siz,
                                                g_rdp.loaded[i].size_bytes,
                                                tlut, width, height,
                                                td->cms, td->cmt);
        if (tex_id) {
            g_rdp.loaded[i].width = width;
            g_rdp.loaded[i].height = height;
            g_rdp.loaded[i].tex_id = tex_id;
        }
    }
}

static void gfx_bind_active_textures(int tex_slot) {
    unsigned int tex0_id = g_rdp.loaded[tex_slot].tex_id;
    unsigned int tex1_id = 0;

    if (tex_slot + 1 < 2) {
        tex1_id = g_rdp.loaded[tex_slot + 1].tex_id;
    }

    if (s_texel0_slot == tex_slot && s_texel0_id == tex0_id && s_texel1_id == tex1_id) {
        return;
    }

    gfx_flush();
    s_texel0_slot = tex_slot;
    s_texel0_id = tex0_id;
    s_texel1_id = tex1_id;

    if (tex0_id != 0) {
        gfx_bind_texture(0, tex0_id);
    }
    if (tex1_id != 0) {
        gfx_bind_texture(1, tex1_id);
    }

    gfx_use_tex = tex0_id == 0 ? 0 : (tex1_id == 0 ? 1 : 2);
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
    gfx_ensure_textures();

    int tex_slot = g_rdp.active_texture_slot;
    if (tex_slot < 0 || tex_slot > 1 || !g_rdp.loaded[tex_slot].addr) {
        tex_slot = (gfx_use_tex > 0) ? gfx_use_tex - 1 : 0;
    }
    gfx_bind_active_textures(tex_slot);
    if (gfx_buf_vbo_num_tris == GFX_MAX_BUFFERED) gfx_flush();

    float tex_width = (float)g_rdp.loaded[tex_slot].width;
    float tex_height = (float)g_rdp.loaded[tex_slot].height;
    float inv_tex_width = tex_width > 0.0f ? 1.0f / (tex_width * 32.0f) : 1.0f;
    float inv_tex_height = tex_height > 0.0f ? 1.0f / (tex_height * 32.0f) : 1.0f;

    for (int i = 0; i < 3; i++) {
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->x;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->y;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->z;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->w;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->u * inv_tex_width;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->v * inv_tex_height;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->r / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->g / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->b / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->a / 255.0f;
    }
    gfx_buf_vbo_num_tris++;
}

// Map G_CCMUX_* / G_ACMUX_* constants to the shader source-array index:
//   0=tex0  1=tex1  2=shade  3=prim  4=env  5=zero  6=one
//
// TODO: COMBINED, NOISE, K4, _ALPHA scalars, LOD, etc.
static int cc_rgb_to_idx(int mux) {
    switch (mux) {
        case G_CCMUX_TEXEL0:      return 0;
        case G_CCMUX_TEXEL1:      return 1;
        case G_CCMUX_SHADE:       return 2;
        case G_CCMUX_PRIMITIVE:   return 3;
        case G_CCMUX_ENVIRONMENT: return 4;
        // G_CCMUX_1 = G_CCMUX_CENTER = G_CCMUX_SCALE = 6.
        // Valid as "one" in A/D slots; B/C use it as chroma-key centre / LOD
        // scale is approximately 1.0 for now.
        case 6:                   return 6;
        default:                  return 5;  // zero
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
        if (slot >= 0 && slot <= GFX_MAX_LIGHTS) {
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
    g_rdp.other_mode_h = (g_rdp.other_mode_h & ~mask) | (cmd->words.w1 & mask);
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
    g_rdp.tex_to_load.addr      = gfx_resolve_addr((uintptr_t)cmd->words.w1);
    g_rdp.tex_to_load.tile_slot = 0;
}

static void gfx_rdp_set_tile(const Gfx *cmd) {
    u8  fmt  = (u8)C0(21, 3);
    u8  siz  = (u8)C0(19, 2);
    u32 line = C0(9, 9);
    u32 tmem = C0(0, 9);
    int tile = (int)C1(24, 3);
    u8  cmt  = (u8)C1(18, 2);
    u8  cms  = (u8)C1(8,  2);

    g_rdp.tile[tile].fmt         = fmt;
    g_rdp.tile[tile].siz         = siz;
    g_rdp.tile[tile].line_bytes  = line * 8;
    g_rdp.tile[tile].tmem_offset = tmem;
    g_rdp.tile[tile].cms         = cms;
    g_rdp.tile[tile].cmt         = cmt;

    if (tile == G_TX_LOADTILE) {
        g_rdp.tex_to_load.tile_slot = (tmem >= 256u) ? 1 : 0;
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
    int slot    = g_rdp.tex_to_load.tile_slot;
    u32 n_bytes = (lrs + 1u) << load_block_shift(g_rdp.tex_to_load.siz);

    g_rdp.loaded[slot].addr       = g_rdp.tex_to_load.addr;
    g_rdp.loaded[slot].size_bytes = n_bytes;
    g_rdp.loaded[slot].width      = 0;
    g_rdp.loaded[slot].height     = 0;
    g_rdp.loaded[slot].tex_id     = 0;
    g_rdp.textures_dirty[slot]    = true;
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
    int slot = g_rdp.tex_to_load.tile_slot;
    u32 w    = (lrs - uls) / 4u + 1u;
    u32 h    = (lrt - ult) / 4u + 1u;

    g_rdp.loaded[slot].addr       = g_rdp.tex_to_load.addr;
    g_rdp.loaded[slot].size_bytes = texels_to_bytes(g_rdp.tex_to_load.siz, w * h);
    g_rdp.loaded[slot].width      = 0;
    g_rdp.loaded[slot].height     = 0;
    g_rdp.loaded[slot].tex_id     = 0;
    g_rdp.textures_dirty[slot]    = true;
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

    if (tile < 2) {
        g_rdp.textures_dirty[tile] = true;
    }
}

static void gfx_rdp_load_tlut(const Gfx *cmd) {
    (void)cmd;
    g_rdp.tlut = g_rdp.tex_to_load.addr;
}
// Convert U10.2 rectangle coordinates to NDC, build four corner vertices at
// GFX_MAX_VERTICES+0..3, draw two triangles, then flush before restoring state.
// Callers are responsible for setting u/v and r/g/b/a on the corner vertices and
// for saving/restoring any combiner state they override.
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

    // Use the full window viewport and no culling/fog for 2D rects.
    float saved_vp[4];
    memcpy(saved_vp, &g_rdp.viewport, sizeof(saved_vp));
    u32 saved_geom = g_rsp.geometry_mode;
    g_rdp.viewport.x = 0.0f;
    g_rdp.viewport.y = 0.0f;
    g_rdp.viewport.w = (float)gl_window_width;
    g_rdp.viewport.h = (float)gl_window_height;
    g_rdp.viewport_dirty = true;
    g_rsp.geometry_mode = 0;

    gfx_sp_tri1(GFX_MAX_VERTICES + 0, GFX_MAX_VERTICES + 1, GFX_MAX_VERTICES + 3);
    gfx_sp_tri1(GFX_MAX_VERTICES + 1, GFX_MAX_VERTICES + 2, GFX_MAX_VERTICES + 3);
    // Flush now so the combiner/use_tex uniforms captured at draw time match this
    // rect's state, not whatever the caller restores afterward.
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
    int saved_cc[8] = {
        g_rdp.cc_rgb_a, g_rdp.cc_rgb_b, g_rdp.cc_rgb_c, g_rdp.cc_rgb_d,
        g_rdp.cc_a_a,   g_rdp.cc_a_b,   g_rdp.cc_a_c,   g_rdp.cc_a_d,
    };

    if (tile < 8) {
        g_rdp.active_texture_slot = (g_rdp.tile[tile].tmem_offset >= 256u) ? 1 : 0;
    }

    if ((g_rdp.other_mode_h & (3u << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {
        // COPY mode: 4 texels/pixel → divide step by 4 to get 1:1 mapping.
        dsdx >>= 2;
        // Force output = texel0 (combiner disabled in COPY mode).
        g_rdp.cc_rgb_a = 5; g_rdp.cc_rgb_b = 5; g_rdp.cc_rgb_c = 5; g_rdp.cc_rgb_d = 0;
        g_rdp.cc_a_a   = 5; g_rdp.cc_a_b   = 5; g_rdp.cc_a_c   = 5; g_rdp.cc_a_d   = 0;
        // Off-by-one edge rule: add 1 pixel in U10.2.
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    if (flip) {
        dsdx = -dsdx;
        dtdy = -dtdy;
    }

    s32 width  = !flip ? lrx - ulx : lry - uly;
    s32 height = !flip ? lry - uly : lrx - ulx;
    float lrs  = (float)(((s32)uls << 7) + (s32)dsdx * width)  / 128.0f;
    float lrt  = (float)(((s32)ult << 7) + (s32)dtdy * height) / 128.0f;

    LoadedVertex *ul = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 0];
    LoadedVertex *ll = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 1];
    LoadedVertex *lr = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 2];
    LoadedVertex *ur = &g_rsp.loaded_vertices[GFX_MAX_VERTICES + 3];

    ul->r = ll->r = lr->r = ur->r = 255;
    ul->g = ll->g = lr->g = ur->g = 255;
    ul->b = ll->b = lr->b = ur->b = 255;
    ul->a = ll->a = lr->a = ur->a = 255;

    ul->u = (float)uls; ul->v = (float)ult;
    lr->u = lrs;        lr->v = lrt;
    if (!flip) {
        ll->u = (float)uls; ll->v = lrt;
        ur->u = lrs;        ur->v = (float)ult;
    } else {
        ll->u = lrs;        ll->v = (float)ult;
        ur->u = (float)uls; ur->v = lrt;
    }

    gfx_draw_rectangle(ulx, uly, lrx, lry);

    g_rdp.cc_rgb_a = saved_cc[0]; g_rdp.cc_rgb_b = saved_cc[1];
    g_rdp.cc_rgb_c = saved_cc[2]; g_rdp.cc_rgb_d = saved_cc[3];
    g_rdp.cc_a_a   = saved_cc[4]; g_rdp.cc_a_b   = saved_cc[5];
    g_rdp.cc_a_c   = saved_cc[6]; g_rdp.cc_a_d   = saved_cc[7];
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
        ul->r = ll->r = lr->r = ur->r = 255;
        ul->g = ll->g = lr->g = ur->g = 255;
        ul->b = ll->b = lr->b = ur->b = 255;
        ul->a = ll->a = lr->a = ur->a = 255;
    }

    gfx_draw_rectangle(ulx << 2, uly << 2, lrx << 2, lry << 2);

    g_rdp.cc_rgb_a = saved_cc[0]; g_rdp.cc_rgb_b = saved_cc[1];
    g_rdp.cc_rgb_c = saved_cc[2]; g_rdp.cc_rgb_d = saved_cc[3];
    g_rdp.cc_a_a   = saved_cc[4]; g_rdp.cc_a_b   = saved_cc[5];
    g_rdp.cc_a_c   = saved_cc[6]; g_rdp.cc_a_d   = saved_cc[7];
}

// cmd points to the G_TEXRECT entry. PC-generated gDPTextureRectangle stores
// s/t and dsdx/dtdy in the next Gfx, while gSPTextureRectangle emits them as
// RDPHALF_1 and RDPHALF_2 commands.
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
    g_rdp.env_r = (u8)(cmd->words.w1 >> 24);
    g_rdp.env_g = (u8)(cmd->words.w1 >> 16);
    g_rdp.env_b = (u8)(cmd->words.w1 >>  8);
    g_rdp.env_a = (u8)(cmd->words.w1);
}

// gDPSetPrimColor also encodes minlevel (w0[15:8]) and lodfrac (w0[7:0]);
// those affect LOD blending and are ignored on the first pass.
static void gfx_rdp_set_prim_color(const Gfx *cmd) {
    g_rdp.prim_r = (u8)(cmd->words.w1 >> 24);
    g_rdp.prim_g = (u8)(cmd->words.w1 >> 16);
    g_rdp.prim_b = (u8)(cmd->words.w1 >>  8);
    g_rdp.prim_a = (u8)(cmd->words.w1);
}

static void gfx_rdp_set_fog_color(const Gfx *cmd) {
    g_rdp.fog_r = (u8)(cmd->words.w1 >> 24);
    g_rdp.fog_g = (u8)(cmd->words.w1 >> 16);
    g_rdp.fog_b = (u8)(cmd->words.w1 >>  8);
    g_rdp.fog_a = (u8)(cmd->words.w1);
}

// Fill colour is two packed 16-bit RGBA5551 values (for 16-bit framebuffers).
// Decode the lower copy and copy to the upper
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

static void gfx_rdp_set_combine(const Gfx *cmd) {
    // Decode cycle-0 sub-fields from the GCCc bit layout:
    //   w0[23:20] rgb_a  (saRGB0, 4-bit)
    //   w0[19:15] rgb_c  (mRGB0,  5-bit)
    //   w0[14:12] a_a    (saA0,   3-bit)
    //   w0[11:9]  a_c    (mA0,    3-bit)
    //   w1[31:28] rgb_b  (sbRGB0, 4-bit)
    //   w1[17:15] rgb_d  (aRGB0,  3-bit)
    //   w1[14:12] a_b    (sbA0,   3-bit)
    //   w1[11:9]  a_d    (aA0,    3-bit)
    // Cycle-1 fields are skipped
    int rgb_a = (int)((cmd->words.w0 >> 20) & 0xF);
    int rgb_c = (int)((cmd->words.w0 >> 15) & 0x1F);
    int a_a   = (int)((cmd->words.w0 >> 12) & 0x7);
    int a_c   = (int)((cmd->words.w0 >>  9) & 0x7);
    int rgb_b = (int)((cmd->words.w1 >> 28) & 0xF);
    int rgb_d = (int)((cmd->words.w1 >> 15) & 0x7);
    int a_b   = (int)((cmd->words.w1 >> 12) & 0x7);
    int a_d   = (int)((cmd->words.w1 >>  9) & 0x7);

    g_rdp.cc_rgb_a = cc_rgb_to_idx(rgb_a);
    g_rdp.cc_rgb_b = cc_rgb_to_idx(rgb_b);
    g_rdp.cc_rgb_c = cc_rgb_to_idx(rgb_c);
    g_rdp.cc_rgb_d = cc_rgb_to_idx(rgb_d);
    g_rdp.cc_a_a   = cc_alpha_to_idx(a_a);
    g_rdp.cc_a_b   = cc_alpha_to_idx(a_b);
    g_rdp.cc_a_c   = cc_alpha_to_idx(a_c);
    g_rdp.cc_a_d   = cc_alpha_to_idx(a_d);
}

bool gfx_dump_dl = false;

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
        case G_TRI1:
            fprintf(stderr, "GBI G_TRI1 v0=%u v1=%u v2=%u\n",
                    (cmd->words.w1 >> 16) & 0xFF,
                    (cmd->words.w1 >>  8) & 0xFF,
                     cmd->words.w1        & 0xFF);
            break;
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
    s_texel0_slot = -1;
    s_texel0_id = 0;
    s_texel1_id = 0;
}

void gbi_run_dl(Gfx *dl) {
    Gfx *stack[GFX_DL_STACK_DEPTH];
    int  stack_depth = 0;
    Gfx *cmd = dl;
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
                Gfx *target = gfx_resolve_addr((uintptr_t)cmd->words.w1);
                if (C0(16, 8) == G_DL_PUSH && stack_depth < GFX_DL_STACK_DEPTH) {
                    stack[stack_depth++] = (Gfx *)((u8 *)raw_cmd + stride);
                }
                raw_cmd = target;
                cmd = raw_cmd;
                continue;
            }

            case G_ENDDL:
                if (stack_depth == 0) return;
                raw_cmd = stack[--stack_depth];
                cmd = raw_cmd;
                continue;

            // RDPHALF_1/2 encode the s/t and dsdx/dtdy for texture rectangles.
            // They are normally consumed by the G_TEXRECT handler below; these
            // cases handle the rare situation where they appear out of context.
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
            // F3DEX_GBI_2: G_TRI1 stores vertex indices in w1 (C1), not w0.
            case G_TRI1:
                gfx_sp_tri1((u8)(C1(16, 8) / 2), (u8)(C1(8, 8) / 2), (u8)(C1(0, 8) / 2));
                break;
            // G_TRI2 and G_QUAD both encode two triangles: first in w0, second in w1.
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

            case G_LINE3D:
            case G_LOAD_UCODE:
            case G_SETPRIMDEPTH:
                break;

            default:
                if (unknown_opcode_warnings < 16) {
                    fprintf(stderr, "gbi: unknown opcode 0x%02X\n", opcode);
                    unknown_opcode_warnings++;
                }
                return;
        }

        cmd = (Gfx *)((u8 *)raw_cmd + stride);
    }
}
