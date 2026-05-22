#include "common.h"
#include "model.h"
#include "nu/nusys.h"
#include "sprite.h"
#include "sprite/player.h"
#include "syn_driver_pc.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PcBackgroundImageStorage {
    BackgroundHeader header;
    u8 data[0x40000];
} PcBackgroundImageStorage;

PcBackgroundImageStorage gBackgroundImageStorage = { 0 };
extern BackgroundHeader gBackgroundImage __attribute__((alias("gBackgroundImageStorage")));
u64 n_aspMainTextStart[1];
u64 n_aspMainDataStart[1];

typedef struct PcBattleHeapStorage {
    HeapNode head;
    u8 data[BATTLE_HEAP_SIZE - sizeof(HeapNode)];
} PcBattleHeapStorage;

PcBattleHeapStorage gPcBattleHeapStorage __attribute__((aligned(16))) = { 0 };
extern HeapNode heap_battleHead __attribute__((alias("gPcBattleHeapStorage")));

extern HeapNode heap_spriteHead;

extern s32 PlayerRasterLoadDescBuffer[101];
extern s32 PlayerRasterLoadDescNumLoaded;
extern s32 PlayerRasterLoadDescBeginSpriteIndex[SPR_Peach3];
extern s32 PlayerRasterLoadDesc[0x2E0];
extern PlayerRastersHeader PlayerRasterHeader;
extern s32 PlayerSpriteRasterSets[SPR_Peach3 + 1];
extern s32 SpriteDataHeader[3];

typedef struct PcSpriteRasterCacheEntryRaw {
    u32 image;
    u8 width;
    u8 height;
    s8 palette;
    s8 quadCacheIndex;
} PcSpriteRasterCacheEntryRaw;

typedef struct PcSpriteAnimComponentRaw {
    u32 cmdList;
    s16 cmdListSize;
    Vec3s compOffset;
} PcSpriteAnimComponentRaw;

static u32 pc_read_native32(const void* ptr) {
    const u8* data = ptr;

    return ((u32)data[3] << 24) | ((u32)data[2] << 16) | ((u32)data[1] << 8) | data[0];
}

static u16 pc_read_be16(const void* ptr) {
    const u8* data = ptr;

    return ((u16)data[0] << 8) | data[1];
}

static u32 pc_read_be32(const void* ptr) {
    const u8* data = ptr;

    return ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
}

static u32 pc_read_sprite_offset(const void* ptr, u32 rawSize) {
    u32 be = pc_read_be32(ptr);
    u32 native = pc_read_native32(ptr);

    if (be == (u32)-1 || native == (u32)-1) {
        return (u32)-1;
    }
    if (be < rawSize) {
        return be;
    }
    if (native < rawSize) {
        return native;
    }
    return be;
}

static u16* pc_copy_sprite_cmd_list(const u8* rawBase, u32 offset, s32 size) {
    s32 count = size / sizeof(u16);
    u16* cmdList = malloc(count * sizeof(*cmdList));
    const u8* src = rawBase + offset;

    for (s32 i = 0; i < count; i++) {
        cmdList[i] = pc_read_be16(src + i * sizeof(u16));
    }

    return cmdList;
}

static SpriteAnimComponent** pc_convert_sprite_component_list(const u8* rawBase, u32 rawSize, u32 offset) {
    const u8* rawList = rawBase + offset;
    s32 count = 0;

    while (pc_read_sprite_offset(rawList + count * sizeof(u32), rawSize) != (u32)-1) {
        count++;
    }

    SpriteAnimComponent** list = malloc((count + 1) * sizeof(*list));

    for (s32 i = 0; i < count; i++) {
        u32 componentOffset = pc_read_sprite_offset(rawList + i * sizeof(u32), rawSize);
        const u8* rawComponent = rawBase + componentOffset;
        s32 cmdListSize = (s16)pc_read_be16(rawComponent + offsetof(PcSpriteAnimComponentRaw, cmdListSize));
        SpriteAnimComponent* component = malloc(sizeof(*component));

        component->cmdList = pc_copy_sprite_cmd_list(rawBase, pc_read_sprite_offset(rawComponent, rawSize), cmdListSize);
        component->cmdListSize = cmdListSize;
        component->compOffset.x = (s16)pc_read_be16(rawComponent + offsetof(PcSpriteAnimComponentRaw, compOffset.x));
        component->compOffset.y = (s16)pc_read_be16(rawComponent + offsetof(PcSpriteAnimComponentRaw, compOffset.y));
        component->compOffset.z = (s16)pc_read_be16(rawComponent + offsetof(PcSpriteAnimComponentRaw, compOffset.z));
        list[i] = component;
    }

    list[count] = PTR_LIST_END;
    return list;
}

