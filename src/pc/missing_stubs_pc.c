#include "common.h"
#include "model.h"
#include "nu/nusys.h"

#include <stdarg.h>
#include <stdio.h>

BackgroundHeader gBackgroundImage;
HeapNode heap_battleHead;
Addr sprite_shading_profiles_data_ROM_START;
u64 n_aspMainTextStart[1];
u64 n_aspMainDataStart[1];

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
    (void)mq;
    mem_clear(dramAddr, (s32)(nPages * 128));
    return 0;
}

s32 osFlashWriteBuffer(OSIoMesg* mb, s32 priority, void* dramAddr, OSMesgQueue* mq) {
    (void)mb;
    (void)priority;
    (void)dramAddr;
    (void)mq;
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
