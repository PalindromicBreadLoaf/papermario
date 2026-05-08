#include "common.h"
#include "asset_loader.h"

u32 __real_dma_copy(Addr romStart, Addr romEnd, void* vramDest);
s32 __real_dma_write(Addr romStart, Addr romEnd, void* vramDest);

u32 __wrap_dma_copy(Addr romStart, Addr romEnd, void* vramDest) {
    u32 length = (u8*)romEnd - (u8*)romStart;
    uintptr_t dest = (uintptr_t)vramDest;

    if (length == 0 || (dest >= 0x80000000UL && dest < 0xC0000000UL)) {
        return length;
    }

    asset_loader_dma_read((u32)(uintptr_t)(u8*)romStart, vramDest, length);
    return length;
}

s32 __wrap_dma_write(Addr romStart, Addr romEnd, void* vramDest) {
    u32 length = (u8*)romEnd - (u8*)romStart;
    (void)vramDest;
    return (s32)length;
}
