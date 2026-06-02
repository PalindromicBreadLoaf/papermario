#include "common.h"
#include "asset_loader.h"
#include "pc_address.h"

#include <stdio.h>
#include <string.h>

extern TextureHeader gCurrentTextureHeader;

#define PC_US_ASSET_TABLE_FIRST_ENTRY 0x01E40020u
#define PC_ASSET_HEADER_SIZE 0x1Cu
#define PC_US_SPRITE_SHADING_ROM_START 0x00315B80u
#define PC_US_SPRITE_SHADING_DATA_ROM_START 0x00315D50u

u32 __real_dma_copy(Addr romStart, Addr romEnd, void* vramDest);
s32 __real_dma_write(Addr romStart, Addr romEnd, void* vramDest);
void* pc_tlb_translate(void* vaddr);
void texture_cache_invalidate_all(void);

static u32 pc_bswap32(u32 value) {
    return ((value & 0x000000FFu) << 24)
        | ((value & 0x0000FF00u) << 8)
        | ((value & 0x00FF0000u) >> 8)
        | ((value & 0xFF000000u) >> 24);
}

static u32 pc_read_native32(const u8* data) {
    u32 value;

    memcpy(&value, data, sizeof(value));
    return value;
}

static void pc_write_native32(u8* data, u32 value) {
    memcpy(data, &value, sizeof(value));
}

static void pc_swap_asset_headers(u32 romStart, void* data, u32 length) {
    u8* header = data;

    if (romStart != PC_US_ASSET_TABLE_FIRST_ENTRY || length < PC_ASSET_HEADER_SIZE) {
        return;
    }

    for (u32 off = 0; off + PC_ASSET_HEADER_SIZE <= length; off += PC_ASSET_HEADER_SIZE) {
        pc_write_native32(header + off + 0x10, pc_bswap32(pc_read_native32(header + off + 0x10)));
        pc_write_native32(header + off + 0x14, pc_bswap32(pc_read_native32(header + off + 0x14)));
        pc_write_native32(header + off + 0x18, pc_bswap32(pc_read_native32(header + off + 0x18)));
    }
}

static void pc_swap_sprite_shading_offsets(u32 romStart, void* data, u32 length) {
    u8* bytes = data;

    if (romStart < PC_US_SPRITE_SHADING_ROM_START || romStart >= PC_US_SPRITE_SHADING_DATA_ROM_START) {
        return;
    }

    for (u32 off = 0; off + sizeof(u32) <= length; off += sizeof(u32)) {
        pc_write_native32(bytes + off, pc_bswap32(pc_read_native32(bytes + off)));
    }
}

static u16 pc_bswap16(u16 value) {
    return (u16)((value >> 8) | (value << 8));
}

// Endian swap likely texture headers
// before game code uses them to compute raster and palette sizes.
static bool pc_looks_like_texture_header_be(const u8* data) {
    if (data[0x20] > 0x04 || data[0x22] > 0x04 || data[0x24] > 0x04 || data[0x26] > 0x04) {
        return false;
    }
    return true;
}

static u8 pc_swap_bitfield_nibbles(u8 v) {
    return (u8)((v << 4) | (v >> 4));
}

// Convert a byte holding a big-endian `field_hi:6` + `field_lo:2` pair into little-endian layout
static u8 pc_swap_bitfield_6_2(u8 v) {
    u8 hi = (u8)((v >> 2) & 0x3F);
    u8 lo = (u8)(v & 0x03);
    return (u8)(hi | (lo << 6));
}

static void pc_swap_texture_header(void* dest, void* data, u32 length) {
    u8* header = data;

    if (length != sizeof(TextureHeader)) {
        return;
    }

    bool is_global = (dest == (void*)&gCurrentTextureHeader);
    bool looks_like = pc_looks_like_texture_header_be(header);

    if (!is_global && !looks_like) {
        return;
    }

    for (u32 off = 0x20; off < 0x28; off += sizeof(u16)) {
        u16 value;
        memcpy(&value, header + off, sizeof(value));
        value = pc_bswap16(value);
        memcpy(header + off, &value, sizeof(value));
    }

    header[0x2A] = pc_swap_bitfield_6_2(header[0x2A]);
    header[0x2B] = pc_swap_bitfield_nibbles(header[0x2B]);
    header[0x2C] = pc_swap_bitfield_nibbles(header[0x2C]);
    header[0x2D] = pc_swap_bitfield_nibbles(header[0x2D]);
    header[0x2E] = pc_swap_bitfield_nibbles(header[0x2E]);
}

u32 __wrap_dma_copy(Addr romStart, Addr romEnd, void* vramDest) {
    u32 length = (u8*)romEnd - (u8*)romStart;
    u32 romOffset = (u32)(uintptr_t)(u8*)romStart;

    if (length == 0 || pc_addr_is_n64_kseg(vramDest)) {
        return length;
    }

    void* destPtr = pc_tlb_translate(vramDest);

    asset_loader_dma_read(romOffset, destPtr, length);
    pc_swap_asset_headers(romOffset, destPtr, length);
    pc_swap_sprite_shading_offsets(romOffset, destPtr, length);
    pc_swap_texture_header(destPtr, destPtr, length);
    texture_cache_invalidate_all();
    return length;
}

s32 __wrap_dma_write(Addr romStart, Addr romEnd, void* vramDest) {
    u32 length = (u8*)romEnd - (u8*)romStart;
    (void)vramDest;
    return (s32)length;
}
