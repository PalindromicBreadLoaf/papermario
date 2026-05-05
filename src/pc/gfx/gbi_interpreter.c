#include <stdio.h>
#include <stdint.h>
#include "gbi_interpreter.h"
#include "rdp_state.h"
#include "gl_backend.h"

// TODO: Unstub these
static void gfx_sp_vertex(const Gfx *cmd)            { (void)cmd; }
static void gfx_sp_modify_vertex(const Gfx *cmd)     { (void)cmd; }
static void gfx_sp_cull_dl(const Gfx *cmd)           { (void)cmd; }
static void gfx_sp_branch_z(const Gfx *cmd)          { (void)cmd; }
static void gfx_sp_tri1(const Gfx *cmd)              { (void)cmd; }
static void gfx_sp_tri2(const Gfx *cmd)              { (void)cmd; }
static void gfx_sp_quad(const Gfx *cmd)              { (void)cmd; }
static void gfx_sp_matrix(const Gfx *cmd)            { (void)cmd; }
static void gfx_sp_pop_matrix(const Gfx *cmd)        { (void)cmd; }
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

            case G_VTX:            gfx_sp_vertex(cmd);            break;
            case G_MODIFYVTX:      gfx_sp_modify_vertex(cmd);     break;
            case G_CULLDL:         gfx_sp_cull_dl(cmd);           break;
            case G_BRANCH_Z:       gfx_sp_branch_z(cmd);          break;
            case G_TRI1:           gfx_sp_tri1(cmd);              break;
            case G_TRI2:           gfx_sp_tri2(cmd);              break;
            case G_QUAD:           gfx_sp_quad(cmd);              break;
            case G_MTX:            gfx_sp_matrix(cmd);            break;
            case G_POPMTX:         gfx_sp_pop_matrix(cmd);        break;
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
