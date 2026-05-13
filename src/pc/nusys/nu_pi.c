#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <PR/os_thread.h>
#include <PR/os_pi.h>
#include "asset_loader.h"

OSPiHandle *nuPiCartHandle = NULL;

#define PC_US_AUDIO_ROM_START 0x00F00000u
#define PC_US_SPRITE_ROM_START 0x01943010u
#define PC_US_ROM_SIZE 0x02800000u

static u32 sAudioFileListStart;
static u32 sAudioFileListEnd;
static u32 sAudioInitBankListStart;
static u32 sAudioInitBankListEnd;
static u32 sAudioInitSongListStart;
static u32 sAudioInitSongListEnd;
static u32 sAudioInitExtraListStart;
static u32 sAudioInitExtraListEnd;
static u32 sSpriteRasterHeaderStart;
static u32 sSpritePlayerTableStart;
static u32 sSpriteNpcTableStart;
static u32 sSpriteRasterIndexRangesStart;
static u32 sSpriteRasterIndexRangesEnd;
static u32 sSpriteRasterLoadDescStart;
static u32 sSpriteRasterLoadDescEnd;

static u16 pc_bswap16(u16 value) {
    return (u16)((value >> 8) | (value << 8));
}

static u32 pc_bswap32(u32 value) {
    return ((value & 0x000000FFu) << 24)
        | ((value & 0x0000FF00u) << 8)
        | ((value & 0x00FF0000u) >> 8)
        | ((value & 0xFF000000u) >> 24);
}

static u16 pc_read_be16(const u8 *data) {
    return (u16)((data[0] << 8) | data[1]);
}

static u32 pc_read_be32(const u8 *data) {
    return ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
}

static void pc_swap_u16_range(void *buffer, u32 size) {
    u16 *values = buffer;

    for (u32 i = 0; i < size / sizeof(u16); i++) {
        values[i] = pc_bswap16(values[i]);
    }
}

static void pc_swap_sbn_file_list(void *buffer, u32 size) {
    u32 *values = buffer;

    for (u32 i = 0; i < size / sizeof(u32); i++) {
        values[i] = pc_bswap32(values[i]);
    }
}

static void pc_swap_u32_range(void *buffer, u32 size) {
    u32 *values = buffer;

    for (u32 i = 0; i < size / sizeof(u32); i++) {
        values[i] = pc_bswap32(values[i]);
    }
}

static void pc_swap_init_bank_list(void *buffer, u32 size) {
    u8 *data = buffer;

    for (u32 off = 0; off + 1 < size; off += 4) {
        *(u16 *)(data + off) = pc_bswap16(*(u16 *)(data + off));
    }
}

// Swap an Instrument struct (0x30 bytes) at the given offset within the BK file.
static void pc_swap_bk_instrument(u8 *inst) {
    for (u32 off = 0x00; off < 0x1C; off += 4) {
        *(u32 *)(inst + off) = pc_bswap32(*(u32 *)(inst + off));
    }
    *(u16 *)(inst + 0x1C) = pc_bswap16(*(u16 *)(inst + 0x1C));
    *(u16 *)(inst + 0x1E) = pc_bswap16(*(u16 *)(inst + 0x1E));
    *(u32 *)(inst + 0x20) = pc_bswap32(*(u32 *)(inst + 0x20));
    *(u32 *)(inst + 0x2C) = pc_bswap32(*(u32 *)(inst + 0x2C));
}

// Swap a single EnvelopePreset header: keep the u8 `count` byte, swap the
// `count` (u16 press, u16 release) offset pairs that follow at +4. Returns
// the EnvelopeOffset count actually written.
static u32 pc_swap_envelope_preset(u8 *data, u32 size, u32 preset_off) {
    if (preset_off + 4 > size) return 0;
    u8 count = data[preset_off];
    if (count == 0 || count > 64) return 0;
    u32 list_off = preset_off + 4;
    u32 list_bytes = (u32)count * 4;
    if (list_off + list_bytes > size) return 0;
    for (u32 j = 0; j < count; j++) {
        u32 entry = list_off + j * 4;
        *(u16 *)(data + entry + 0) = pc_bswap16(*(u16 *)(data + entry + 0));
        *(u16 *)(data + entry + 2) = pc_bswap16(*(u16 *)(data + entry + 2));
    }
    return count;
}

