#ifndef _EFFECTS_INTERNAL_H_
#define _EFFECTS_INTERNAL_H_

#include "effects.h"

s32 effect_rand_int(s32);
s32 effect_simple_rand(s32, s32);

#ifdef draw_box
#undef draw_box
#endif
#include "effect_shims.h"

#ifdef draw_box
#undef draw_box
#endif
#define draw_box(flags, windowStyle, ...) shim_draw_box(flags, PC_WINDOW_STYLE(windowStyle), __VA_ARGS__)

#endif
