#include "common.h"
#include "effects.h"

extern EffectSharedData gEffectSharedData[15];

static bool pc_effect_is_loaded(s32 effectID) {
    for (s32 i = 0; i < ARRAY_COUNT(gEffectSharedData); i++) {
        EffectSharedData* sharedData = &gEffectSharedData[i];

        if ((sharedData->flags & FX_SHARED_DATA_LOADED) && sharedData->effectIndex == effectID) {
            return true;
        }
    }
    return false;
}

void shim_guRotateF(float mf[4][4], float a, float x, float y, float z) { guRotateF(mf, a, x, y, z); }
void shim_guTranslateF(float mf[4][4], float x, float y, float z) { guTranslateF(mf, x, y, z); }
void shim_guTranslate(Mtx* m, float x, float y, float z) { guTranslate(m, x, y, z); }
void shim_guScaleF(float mf[4][4], float x, float y, float z) { guScaleF(mf, x, y, z); }
void shim_guMtxCatF(float m[4][4], float n[4][4], float r[4][4]) { guMtxCatF(m, n, r); }
void shim_guMtxF2L(float mf[4][4], Mtx* m) { guMtxF2L(mf, m); }
RenderTask* shim_queue_render_task(RenderTask* task) { return queue_render_task(task); }
EffectInstance* shim_create_effect_instance(EffectBlueprint* effectBp) {
    if (!pc_effect_is_loaded(effectBp->effectID)) {
        load_effect(effectBp->effectID);
    }
    return create_effect_instance(effectBp);
}
void shim_remove_effect(EffectInstance* effect) { remove_effect(effect); }
void* shim_general_heap_malloc(s32 size) { return general_heap_malloc(size); }
void shim_mem_clear(void* data, s32 numBytes) { mem_clear(data, numBytes); }
s32 shim_rand_int(s32 max) { return rand_int(max); }
f32 shim_clamp_angle(f32 theta) { return clamp_angle(theta); }
f32 shim_sin_deg(f32 x) { return sin_deg(x); }
f32 shim_cos_deg(f32 x) { return cos_deg(x); }
f32 shim_atan2(f32 startX, f32 startZ, f32 endX, f32 endZ) { return atan2(startX, startZ, endX, endZ); }
bool shim_npc_raycast_down_sides(s32 ignoreFlags, f32* x, f32* y, f32* z, f32* length) {
    return npc_raycast_down_sides(ignoreFlags, x, y, z, length);
}
s32 shim_load_effect(s32 effectIndex) { return load_effect(effectIndex); }
float shim_sqrtf(float value) { return sqrtf(value); }
void shim_mdl_draw_hidden_panel_surface(Gfx** gfx, u16 treeIndex) { mdl_draw_hidden_panel_surface(gfx, treeIndex); }
bool shim_is_point_visible(f32 x, f32 y, f32 z, s32 arg3, f32* arg4, f32* arg5) {
    return is_point_visible(x, y, z, arg3, arg4, arg5);
}
void shim_guPerspectiveF(f32 mf[4][4], u16* perspNorm, f32 fovy, f32 aspect, f32 near, f32 far, f32 scale) {
    guPerspectiveF(mf, perspNorm, fovy, aspect, near, far, scale);
}
void shim_transform_point(Matrix4f mtx, f32 inX, f32 inY, f32 inZ, f32 inS, f32* outX, f32* outY, f32* outZ, f32* outW) {
    transform_point(mtx, inX, inY, inZ, inS, outX, outY, outZ, outW);
}
void shim_guPositionF(float mf[4][4], float r, float p, float h, float s, float x, float y, float z) {
    guPositionF(mf, r, p, h, s, x, y, z);
}
void shim_guOrthoF(float mf[4][4], float l, float r, float b, float t, float n, float f, float scale) {
    guOrthoF(mf, l, r, b, t, n, f, scale);
}
void shim_guFrustumF(float mf[4][4], float l, float r, float b, float t, float n, float f, float scale) {
    guFrustumF(mf, l, r, b, t, n, f, scale);
}
void shim_draw_prev_frame_buffer_at_screen_pos(s32 x, s32 y, s32 width, s32 height, f32 alpha) {
    draw_prev_frame_buffer_at_screen_pos(x, y, width, height, alpha);
}
void shim_draw_box(s32 flags, WindowStyle windowStyle, s32 posX, s32 posY, s32 posZ, s32 width, s32 height,
    u8 opacity, u8 darkening, f32 scaleX, f32 scaleY, f32 rotX, f32 rotY, f32 rotZ,
    void (*fpDrawContents)(void*), void* drawContentsArg0, Matrix4f rotScaleMtx, s32 translateX, s32 translateY,
    f32 (*outMtx)[4]) {
    draw_box(flags, windowStyle, posX, posY, posZ, width, height, opacity, darkening, scaleX, scaleY, rotX, rotY,
        rotZ, fpDrawContents, drawContentsArg0, rotScaleMtx, translateX, translateY, outMtx);
}
void shim_draw_msg(s32 msgID, s32 posX, s32 posY, s32 opacity, s32 palette, s32 style) {
    draw_msg(msgID, posX, posY, opacity, palette, style);
}
s32 shim_get_msg_width(s32 msgID, u16 flags) { return get_msg_width(msgID, flags); }
void shim_mdl_get_shroud_tint_params(u8* r, u8* g, u8* b, u8* a) { mdl_get_shroud_tint_params(r, g, b, a); }
void shim_sfx_play_sound_at_position(s32 soundID, s32 value2, f32 posX, f32 posY, f32 posZ) {
    sfx_play_sound_at_position(soundID, value2, posX, posY, posZ);
}