static void pc_swap_bk_header(void *buffer, u32 size) {
    u8 *data = buffer;

    if (size < 0x40 || memcmp(data, "BK", 2) != 0) {
        return;
    }

    *(u16 *)(data + 0x00) = pc_bswap16(*(u16 *)(data + 0x00));
    *(u32 *)(data + 0x04) = pc_bswap32(*(u32 *)(data + 0x04));
    *(u32 *)(data + 0x08) = pc_bswap32(*(u32 *)(data + 0x08));
    *(u16 *)(data + 0x0C) = pc_bswap16(*(u16 *)(data + 0x0C));
    pc_swap_u16_range(data + 0x12, 0x20);
    pc_swap_u16_range(data + 0x32, 0x0E);

    if (size <= 0x40) {
        return;
    }

    u16 instr_offs[16];
    for (int i = 0; i < 16; i++) {
        instr_offs[i] = *(u16 *)(data + 0x12 + i * 2);
    }
    u16 loop_states_start  = *(u16 *)(data + 0x34);
    u16 loop_states_length = *(u16 *)(data + 0x36);
    u16 predictors_start   = *(u16 *)(data + 0x38);
    u16 predictors_length  = *(u16 *)(data + 0x3A);

    u32 envelope_offs[16];
    u32 env_count = 0;
    for (int i = 0; i < 16; i++) {
        u16 io = instr_offs[i];
        if (io == 0 || (u32)io + 0x30 > size) continue;
        u8 *inst = data + io;
        pc_swap_bk_instrument(inst);

        u32 env_off = *(u32 *)(inst + 0x2C);
        if (env_off == 0 || env_off >= size) continue;
        bool seen = false;
        for (u32 j = 0; j < env_count; j++) {
            if (envelope_offs[j] == env_off) { seen = true; break; }
        }
        if (!seen && env_count < 16) envelope_offs[env_count++] = env_off;
    }

    for (u32 i = 0; i < env_count; i++) {
        pc_swap_envelope_preset(data, size, envelope_offs[i]);
    }

    if (loop_states_start != 0 && loop_states_length != 0
            && (u32)loop_states_start + loop_states_length <= size) {
        pc_swap_u16_range(data + loop_states_start, loop_states_length);
    }
    if (predictors_start != 0 && predictors_length != 0
            && (u32)predictors_start + predictors_length <= size) {
        pc_swap_u16_range(data + predictors_start, predictors_length);
    }
}

static void pc_swap_sef_table(u8 *data, u32 size, u16 offset, u32 count) {
    u32 bytes = count * 2 * sizeof(u16);

    if (offset != 0 && offset < size && bytes <= size - offset) {
        pc_swap_u16_range(data + offset, bytes);
    }
}

static void pc_swap_sef_file(void *buffer, u32 size) {
    u8 *data = buffer;
    u16 sections[8];
    u16 section2000;

    if (size < 0x22 || memcmp(data, "SEF ", 4) != 0) {
        return;
    }

    *(u32 *)(data + 0x00) = pc_bswap32(*(u32 *)(data + 0x00));
    *(u32 *)(data + 0x04) = pc_bswap32(*(u32 *)(data + 0x04));
    *(u32 *)(data + 0x08) = pc_bswap32(*(u32 *)(data + 0x08));

    for (u32 i = 0; i < 8; i++) {
        sections[i] = pc_read_be16(data + 0x10 + i * sizeof(u16));
        *(u16 *)(data + 0x10 + i * sizeof(u16)) = sections[i];
    }

    section2000 = pc_read_be16(data + 0x20);
    *(u16 *)(data + 0x20) = section2000;

    for (u32 i = 0; i < 8; i++) {
        pc_swap_sef_table(data, size, sections[i], i < 4 ? 0xC0 : 0x40);
    }
    pc_swap_sef_table(data, size, section2000, 0x140);
}

