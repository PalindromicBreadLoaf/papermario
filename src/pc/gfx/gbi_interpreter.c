#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "gbi_interpreter.h"
#include "rdp_state.h"
#include "gl_backend.h"

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

static void gfx_transposed_matrix_mul(float res[3], const float a[3], const float b[4][4]) {
    res[0] = a[0]*b[0][0] + a[1]*b[0][1] + a[2]*b[0][2];
    res[1] = a[0]*b[1][0] + a[1]*b[1][1] + a[2]*b[1][2];
    res[2] = a[0]*b[2][0] + a[1]*b[2][1] + a[2]*b[2][2];
}

static void gfx_update_light_coeffs(void) {
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
    if (g_rsp.lights_dirty && (g_rsp.geometry_mode & G_LIGHTING)) {
        gfx_update_light_coeffs();
    }

    for (int i = 0; i < n; i++) {
        const Vtx_t  *v  = &src[i].v;
        const Vtx_tn *vn = &src[i].n;
        LoadedVertex *d  = &g_rsp.loaded_vertices[dest + i];

        float x = v->ob[0] * g_rsp.MP_matrix[0][0] + v->ob[1] * g_rsp.MP_matrix[1][0] +
                  v->ob[2] * g_rsp.MP_matrix[2][0] + g_rsp.MP_matrix[3][0];
        float y = v->ob[0] * g_rsp.MP_matrix[0][1] + v->ob[1] * g_rsp.MP_matrix[1][1] +
                  v->ob[2] * g_rsp.MP_matrix[2][1] + g_rsp.MP_matrix[3][1];
        float z = v->ob[0] * g_rsp.MP_matrix[0][2] + v->ob[1] * g_rsp.MP_matrix[1][2] +
                  v->ob[2] * g_rsp.MP_matrix[2][2] + g_rsp.MP_matrix[3][2];
        float w = v->ob[0] * g_rsp.MP_matrix[0][3] + v->ob[1] * g_rsp.MP_matrix[1][3] +
                  v->ob[2] * g_rsp.MP_matrix[2][3] + g_rsp.MP_matrix[3][3];
        d->x = x; d->y = y; d->z = z; d->w = w;

        d->u = (float)((v->tc[0] * (s32)g_rsp.tex_scale.s) >> 16);
        d->v = (float)((v->tc[1] * (s32)g_rsp.tex_scale.t) >> 16);

        if (g_rsp.geometry_mode & G_LIGHTING) {
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

    if (gfx_buf_vbo_num_tris == GFX_MAX_BUFFERED) gfx_flush();

    for (int i = 0; i < 3; i++) {
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->x;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->y;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->z;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->w;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->u;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->v;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->r / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->g / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->b / 255.0f;
        gfx_buf_vbo[gfx_buf_vbo_len++] = lv[i]->a / 255.0f;
    }
    gfx_buf_vbo_num_tris++;
}

// TODO: Unstub these
static void gfx_sp_modify_vertex(const Gfx *cmd)     { (void)cmd; }
static void gfx_sp_cull_dl(const Gfx *cmd)           { (void)cmd; }
static void gfx_sp_branch_z(const Gfx *cmd)          { (void)cmd; }
static void gfx_sp_geometry_mode(const Gfx *cmd)     { (void)cmd; }
static void gfx_sp_texture(const Gfx *cmd)           { (void)cmd; }
static void gfx_sp_move_mem(const Gfx *cmd)          { (void)cmd; }
static void gfx_sp_move_word(const Gfx *cmd)         { (void)cmd; }
static void gfx_rdp_set_other_mode_h(const Gfx *cmd) { (void)cmd; }
static void gfx_rdp_set_other_mode_l(const Gfx *cmd) { (void)cmd; }
static void gfx_rdp_set_texture_image(const Gfx *cmd){ (void)cmd; }
static void gfx_rdp_set_tile(const Gfx *cmd)         { (void)cmd; }
static void gfx_rdp_load_tile(const Gfx *cmd)        { (void)cmd; }
static void gfx_rdp_load_block(const Gfx *cmd)       { (void)cmd; }
static void gfx_rdp_set_tile_size(const Gfx *cmd)    { (void)cmd; }
static void gfx_rdp_load_tlut(const Gfx *cmd)        { (void)cmd; }
static void gfx_rdp_set_combine(const Gfx *cmd)      { (void)cmd; }
static void gfx_rdp_set_env_color(const Gfx *cmd)    { (void)cmd; }
static void gfx_rdp_set_prim_color(const Gfx *cmd)   { (void)cmd; }
static void gfx_rdp_set_blend_color(const Gfx *cmd)  { (void)cmd; }
static void gfx_rdp_set_fog_color(const Gfx *cmd)    { (void)cmd; }
static void gfx_rdp_set_fill_color(const Gfx *cmd)   { (void)cmd; }
static void gfx_rdp_fill_rect(const Gfx *cmd)        { (void)cmd; }
// cmd points to the G_TEXRECT entry; cmd+1 = RDPHALF_1, cmd+2 = RDPHALF_2.
static void gfx_rdp_tex_rect(const Gfx *cmd)         { (void)cmd; }
static void gfx_rdp_set_scissor(const Gfx *cmd)      { (void)cmd; }
static void gfx_rdp_set_color_image(const Gfx *cmd)  { (void)cmd; }
static void gfx_rdp_set_z_image(const Gfx *cmd)      { (void)cmd; }

void gbi_init(void) {
    rdp_state_init();
}

void gbi_run_dl(Gfx *dl) {
#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1u << (width)) - 1u))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1u << (width)) - 1u))

    Gfx *stack[GFX_DL_STACK_DEPTH];
    int  stack_depth = 0;
    Gfx *cmd = dl;

    while (1) {
        u8 opcode = (u8)(cmd->words.w0 >> 24);

        switch (opcode) {
            case G_NOOP:
            case G_SPNOOP:
                break;

            case G_DL: {
                Gfx *target = (Gfx *)(uintptr_t)cmd->words.w1;
                if (C0(16, 8) == G_DL_PUSH && stack_depth < GFX_DL_STACK_DEPTH) {
                    stack[stack_depth++] = cmd + 1;
                }
                cmd = target;
                continue;
            }

            case G_ENDDL:
                if (stack_depth == 0) return;
                cmd = stack[--stack_depth];
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
                              (const Vtx *)(uintptr_t)cmd->words.w1);
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
            case G_MTX:    gfx_sp_matrix((u8)(C0(0, 8) ^ G_MTX_PUSH), (const s32 *)(uintptr_t)cmd->words.w1); break;
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

            // Texture rectangle: 3 Gfx words: TEXRECT, RDPHALF_1, RDPHALF_2.
            // Advance past the two trailing RDPHALF words before the loop's cmd++.
            case G_TEXRECT:
            case G_TEXRECTFLIP:
                gfx_rdp_tex_rect(cmd);
                cmd += 2;
                break;

            case G_SETSCISSOR:     gfx_rdp_set_scissor(cmd);      break;
            case G_SETCIMG:        gfx_rdp_set_color_image(cmd);  break;
            case G_SETZIMG:        gfx_rdp_set_z_image(cmd);      break;

            case G_RDPFULLSYNC:
            case G_RDPTILESYNC:
            case G_RDPPIPESYNC:
            case G_RDPLOADSYNC:
                break;

            default:
                fprintf(stderr, "gbi: unknown opcode 0x%02X\n", opcode);
                break;
        }

        cmd++;
    }

#undef C0
#undef C1
}
