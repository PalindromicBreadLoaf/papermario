#include "common.h"
#include "model.h"
#include "nu/nusys.h"
#include "sprite.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

typedef struct PcBackgroundImageStorage {
    BackgroundHeader header;
    u8 data[0x40000];
} PcBackgroundImageStorage;

PcBackgroundImageStorage gBackgroundImageStorage = { 0 };
extern BackgroundHeader gBackgroundImage __attribute__((alias("gBackgroundImageStorage")));
HeapNode heap_battleHead;
Addr sprite_shading_profiles_data_ROM_START;
u64 n_aspMainTextStart[1];
u64 n_aspMainDataStart[1];

extern HeapNode heap_spriteHead;

static u32 pc_low32_ptr(const void* ptr) {
    return (u32)(uintptr_t)ptr;
}

SpriteAnimData* spr_load_sprite(s32 idx, s32 isPlayerSprite, s32 useTailAlloc) {
    u32* header = _heap_malloc(&heap_spriteHead, 5 * sizeof(*header));
    u32* emptyList = _heap_malloc(&heap_spriteHead, sizeof(*emptyList));

    (void)idx;
    (void)isPlayerSprite;
    (void)useTailAlloc;

    emptyList[0] = (u32)(uintptr_t)PTR_LIST_END;
    header[0] = pc_low32_ptr(emptyList);
    header[1] = 0;
    header[2] = 0;
    header[3] = 0;
    header[4] = 0;
    return (SpriteAnimData*)header;
}

void spr_load_npc_extra_anims(SpriteAnimData* header, u32* extraAnimList) {
    (void)header;
    (void)extraAnimList;
}

void osSetTime(OSTime time) {
    (void)time;
}

void* __osGetActiveQueue(void) {
    return NULL;
}

int _Printf(outfun prout, char* arg, const char* fmt, va_list ap) {
    char buffer[2048];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, ap);

    if (len > 0 && prout != NULL) {
        prout(arg, buffer, (size_t)len);
    }
    return len;
}

void is_debug_init(void) {
}

OSPiHandle* osFlashInit(void) {
    return NULL;
}

s32 osFlashReadArray(OSIoMesg* mb, s32 priority, u32 pageNum, void* dramAddr, u32 nPages, OSMesgQueue* mq) {
    (void)mb;
    (void)priority;
    (void)pageNum;
    (void)nPages;
    mem_clear(dramAddr, (s32)(nPages * 128));
    osSendMesg(mq, NULL, OS_MESG_BLOCK);
    return 0;
}

s32 osFlashWriteBuffer(OSIoMesg* mb, s32 priority, void* dramAddr, OSMesgQueue* mq) {
    (void)mb;
    (void)priority;
    (void)dramAddr;
    osSendMesg(mq, NULL, OS_MESG_BLOCK);
    return 0;
}

s32 osFlashWriteArray(u32 pageNum) {
    (void)pageNum;
    return 0;
}

s32 osFlashSectorErase(u32 pageNum) {
    (void)pageNum;
    return 0;
}

void load_obfuscation_shims(void) {
}

void create_audio_system_obfuscated(void) {
    create_audio_system();
}

void load_engine_data_obfuscated(void) {
    load_engine_data();
}

void general_heap_create_obfuscated(void) {
    general_heap_create();
}

void battle_heap_create_obfuscated(void) {
    battle_heap_create();
}

void fx_sun_undeclared(s32 mode, s32 x, s32 y, s32 z, s32 scale, s32 duration) {
    fx_sun(mode, x, y, z, scale, duration);
}

s32 nuContRmbCheck(u32 contNo) {
    (void)contNo;
    return 0;
}

void nuContRmbModeSet(u32 contNo, u8 mode) {
    (void)contNo;
    (void)mode;
}

void nuContRmbStart(u32 contNo, u16 freq, u16 frame) {
    (void)contNo;
    (void)freq;
    (void)frame;
}

void nuContRmbForceStop(void) {
}

void nuContRmbForceStopEnd(void) {
}