static void pc_swap_per_prg_header(void *buffer, u32 size) {
    u8 *data = buffer;

    if (size < 0x10 || (memcmp(data, "PER ", 4) != 0 && memcmp(data, "PRG ", 4) != 0)) {
        return;
    }

    *(u32 *)(data + 0x00) = pc_bswap32(*(u32 *)(data + 0x00));
    *(u32 *)(data + 0x04) = pc_bswap32(*(u32 *)(data + 0x04));
}

static void pc_swap_sbn_header(u32 rom_addr, void *buffer, u32 size) {
    u8 *data = buffer;
    u32 file_list_offset;
    u32 num_entries;
    u32 init_offset;

    if (size < 0x40 || memcmp(data, "SBN ", 4) != 0) {
        return;
    }

    file_list_offset = pc_read_be32(data + 0x10);
    num_entries = pc_read_be32(data + 0x14);
    init_offset = pc_read_be32(data + 0x24);

    sAudioFileListStart = rom_addr + file_list_offset;
    sAudioFileListEnd = sAudioFileListStart + num_entries * 8;

    for (u32 off = 0; off < 0x28; off += 4) {
        if (off == 0x08 || off == 0x0C || off == 0x20) {
            continue;
        }
        *(u32 *)(data + off) = pc_bswap32(*(u32 *)(data + off));
    }

    if (init_offset != 0) {
        sAudioInitBankListStart = sAudioInitBankListEnd = 0;
        sAudioInitSongListStart = sAudioInitSongListEnd = 0;
        sAudioInitExtraListStart = sAudioInitExtraListEnd = 0;
    }
}

static void pc_swap_init_header(u32 rom_addr, void *buffer, u32 size) {
    u8 *data = buffer;
    u16 bank_list_offset;
    u16 bank_list_size;
    u16 song_list_offset;
    u16 song_list_size;
    u16 mseq_list_offset;
    u16 mseq_list_size;

    if (size < 0x20 || memcmp(data, "INIT", 4) != 0) {
        return;
    }

    bank_list_offset = pc_read_be16(data + 0x08);
    bank_list_size = pc_read_be16(data + 0x0A);
    song_list_offset = pc_read_be16(data + 0x0C);
    song_list_size = pc_read_be16(data + 0x0E);
    mseq_list_offset = pc_read_be16(data + 0x10);
    mseq_list_size = pc_read_be16(data + 0x12);

    sAudioInitBankListStart = rom_addr + bank_list_offset;
    sAudioInitBankListEnd = sAudioInitBankListStart + bank_list_size;
    sAudioInitSongListStart = rom_addr + song_list_offset;
    sAudioInitSongListEnd = sAudioInitSongListStart + song_list_size;
    sAudioInitExtraListStart = rom_addr + mseq_list_offset;
    sAudioInitExtraListEnd = sAudioInitExtraListStart + mseq_list_size;

    *(u32 *)(data + 0x00) = pc_bswap32(*(u32 *)(data + 0x00));
    *(u32 *)(data + 0x04) = pc_bswap32(*(u32 *)(data + 0x04));
    pc_swap_u16_range(data + 0x08, 0x0C);
}

static bool pc_range_contains(u32 start, u32 end, u32 addr, u32 size) {
    return start != 0 && addr >= start && addr < end && size <= end - addr;
}

static bool pc_range_starts_in(u32 start, u32 end, u32 addr) {
    return start != 0 && addr >= start && addr < end;
}

static u32 pc_normalize_rom_addr(u32 rom_addr) {
    u32 low_addr = rom_addr & 0x0FFFFFFFu;

    if (rom_addr >= 0x10000000u && low_addr < PC_US_ROM_SIZE) {
        return low_addr;
    }
    return rom_addr;
}

