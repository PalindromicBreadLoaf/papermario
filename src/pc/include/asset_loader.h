#ifndef ASSET_LOADER_H
#define ASSET_LOADER_H

#include <PR/ultratypes.h>

void asset_loader_init(const char *rom_path);
void asset_loader_dma_read(u32 rom_offset, void *dest, u32 size);
void asset_loader_shutdown(void);

u32 asset_loader_rom_size(void);
const void *asset_loader_rom_ptr(u32 rom_offset, u32 size);

#endif /* ASSET_LOADER_H */