static SpriteRasterCacheEntry** pc_convert_sprite_rasters(const u8* rawBase, u32 rawSize, u32 offset) {
    const u8* rawList = rawBase + offset;
    s32 count = 0;

    while (pc_read_sprite_offset(rawList + count * sizeof(u32), rawSize) != (u32)-1) {
        count++;
    }

    SpriteRasterCacheEntry** list = malloc((count + 1) * sizeof(*list));

    for (s32 i = 0; i < count; i++) {
        u32 rasterOffset = pc_read_sprite_offset(rawList + i * sizeof(u32), rawSize);
        const u8* rawRaster = rawBase + rasterOffset;
        SpriteRasterCacheEntry* raster = malloc(sizeof(*raster));

        raster->image =
            (IMG_PTR)(rawBase + pc_read_sprite_offset(rawRaster + offsetof(PcSpriteRasterCacheEntryRaw, image), rawSize));
        raster->width = rawRaster[offsetof(PcSpriteRasterCacheEntryRaw, width)];
        raster->height = rawRaster[offsetof(PcSpriteRasterCacheEntryRaw, height)];
        raster->palette = (s8)rawRaster[offsetof(PcSpriteRasterCacheEntryRaw, palette)];
        raster->quadCacheIndex = (s8)rawRaster[offsetof(PcSpriteRasterCacheEntryRaw, quadCacheIndex)];
        list[i] = raster;
    }

    list[count] = PTR_LIST_END;
    return list;
}

static PAL_PTR* pc_convert_sprite_palettes(const u8* rawBase, u32 rawSize, u32 offset) {
    const u8* rawList = rawBase + offset;
    s32 count = 0;

    while (pc_read_sprite_offset(rawList + count * sizeof(u32), rawSize) != (u32)-1) {
        count++;
    }

    PAL_PTR* list = malloc((count + 1) * sizeof(*list));

    for (s32 i = 0; i < count; i++) {
        list[i] = (PAL_PTR)(rawBase + pc_read_sprite_offset(rawList + i * sizeof(u32), rawSize));
    }
    list[count] = PTR_LIST_END;

    return list;
}

static SpriteAnimData* pc_convert_sprite_anim_data(u8* rawData, u32 rawSize) {
    const u8* rawBase = rawData;
    s32 animCount = 0;
    u32 headerSize;
    SpriteAnimData* animData;

    while (pc_read_sprite_offset(rawBase + (4 + animCount) * sizeof(u32), rawSize) != (u32)-1) {
        animCount++;
    }

    headerSize = offsetof(SpriteAnimData, animListStart) + (animCount + 1) * sizeof(SpriteAnimComponent**);
    animData = _heap_malloc(&heap_spriteHead, headerSize);
    animData->rastersOffset = pc_convert_sprite_rasters(rawBase, rawSize, pc_read_sprite_offset(rawBase, rawSize));
    animData->palettesOffset =
        pc_convert_sprite_palettes(rawBase, rawSize, pc_read_sprite_offset(rawBase + sizeof(u32), rawSize));
    animData->maxComponents = pc_read_be32(rawBase + 2 * sizeof(u32));
    animData->colorVariations = pc_read_be32(rawBase + 3 * sizeof(u32));

    for (s32 i = 0; i < animCount; i++) {
        animData->animListStart[i] = pc_convert_sprite_component_list(
            rawBase, rawSize, pc_read_sprite_offset(rawBase + (4 + i) * sizeof(u32), rawSize));
    }
    animData->animListStart[animCount] = PTR_LIST_END;

    return animData;
}