// Walks a BGM file's composition stream starting at byte offset `comp_off`,
// swapping each u32 command in place until it hits BGM_COMP_END (0). For
// BGM_COMP_PLAY_PHRASE commands, records the referenced phrase-table offset
// so phrase u32s can be swapped exactly once afterward. Returns the number
// of new phrase offsets appended to `phrase_offs[]`.
static u32 pc_swap_bgm_composition(u8 *data, u32 size, u32 comp_off,
                                   u32 *phrase_offs, u32 phrase_count, u32 phrase_cap) {
    u32 off = comp_off;
    while (off + sizeof(u32) <= size) {
        u32 *cmd_ptr = (u32 *)(data + off);
        u32 cmd_be = *cmd_ptr;
        if (cmd_be == 0) {
            break;
        }
        u32 cmd_le = pc_bswap32(cmd_be);
        *cmd_ptr = cmd_le;
        off += sizeof(u32);

        u32 opcode = cmd_le >> 28;
        if (opcode == 1) {
            // BGM_COMP_PLAY_PHRASE points at comp_off + (cmd & 0xFFFF) * 4.
            u32 phrase_off = comp_off + (cmd_le & 0xFFFFu) * 4;
            bool seen = false;
            for (u32 j = 0; j < phrase_count; j++) {
                if (phrase_offs[j] == phrase_off) { seen = true; break; }
            }
            if (!seen && phrase_count < phrase_cap) {
                phrase_offs[phrase_count++] = phrase_off;
            }
        }
    }
    return phrase_count;
}

static void pc_swap_bgm_file(void *buffer, u32 size) {
    u8 *data = buffer;
    if (size < 0x24) return;
    if (data[0] != 'B' || data[1] != 'G' || data[2] != 'M' || data[3] != ' ') {
        return;
    }

    u16 compositions[4];
    u16 drums_off = pc_read_be16(data + 0x1C);
    u16 drums_count = pc_read_be16(data + 0x1E);
    u16 instr_off = pc_read_be16(data + 0x20);
    u16 instr_count = pc_read_be16(data + 0x22);
    for (u32 i = 0; i < 4; i++) {
        compositions[i] = pc_read_be16(data + 0x14 + i * 2);
    }

    *(u32 *)(data + 0x00) = pc_bswap32(*(u32 *)(data + 0x00));
    *(u32 *)(data + 0x04) = pc_bswap32(*(u32 *)(data + 0x04));
    *(u32 *)(data + 0x08) = pc_bswap32(*(u32 *)(data + 0x08));
    pc_swap_u16_range(data + 0x14, 0x10);

    // Drum info: BGMDrumInfo[drums_count], each 12 bytes with 2 u16s at the start.
    if (drums_off != 0 && drums_count != 0) {
        u32 base = (u32)drums_off * 4;
        for (u32 i = 0; i < drums_count; i++) {
            u32 off = base + i * 12;
            if (off + 4 > size) break;
            *(u16 *)(data + off + 0) = pc_bswap16(*(u16 *)(data + off + 0));
            *(u16 *)(data + off + 2) = pc_bswap16(*(u16 *)(data + off + 2));
        }
    }

    // Instrument info: BGMInstrumentInfo[instr_count], each 8 bytes with 1 u16 at the start.
    if (instr_off != 0 && instr_count != 0) {
        u32 base = (u32)instr_off * 4;
        for (u32 i = 0; i < instr_count; i++) {
            u32 off = base + i * 8;
            if (off + 2 > size) break;
            *(u16 *)(data + off) = pc_bswap16(*(u16 *)(data + off));
        }
    }

    // Compositions and phrase tables.
    u32 phrase_offs[256];
    u32 phrase_count = 0;
    for (u32 i = 0; i < 4; i++) {
        if (compositions[i] == 0) continue;
        u32 comp_off = (u32)compositions[i] * 4;
        if (comp_off >= size) continue;
        phrase_count = pc_swap_bgm_composition(data, size, comp_off,
                                               phrase_offs, phrase_count, 256);
    }

    // Phrase tables are 16 u32 track entries each.
    for (u32 i = 0; i < phrase_count; i++) {
        u32 off = phrase_offs[i];
        for (u32 t = 0; t < 16; t++) {
            u32 tracker_off = off + t * sizeof(u32);
            if (tracker_off + sizeof(u32) > size) break;
            u32 *p = (u32 *)(data + tracker_off);
            *p = pc_bswap32(*p);
        }
    }
}

