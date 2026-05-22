#ifndef _PC_PRELUDE_H_
#define _PC_PRELUDE_H_

// Force-included before every translation unit in the PC build.
// Pre-defines the include_asm.h guard so the original include/include_asm.h is
// skipped when pulled in transitively through include/macros.h.
#define __INCLUDE_ASM_H__
#define INCLUDE_ASM(TYPE, FOLDER, NAME, ARGS...)
#define INCLUDE_ASM_SHIFT(TYPE, FOLDER, NAME, ARGS...)

#if !defined(__ASSEMBLER__)
#include <stdint.h>
#endif

#if !defined(__cplusplus) && !defined(__ASSEMBLER__)
#ifdef s32
#define PC_RESTORE_WIDE_S32
#undef s32
#endif
#ifdef u32
#define PC_RESTORE_WIDE_U32
#undef u32
#endif
#include "evt.h"
#ifdef PC_GAME_SOURCE
#include "common_structs.h"
#endif
#ifdef PC_RESTORE_WIDE_S32
#define s32 intptr_t
#undef PC_RESTORE_WIDE_S32
#endif
#ifdef PC_RESTORE_WIDE_U32
#define u32 uintptr_t
#undef PC_RESTORE_WIDE_U32
#endif
#ifdef PC_GAME_SOURCE
#include "common.h"

#undef PHYSICAL_TO_VIRTUAL
#undef VIRTUAL_TO_PHYSICAL
#define PHYSICAL_TO_VIRTUAL(addr) ((void *)(uintptr_t)(addr))
#define VIRTUAL_TO_PHYSICAL(addr) ((uintptr_t)(addr))

static inline WindowStyle pc_window_style_from_union(WindowStyle style) {
    return style;
}

static inline WindowStyle pc_window_style_from_custom(WindowStyleCustom* style) {
    return (WindowStyle){ .customStyle = style };
}

static inline WindowStyle pc_window_style_from_const_custom(const WindowStyleCustom* style) {
    return (WindowStyle){ .customStyle = (WindowStyleCustom*)style };
}

static inline WindowStyle pc_window_style_from_int(int style) {
    return (WindowStyle){ .defaultStyleID = style };
}

#define PC_WINDOW_STYLE(style) _Generic((style), \
    WindowStyle: pc_window_style_from_union, \
    WindowStyleCustom*: pc_window_style_from_custom, \
    const WindowStyleCustom*: pc_window_style_from_const_custom, \
    default: pc_window_style_from_int)(style)
#ifndef PC_BUILDING_DRAW_BOX
#define draw_box(flags, windowStyle, ...) draw_box(flags, PC_WINDOW_STYLE(windowStyle), __VA_ARGS__)
#endif
#ifndef PC_BUILDING_WINDOWS
#define replace_window_update(idx, priority, pendingFunc) \
    replace_window_update(idx, priority, (WindowUpdateFunc){ .func = (pendingFunc) })
#endif
#endif
#endif

#endif // _PC_PRELUDE_H_