SpriteAnimData* spr_load_sprite(s32 idx, s32 isPlayerSprite, s32 useTailAlloc) {
    u32 base = isPlayerSprite ? SpriteDataHeader[1] : SpriteDataHeader[2];
    u32 entry[2];
    u32 compressedSize;
    u32 rawSize;
    u8* compressedData;
    u8* rawData;
    SpriteAnimData* animData;

    nuPiReadRom(base + idx * sizeof(u32), entry, sizeof(entry));
    compressedSize = ALIGN8(entry[1] - entry[0]);
    compressedData = general_heap_malloc(compressedSize);
    nuPiReadRom(base + entry[0], compressedData, compressedSize);

    rawSize = ((u32*)compressedData)[1];
    (void)useTailAlloc;

    rawData = malloc(rawSize);
    decode_yay0(compressedData, rawData);
    general_heap_free(compressedData);

    animData = pc_convert_sprite_anim_data(rawData, rawSize);

    if (isPlayerSprite) {
        s32 count;

        PlayerRasterLoadDescBeginSpriteIndex[idx] = PlayerRasterLoadDescNumLoaded;
        count = PlayerSpriteRasterSets[idx + 1] - PlayerSpriteRasterSets[idx];
        nuPiReadRom(SpriteDataHeader[0] + PlayerRasterHeader.loadDescriptors + sizeof(u32) * PlayerSpriteRasterSets[idx],
            PlayerRasterLoadDescBuffer, sizeof(PlayerRasterLoadDescBuffer));
        for (s32 i = 0; i < count; i++) {
            PlayerRasterLoadDesc[PlayerRasterLoadDescNumLoaded++] = PlayerRasterLoadDescBuffer[i];
        }
    }

    return animData;
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

// File-backed 128 KB N64 flash image.
#define PC_FLASH_PAGE_SIZE   128
#define PC_FLASH_TOTAL_BYTES 0x20000
#define PC_FLASH_TOTAL_PAGES (PC_FLASH_TOTAL_BYTES / PC_FLASH_PAGE_SIZE)
#define PC_FLASH_SECTOR_PAGES 128

static u8 pc_flash_image[PC_FLASH_TOTAL_BYTES];
static u8 pc_flash_write_buf[PC_FLASH_PAGE_SIZE];
static bool pc_flash_loaded = false;

static const char* pc_flash_path(void) {
    static char path[1024];
    const char* override = getenv("PM64_SAVE");
    if (override != NULL && override[0] != '\0') {
        snprintf(path, sizeof(path), "%s", override);
    } else {
        snprintf(path, sizeof(path), "papermario_save.bin");
    }
    return path;
}

static void pc_flash_load_image(void) {
    if (pc_flash_loaded) {
        return;
    }
    pc_flash_loaded = true;
    memset(pc_flash_image, 0, sizeof(pc_flash_image));

    FILE* fp = fopen(pc_flash_path(), "rb");
    if (fp == NULL) {
        return;
    }
    fread(pc_flash_image, 1, sizeof(pc_flash_image), fp);
    fclose(fp);
}

static void pc_flash_persist_image(void) {
    FILE* fp = fopen(pc_flash_path(), "wb");
    if (fp == NULL) {
        return;
    }
    fwrite(pc_flash_image, 1, sizeof(pc_flash_image), fp);
    fclose(fp);
}

OSPiHandle* osFlashInit(void) {
    pc_flash_load_image();
    return NULL;
}

s32 osFlashReadArray(OSIoMesg* mb, s32 priority, u32 pageNum, void* dramAddr, u32 nPages, OSMesgQueue* mq) {
    (void)mb;
    (void)priority;

    pc_flash_load_image();

    u32 byteOffset = pageNum * PC_FLASH_PAGE_SIZE;
    u32 byteCount = nPages * PC_FLASH_PAGE_SIZE;

    if (byteOffset >= PC_FLASH_TOTAL_BYTES) {
        memset(dramAddr, 0, byteCount);
    } else {
        u32 available = PC_FLASH_TOTAL_BYTES - byteOffset;
        u32 toCopy = byteCount < available ? byteCount : available;
        memcpy(dramAddr, &pc_flash_image[byteOffset], toCopy);
        if (toCopy < byteCount) {
            memset((u8*)dramAddr + toCopy, 0, byteCount - toCopy);
        }
    }

    osSendMesg(mq, NULL, OS_MESG_BLOCK);
    return 0;
}

s32 osFlashWriteBuffer(OSIoMesg* mb, s32 priority, void* dramAddr, OSMesgQueue* mq) {
    (void)mb;
    (void)priority;

    if (dramAddr != NULL) {
        memcpy(pc_flash_write_buf, dramAddr, PC_FLASH_PAGE_SIZE);
    }
    osSendMesg(mq, NULL, OS_MESG_BLOCK);
    return 0;
}

s32 osFlashWriteArray(u32 pageNum) {
    pc_flash_load_image();

    u32 byteOffset = pageNum * PC_FLASH_PAGE_SIZE;
    if (byteOffset >= PC_FLASH_TOTAL_BYTES) {
        return 0;
    }

    memcpy(&pc_flash_image[byteOffset], pc_flash_write_buf, PC_FLASH_PAGE_SIZE);
    pc_flash_persist_image();
    return 0;
}

s32 osFlashSectorErase(u32 pageNum) {
    pc_flash_load_image();

    u32 sectorStart = (pageNum / PC_FLASH_SECTOR_PAGES) * PC_FLASH_SECTOR_PAGES;
    u32 byteOffset = sectorStart * PC_FLASH_PAGE_SIZE;
    u32 byteCount = PC_FLASH_SECTOR_PAGES * PC_FLASH_PAGE_SIZE;

    if (byteOffset >= PC_FLASH_TOTAL_BYTES) {
        return 0;
    }
    if (byteOffset + byteCount > PC_FLASH_TOTAL_BYTES) {
        byteCount = PC_FLASH_TOTAL_BYTES - byteOffset;
    }
    memset(&pc_flash_image[byteOffset], 0, byteCount);
    pc_flash_persist_image();
    return 0;
}

void load_obfuscation_shims(void) {
}

void create_audio_system_obfuscated(void) {
    create_audio_system();
    pc_syn_set_audio_clients_ready();
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