// When a chunk-1 "BGM " header is seen we eagerly DMA the rest of the file into the
// same buffer, swap it as a whole, and remember the ROM range so the audio engine's follow-up
// chunked reads short-circuit instead of clobbering swapped data.
#define PC_BGM_PRELOAD_SLOTS 8
typedef struct {
    u32 rom_start;
    u32 rom_end;
    bool active;
} PcBgmPreload;

static PcBgmPreload sBgmPreloads[PC_BGM_PRELOAD_SLOTS];

static bool pc_bgm_preload_contains(u32 rom_addr, u32 size, u32 *out_skip_size) {
    for (u32 i = 0; i < PC_BGM_PRELOAD_SLOTS; i++) {
        if (!sBgmPreloads[i].active) {
            continue;
        }
        if (rom_addr >= sBgmPreloads[i].rom_start && rom_addr < sBgmPreloads[i].rom_end) {
            u32 avail = sBgmPreloads[i].rom_end - rom_addr;
            *out_skip_size = size <= avail ? size : avail;
            if (rom_addr + size >= sBgmPreloads[i].rom_end) {
                sBgmPreloads[i].active = false;
            }
            return true;
        }
    }
    return false;
}

static void pc_bgm_preload_register(u32 rom_start, u32 rom_end) {
    for (u32 i = 0; i < PC_BGM_PRELOAD_SLOTS; i++) {
        if (!sBgmPreloads[i].active) {
            sBgmPreloads[i].rom_start = rom_start;
            sBgmPreloads[i].rom_end = rom_end;
            sBgmPreloads[i].active = true;
            return;
        }
    }
    // Out of slots, so overwrite the oldest entry. We shouldn't hit this in practice.
    sBgmPreloads[0].rom_start = rom_start;
    sBgmPreloads[0].rom_end = rom_end;
    sBgmPreloads[0].active = true;
}

static void pc_try_preload_bgm(u32 rom_addr, void *buffer, u32 size) {
    u8 *data = buffer;

    if (size < 0x24) {
        return;
    }
    if (data[0] != 'B' || data[1] != 'G' || data[2] != 'M' || data[3] != ' ') {
        return;
    }

    u32 file_size = pc_read_be32(data + 0x04);
    if (file_size <= size || file_size > 0x100000u) {
        return;
    }

    u32 remaining = file_size - size;
    asset_loader_dma_read(rom_addr + size, data + size, remaining);
    pc_bgm_preload_register(rom_addr, rom_addr + file_size);
}

static void pc_swap_audio_metadata(u32 rom_addr, void *buffer, u32 size) {
    if (rom_addr < PC_US_AUDIO_ROM_START) {
        return;
    }

    pc_try_preload_bgm(rom_addr, buffer, size);

    pc_swap_sbn_header(rom_addr, buffer, size);
    pc_swap_init_header(rom_addr, buffer, size);
    pc_swap_bk_header(buffer, size);
    pc_swap_sef_file(buffer, size);
    pc_swap_per_prg_header(buffer, size);

    // pc_swap_bgm_file walks the BGM body using header offsets. After
    // pc_try_preload_bgm has filled the entire file into `buffer`, swap across
    // the full file size, not just the requested chunk, so drum/instrument/
    // composition/phrase data past the first chunk gets byte-swapped too.
    u32 bgm_swap_size = size;
    if (size >= 0x24 && ((u8 *)buffer)[0] == 'B' && ((u8 *)buffer)[1] == 'G'
            && ((u8 *)buffer)[2] == 'M' && ((u8 *)buffer)[3] == ' ') {
        u32 file_size = pc_read_be32((const u8 *)buffer + 0x04);
        if (file_size > size && file_size <= 0x100000u) {
            bgm_swap_size = file_size;
        }
    }
    pc_swap_bgm_file(buffer, bgm_swap_size);

    if (pc_range_contains(sAudioFileListStart, sAudioFileListEnd, rom_addr, size)) {
        pc_swap_sbn_file_list(buffer, size);
    } else if (pc_range_starts_in(sAudioInitBankListStart, sAudioInitBankListEnd, rom_addr)) {
        pc_swap_init_bank_list(buffer, size);
    } else if (pc_range_starts_in(sAudioInitSongListStart, sAudioInitSongListEnd, rom_addr)
            || pc_range_starts_in(sAudioInitExtraListStart, sAudioInitExtraListEnd, rom_addr)) {
        pc_swap_u16_range(buffer, size);
    }
}

