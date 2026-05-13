#ifndef RDP_STATE_H
#define RDP_STATE_H

#include <stdbool.h>
#include <PR/ultratypes.h>
#include <PR/gbi.h>

// Maximum directional lights (not counting ambient). F3DEX2 supports up to 7;
// SM64's PC port uses 2, so hopefully that's enough here too.
#define GFX_MAX_LIGHTS    2
// Vertex cache size for gSPVertex (F3DEX2 supports up to 32, game uses <= 64).
#define GFX_MAX_VERTICES  64
// Maximum display-list call-stack depth for gSPDisplayList recursion.
#define GFX_DL_STACK_DEPTH 16

// Tile descriptor
// Mirrors one N64 tile slot, updated by gDPSetTile / gDPSetTileSize.

typedef struct {
    u8  fmt;
    u8  siz;
    u32 line_bytes;     // stride in bytes (line * 8)
    u16 uls, ult;       // upper-left in S10.5
    u16 lrs, lrt;       // lower-right in S10.5
    u8  cms, cmt;       // clamp/mirror/wrap flags
    u32 tmem_offset;    // TMEM word offset (tracking only, no TMEM limit enforced)
} TileDesc;

// Loaded-vertex cache
// One entry per slot in the RSP vertex cache, plus 4 extra for rectangle draws.

typedef struct {
    float x, y, z, w;   // clip-space position (MP_matrix already applied)
    float u, v;         // scaled texture coordinates (S10.5 units)
    u8    r, g, b, a;   // vertex color / fog-factor in alpha
    u8    clip_rej;     // trivial rejection bitmask (6 half-space bits)
} LoadedVertex;

// RSP state

typedef struct {
    // Matrix stack: modelview[0] is always the base, [depth-1] is current top.
    float modelview_stack[11][4][4];
    int   modelview_depth;      // 1-based; minimum 1

    float P_matrix[4][4];       // projection matrix
    float MP_matrix[4][4];      // cached MV_top × P (recomputed on any change)

    u32   geometry_mode;        // G_ZBUFFER, G_LIGHTING, G_CULL_*, G_FOG, …

    s16   fog_mul;              // from gSPFogFactor / G_MW_FOG
    s16   fog_offset;

    struct { u16 s, t; } tex_scale;  // U0.16, set by gSPTexture

    // Lights: [0 .. num_lights-2] are directional, [num_lights-1] is ambient.
    Light_t lights[GFX_MAX_LIGHTS + 1];
    int     num_lights;         // includes the ambient entry
    bool    lights_dirty;       // re-transform normals before next vertex batch

    // Transformed light direction coefficients (in eye space, normalised).
    float light_coeffs[GFX_MAX_LIGHTS][3];
    // Lookat-X and lookat-Y coefficients for G_TEXTURE_GEN.
    float lookat_coeffs[2][3];

    // Vertex cache (+ 4 slots at the end reserved for rectangle corners).
    LoadedVertex loaded_vertices[GFX_MAX_VERTICES + 4];

    // State machine for multi-word commands (G_TEXRECT, G_FILLRECT in F3DEX2).
    u32 saved_opcode;
    s32 saved_ulx, saved_uly, saved_lrx, saved_lry;
    u8  saved_tile;
    u16 saved_uls, saved_ult;

    // RSP segmented-address bases, set by gSPSegment/G_MW_SEGMENT.
    void *segments[16];
} RspState;

// RDP state

typedef struct {
    // Texture pipeline
    struct {
        const u8 *addr;      // pointer into ROM buffer set by gDPSetTextureImage
        u8        fmt;       // G_IM_FMT_* of the image being loaded
        u8        siz;       // G_IM_SIZ_* of the image being loaded
        int       tile_slot; // target tile slot (G_TX_LOADTILE → TMEM half)
    } tex_to_load;

    struct {
        const u8 *addr;
        u32       size_bytes;
        u16       width;
        u16       height;
        unsigned int tex_id;
    } loaded[2];             // currently loaded data for tile slots 0 and 1

    TileDesc tile[8];        // all 8 tile descriptors
    const u8 *tlut;          // RGBA5551 palette (big-endian), set by gDPLoadTLUT
    bool textures_dirty[2];  // set when load or tile-desc changes
    int active_texture_slot; // texture slot selected by the current render tile

    // Color registers
    u8 env_r,  env_g,  env_b,  env_a;
    u8 prim_r, prim_g, prim_b, prim_a;
    u8 fog_r,  fog_g,  fog_b,  fog_a;
    u8 fill_r, fill_g, fill_b, fill_a;

    // Combiner + render mode
    u32 combine_mode;    // packed combiner slot indices
    u32 other_mode_l;    // gDPSetRenderMode / gSPSetOtherModeL
    u32 other_mode_h;    // gDPSetCycleType  / gSPSetOtherModeH

    // Combiner slot indices for the mega-shader
    // Each is a small integer that the fragment shader indexes into a colour
    // source array. rgb = {0=tex0, 1=tex1, 2=shade, 3=prim, 4=env, 5=zero}.
    int cc_rgb_a, cc_rgb_b, cc_rgb_c, cc_rgb_d;
    int cc_a_a,  cc_a_b,  cc_a_c,  cc_a_d;

    // Viewport + scissor
    struct { float x, y, w, h; } viewport;
    struct { float x, y, w, h; } scissor;
    bool viewport_dirty;

    // Z-buffer bookkeeping
    void *z_buf_addr;
    void *color_buf_addr;

    // Set whenever other_mode_l changes; triggers a flush + GL state update before
    // the next triangle batch so render-mode boundaries are respected.
    bool blend_dirty;
} RdpState;

extern RspState g_rsp;
extern RdpState g_rdp;

void rdp_state_init(void);

#endif /* RDP_STATE_H */
