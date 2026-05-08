#include <PR/os_thread.h>
#include <PR/os_pi.h>
#include "asset_loader.h"

OSPiHandle *nuPiCartHandle = NULL;

void nuPiInit(void) {
}

void nuPiReadRom(u32 rom_addr, void *buf_ptr, u32 size) {
    asset_loader_dma_read(rom_addr, buf_ptr, size);
}

void nuPiReadRomOverlay(void *segment) {
    (void)segment;
}
