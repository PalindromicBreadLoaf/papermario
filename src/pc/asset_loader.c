#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asset_loader.h"

static u8 *sRomBuffer;
static u32 sRomSize;

void asset_loader_init(const char *rom_path) {
    FILE *fp = fopen(rom_path, "rb");
    if (!fp) {
        fprintf(stderr, "asset_loader: cannot open '%s'\n", rom_path);
        exit(1);
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);

    if (len <= 0) {
        fprintf(stderr, "asset_loader: empty or unreadable ROM '%s'\n", rom_path);
        fclose(fp);
        exit(1);
    }

    sRomSize = (u32)len;
    sRomBuffer = malloc(sRomSize);
    if (!sRomBuffer) {
        fprintf(stderr, "asset_loader: out of memory allocating %u bytes for ROM\n", sRomSize);
        fclose(fp);
        exit(1);
    }

    if (fread(sRomBuffer, 1, sRomSize, fp) != sRomSize) {
        fprintf(stderr, "asset_loader: short read on '%s'\n", rom_path);
        fclose(fp);
        exit(1);
    }

    fclose(fp);
    fprintf(stdout, "asset_loader: loaded ROM '%s' (%u bytes)\n", rom_path, sRomSize);
}

void asset_loader_dma_read(u32 rom_offset, void *dest, u32 size) {
    if (!sRomBuffer) {
        fprintf(stderr, "asset_loader: dma_read called before init\n");
        return;
    }
    if (rom_offset >= sRomSize || size > sRomSize - rom_offset) {
        fprintf(stderr, "asset_loader: out-of-bounds DMA read (offset=0x%08X size=%u rom_size=%u)\n",
                rom_offset, size, sRomSize);
        return;
    }
    memcpy(dest, sRomBuffer + rom_offset, size);
}

void asset_loader_shutdown(void) {
    free(sRomBuffer);
    sRomBuffer = NULL;
    sRomSize = 0;
}

u32 asset_loader_rom_size(void) {
    return sRomSize;
}

const void *asset_loader_rom_ptr(u32 rom_offset, u32 size) {
    if (!sRomBuffer || rom_offset >= sRomSize || size > sRomSize - rom_offset) {
        return NULL;
    }
    return sRomBuffer + rom_offset;
}