static void pc_swap_sprite_metadata(u32 rom_addr, void *buffer, u32 size) {
    u32 *values = buffer;

    if (rom_addr < PC_US_SPRITE_ROM_START) {
        return;
    }

    if (rom_addr == PC_US_SPRITE_ROM_START && size == 3 * sizeof(u32)) {
        pc_swap_u32_range(buffer, size);
        sSpriteRasterHeaderStart = PC_US_SPRITE_ROM_START + values[0];
        sSpritePlayerTableStart = PC_US_SPRITE_ROM_START + values[1];
        sSpriteNpcTableStart = PC_US_SPRITE_ROM_START + values[2];
        return;
    }

    if (rom_addr == sSpriteRasterHeaderStart && size == 3 * sizeof(u32)) {
        pc_swap_u32_range(buffer, size);
        sSpriteRasterIndexRangesStart = sSpriteRasterHeaderStart + values[0];
        sSpriteRasterLoadDescStart = sSpriteRasterHeaderStart + values[1];
        sSpriteRasterIndexRangesEnd = sSpriteRasterLoadDescStart;
        sSpriteRasterLoadDescEnd = sSpriteRasterHeaderStart + values[2];
        return;
    }

    if (pc_range_contains(sSpriteRasterIndexRangesStart, sSpriteRasterIndexRangesEnd, rom_addr, size)
            || pc_range_contains(sSpriteRasterLoadDescStart, sSpriteRasterLoadDescEnd, rom_addr, size)) {
        pc_swap_u32_range(buffer, size);
        return;
    }

    if (size == 2 * sizeof(u32)
            && ((rom_addr >= sSpritePlayerTableStart && rom_addr < sSpriteNpcTableStart)
                || rom_addr >= sSpriteNpcTableStart)) {
        pc_swap_u32_range(buffer, size);
    }
}

static void pc_swap_yay0_header(void *buffer, u32 size) {
    u8 *data = buffer;

    if (size < 0x10 || memcmp(data, "Yay0", 4) != 0) {
        return;
    }

    *(u32 *)(data + 0x04) = pc_read_be32(data + 0x04);
    *(u32 *)(data + 0x08) = pc_read_be32(data + 0x08);
    *(u32 *)(data + 0x0C) = pc_read_be32(data + 0x0C);
}

void nuPiInit(void) {
}

void nuPiReadRom(u32 rom_addr, void *buf_ptr, u32 size) {
    if (buf_ptr == NULL || size == 0) {
        return;
    }

    rom_addr = pc_normalize_rom_addr(rom_addr);

    // If this read falls inside a BGM file we already fetched and swapped on
    // the first chunk, the buffer already holds the correct swapped data
    u32 skip = 0;
    if (pc_bgm_preload_contains(rom_addr, size, &skip)) {
        if (skip >= size) {
            return;
        }
        rom_addr += skip;
        buf_ptr = (u8 *)buf_ptr + skip;
        size -= skip;
    }

    asset_loader_dma_read(rom_addr, buf_ptr, size);
    pc_swap_audio_metadata(rom_addr, buf_ptr, size);
    pc_swap_sprite_metadata(rom_addr, buf_ptr, size);
    pc_swap_yay0_header(buf_ptr, size);
}

void nuPiReadRomOverlay(void *segment) {
    (void)segment;
}
