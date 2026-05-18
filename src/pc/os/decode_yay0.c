#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <PR/ultratypes.h>

#define PC_SHAPE_VRAM_BASE   0x80210000u
#define PC_SHAPE_SIZE_LIMIT  0x40000u
#define PC_GBI_ENDDL         0xDFu
#define PC_GBI_DL            0xDEu
#define PC_GBI_DL_MAX_DEPTH  32u
#define PC_BACKGROUND_VRAM_BASE 0x80200000u
#define PC_BACKGROUND_SIZE_LIMIT 0x40000u
#define PC_BACKGROUND_ROM_HEADER_SIZE 0x10u
#define PC_SHAPE_HEADER_SIZE 0x20u
#define PC_MODEL_NODE_SIZE 0x14u
#define PC_MODEL_GROUP_SIZE 0x14u
#define PC_MODEL_DISPLAY_SIZE 0x08u
#define PC_MODEL_PROPERTY_SIZE 0x0Cu
#define PC_MODEL_PROP_TEXTURE_NAME 0x5Eu

extern u8 gMapShapeData[];
extern u8 gBackgroundImage[];
extern u8 heap_generalHead[];
void texture_cache_invalidate_all(void);

u32 gPcMapShapeDataSize;
u32 gPcMapShapePayloadShift;

// Shape payloads can exceed the shared ShapeFile BSS slot. PC decodes them
// into a larger arena and mirrors the converted header back to gMapShapeData.
u8 gPcShapeArena[PC_SHAPE_SIZE_LIMIT];

typedef struct PcModelProperty {
    s32 key;
    s32 dataType;
    union {
        s32 s;
        f32 f;
        void *p;
    } data;
} PcModelProperty;

typedef struct PcModelDisplayData {
    void *displayList;
    char unk_04[4];
} PcModelDisplayData;

typedef struct PcModelGroupData {
    void *transformMatrix;
    void *lightingGroup;
    s32 numLights;
    s32 numChildren;
    void **childList;
} PcModelGroupData;

typedef struct PcModelNode {
    s32 type;
    PcModelDisplayData *displayData;
    s32 numProperties;
    PcModelProperty *propertyList;
    PcModelGroupData *groupData;
} PcModelNode;

static inline u32 read_be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static inline u16 read_be16(const u8 *p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}

static inline s16 read_be_s16(const u8 *p) {
    return (s16)read_be16(p);
}

static inline void write_native16(u8 *p, u16 value) {
    memcpy(p, &value, sizeof(value));
}

static inline void write_native32(u8 *p, u32 value) {
    memcpy(p, &value, sizeof(value));
}

static inline u32 read_native32(const u8 *p) {
    u32 value;

    memcpy(&value, p, sizeof(value));
    return value;
}

static inline int yay0_header_values_plausible(u32 out_size, u32 off_table_offset, u32 uncomp_offset) {
    const u32 max_asset_size = 64u * 1024u * 1024u;

    return out_size <= max_asset_size
        && off_table_offset >= 0x10
        && uncomp_offset >= 0x10
        && off_table_offset < max_asset_size
        && uncomp_offset < max_asset_size;
}

static inline int range_valid(u32 offset, u32 size, u32 total_size) {
    return offset < total_size && size <= total_size - offset;
}

static inline size_t align_up_size(size_t value, size_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static u32 shape_native_header_size(void) {
    return (u32)align_up_size(5u * sizeof(void *) + 0x0Cu, sizeof(void *));
}

static u32 shape_arena_size(void) {
    (void)heap_generalHead;
    return (u32)sizeof(gPcShapeArena);
}

static u32 shape_payload_offset(u32 offset) {
    if (offset >= PC_SHAPE_HEADER_SIZE) {
        return offset + gPcMapShapePayloadShift;
    }
    return offset;
}

static int shape_range_valid(u32 out_size, u32 offset, u32 size) {
    u32 adjusted_size = out_size + gPcMapShapePayloadShift;
    u32 adjusted_offset = shape_payload_offset(offset);

    return adjusted_size >= out_size
        && range_valid(offset, size, out_size)
        && range_valid(adjusted_offset, size, adjusted_size);
}

static int shape_ptr_valid(u32 value, u32 out_size) {
    return value >= PC_SHAPE_VRAM_BASE && value - PC_SHAPE_VRAM_BASE < out_size;
}

static u8 *shape_ptr_to_raw(u8 *dst, u32 out_size, u32 value) {
    u32 offset;

    if (!shape_ptr_valid(value, out_size)) {
        return NULL;
    }

    offset = value - PC_SHAPE_VRAM_BASE;
    if (!shape_range_valid(out_size, offset, 1)) {
        return NULL;
    }
    return dst + shape_payload_offset(offset);
}

static void *shape_ptr_to_host(u8 *dst, u32 out_size, u32 value) {
    u8 *raw = shape_ptr_to_raw(dst, out_size, value);

    return raw != NULL ? raw : NULL;
}

static PcModelNode *convert_shape_node(u8 *dst, u32 out_size, u32 node_vaddr);

// One bit per 8-byte display-list slot in the shape buffer, set after byte-swapping.
static u8 g_shape_dl_visited[PC_SHAPE_SIZE_LIMIT / 8u];
static u8 g_shape_mtx_visited[PC_SHAPE_SIZE_LIMIT / 32u];

static char **convert_shape_name_list(u8 *dst, u32 out_size, u32 list_vaddr) {
    u8 *list = shape_ptr_to_raw(dst, out_size, list_vaddr);
    u32 list_offset;
    u32 count = 0;
    char **converted;

    if (list == NULL) {
        return NULL;
    }

    list_offset = list_vaddr - PC_SHAPE_VRAM_BASE;
    while (shape_range_valid(out_size, list_offset + count * 4u, 4) && count < 1024) {
        u32 value = read_be32(list + count * 4u);

        if (value == 0) {
            break;
        }
        if (!shape_ptr_valid(value, out_size)) {
            break;
        }
        count++;
    }

    converted = malloc((count + 1u) * sizeof(*converted));
    if (converted == NULL) {
        return NULL;
    }

    for (u32 i = 0; i < count; i++) {
        converted[i] = shape_ptr_to_host(dst, out_size, read_be32(list + i * 4u));
    }
    converted[count] = NULL;
    return converted;
}

static void **convert_shape_child_list(u8 *dst, u32 out_size, u32 list_vaddr, u32 count) {
    u8 *list = shape_ptr_to_raw(dst, out_size, list_vaddr);
    void **converted;

    if (list == NULL || count > 4096 || !shape_range_valid(out_size, list_vaddr - PC_SHAPE_VRAM_BASE, count * 4u)) {
        return NULL;
    }

    converted = malloc(count * sizeof(*converted));
    if (converted == NULL) {
        return NULL;
    }

    for (u32 i = 0; i < count; i++) {
        converted[i] = convert_shape_node(dst, out_size, read_be32(list + i * 4u));
    }
    return converted;
}

static PcModelProperty *convert_shape_properties(u8 *dst, u32 out_size, u32 list_vaddr, u32 count) {
    u8 *list = shape_ptr_to_raw(dst, out_size, list_vaddr);
    PcModelProperty *converted;

    if (list == NULL || count > 4096
            || !shape_range_valid(out_size, list_vaddr - PC_SHAPE_VRAM_BASE,
                                  count * PC_MODEL_PROPERTY_SIZE)) {
        fprintf(stderr,
                "[shape] convert_shape_properties FAIL: vaddr=0x%08X count=%u list=%p vram_base=0x%08X out_size=0x%X\n",
                list_vaddr, count, (void*)list, PC_SHAPE_VRAM_BASE, out_size);
        return NULL;
    }

    converted = malloc(count * sizeof(*converted));
    if (converted == NULL) {
        return NULL;
    }

    for (u32 i = 0; i < count; i++) {
        u8 *src = list + i * PC_MODEL_PROPERTY_SIZE;
        u32 data = read_be32(src + 0x08);

        converted[i].key = (s32)read_be32(src + 0x00);
        converted[i].dataType = (s32)read_be32(src + 0x04);
        converted[i].data.p = NULL;
        if (data != 0 && shape_ptr_valid(data, out_size)) {
            converted[i].data.p = shape_ptr_to_host(dst, out_size, data);
        } else {
            write_native32((u8 *)&converted[i].data, data);
        }

        if (converted[i].key == PC_MODEL_PROP_TEXTURE_NAME && data != 0) {
            converted[i].data.p = shape_ptr_to_host(dst, out_size, data);
        }
    }
    return converted;
}

static void swap_shape_dl(u8 *dst, u32 out_size, u32 dl_vaddr, unsigned int depth) {
    u8 *ptr = shape_ptr_to_raw(dst, out_size, dl_vaddr);
    u8 *end = dst + out_size + gPcMapShapePayloadShift;

    if (ptr == NULL || depth > PC_GBI_DL_MAX_DEPTH) {
        return;
    }

    while (ptr + 8 <= end) {
        u32 cmd_off = (u32)(ptr - dst);
        u32 visited_idx = cmd_off / 8u;

        if (g_shape_dl_visited[visited_idx / 8u] & (1u << (visited_idx % 8u))) {
            break;
        }
        g_shape_dl_visited[visited_idx / 8u] |= (1u << (visited_idx % 8u));

        u8 opcode = ptr[0];
        u32 w1 = read_be32(ptr + 4);

        write_native32(ptr,     read_be32(ptr));
        write_native32(ptr + 4, w1);
        ptr += 8;

        if (opcode == PC_GBI_ENDDL) {
            break;
        }

        if (opcode == PC_GBI_DL) {
            swap_shape_dl(dst, out_size, w1, depth + 1);
        }
    }
}

static void swap_shape_mtx(u8 *dst, u32 out_size, u32 mtx_vaddr) {
    u32 offset;
    u32 adjusted_offset;
    u32 word_idx;
    u8 *mtx;

    if (!shape_ptr_valid(mtx_vaddr, out_size)) {
        return;
    }

    offset = mtx_vaddr - PC_SHAPE_VRAM_BASE;
    if (!shape_range_valid(out_size, offset, 0x40u)) {
        return;
    }

    adjusted_offset = shape_payload_offset(offset);
    word_idx = adjusted_offset / 4u;
    if (word_idx / 8u >= sizeof(g_shape_mtx_visited)) {
        return;
    }
    if (g_shape_mtx_visited[word_idx / 8u] & (1u << (word_idx % 8u))) {
        return;
    }
    g_shape_mtx_visited[word_idx / 8u] |= (1u << (word_idx % 8u));

    mtx = dst + adjusted_offset;
    for (u32 i = 0; i < 16; i++) {
        write_native32(mtx + i * 4u, read_be32(mtx + i * 4u));
    }
}

static PcModelDisplayData *convert_shape_display_data(u8 *dst, u32 out_size, u32 display_vaddr) {
    u8 *display = shape_ptr_to_raw(dst, out_size, display_vaddr);
    PcModelDisplayData *converted;

    if (display == NULL || !shape_range_valid(out_size, display_vaddr - PC_SHAPE_VRAM_BASE, PC_MODEL_DISPLAY_SIZE)) {
        return NULL;
    }

    converted = malloc(sizeof(*converted));
    if (converted == NULL) {
        return NULL;
    }

    u32 dl_vaddr = read_be32(display + 0x00);
    swap_shape_dl(dst, out_size, dl_vaddr, 0u);
    converted->displayList = shape_ptr_to_host(dst, out_size, dl_vaddr);
    memcpy(converted->unk_04, display + 0x04, sizeof(converted->unk_04));
    return converted;
}

static PcModelGroupData *convert_shape_group_data(u8 *dst, u32 out_size, u32 group_vaddr) {
    u8 *group = shape_ptr_to_raw(dst, out_size, group_vaddr);
    PcModelGroupData *converted;
    u32 transform_matrix;
    u32 num_children;

    if (group == NULL || !shape_range_valid(out_size, group_vaddr - PC_SHAPE_VRAM_BASE, PC_MODEL_GROUP_SIZE)) {
        return NULL;
    }

    converted = malloc(sizeof(*converted));
    if (converted == NULL) {
        return NULL;
    }

    transform_matrix = read_be32(group + 0x00);
    num_children = read_be32(group + 0x0C);
    swap_shape_mtx(dst, out_size, transform_matrix);
    converted->transformMatrix = shape_ptr_to_host(dst, out_size, transform_matrix);
    converted->lightingGroup = shape_ptr_to_host(dst, out_size, read_be32(group + 0x04));
    converted->numLights = (s32)read_be32(group + 0x08);
    converted->numChildren = (s32)num_children;
    converted->childList = convert_shape_child_list(dst, out_size, read_be32(group + 0x10), num_children);
    return converted;
}

static PcModelNode *convert_shape_node(u8 *dst, u32 out_size, u32 node_vaddr) {
    u8 *node = shape_ptr_to_raw(dst, out_size, node_vaddr);
    PcModelNode *converted;
    u32 num_properties;

    if (node == NULL || !shape_range_valid(out_size, node_vaddr - PC_SHAPE_VRAM_BASE, PC_MODEL_NODE_SIZE)) {
        return NULL;
    }

    converted = malloc(sizeof(*converted));
    if (converted == NULL) {
        return NULL;
    }

    num_properties = read_be32(node + 0x08);
    converted->type = (s32)read_be32(node + 0x00);
    converted->displayData = convert_shape_display_data(dst, out_size, read_be32(node + 0x04));
    converted->propertyList = convert_shape_properties(dst, out_size, read_be32(node + 0x0C), num_properties);
    converted->numProperties = (converted->propertyList != NULL) ? (s32)num_properties : 0;
    converted->groupData = convert_shape_group_data(dst, out_size, read_be32(node + 0x10));
    return converted;
}

static void try_convert_shape_file(u8 *dst, u32 out_size) {
    u32 root;
    u32 vertex_table;
    u32 model_names;
    u32 collider_names;
    u32 zone_names;
    u32 native_header_size;
    u32 arena_size;

    if (dst != gPcShapeArena) {
        return;
    }

    gPcMapShapeDataSize = 0;
    gPcMapShapePayloadShift = 0;

    arena_size = shape_arena_size();
    if (out_size < PC_SHAPE_HEADER_SIZE || out_size > arena_size) {
        return;
    }

    memset(g_shape_dl_visited, 0, sizeof(g_shape_dl_visited));
    memset(g_shape_mtx_visited, 0, sizeof(g_shape_mtx_visited));

    root = read_be32(dst + 0x00);
    vertex_table = read_be32(dst + 0x04);
    model_names = read_be32(dst + 0x08);
    collider_names = read_be32(dst + 0x0C);
    zone_names = read_be32(dst + 0x10);
    if (!shape_ptr_valid(root, out_size)) {
        return;
    }

    native_header_size = shape_native_header_size();
    if (native_header_size < PC_SHAPE_HEADER_SIZE || native_header_size > arena_size
            || out_size > arena_size - (native_header_size - PC_SHAPE_HEADER_SIZE)) {
        return;
    }

    gPcMapShapePayloadShift = native_header_size - PC_SHAPE_HEADER_SIZE;
    if (gPcMapShapePayloadShift != 0) {
        memmove(dst + native_header_size, dst + PC_SHAPE_HEADER_SIZE, out_size - PC_SHAPE_HEADER_SIZE);
    }
    gPcMapShapeDataSize = out_size + gPcMapShapePayloadShift;

    memset(dst, 0, native_header_size);
    ((void **)dst)[0] = convert_shape_node(dst, out_size, root);
    ((void **)dst)[1] = shape_ptr_to_host(dst, out_size, vertex_table);
    ((void **)dst)[2] = convert_shape_name_list(dst, out_size, model_names);
    ((void **)dst)[3] = convert_shape_name_list(dst, out_size, collider_names);
    ((void **)dst)[4] = convert_shape_name_list(dst, out_size, zone_names);
}

static int background_ptr_valid(u32 value, u32 out_size) {
    return value >= PC_BACKGROUND_VRAM_BASE && value < PC_BACKGROUND_VRAM_BASE + out_size;
}

static void try_convert_background_file(u8 *dst, u32 out_size) {
    u32 raster;
    u32 palette;
    u16 start_x;
    u16 start_y;
    u16 width;
    u16 height;
    size_t native_header_size = 2u * sizeof(void *) + 4u * sizeof(u16);

    if (dst != gBackgroundImage || out_size < PC_BACKGROUND_ROM_HEADER_SIZE || out_size > PC_BACKGROUND_SIZE_LIMIT) {
        return;
    }

    raster = read_be32(dst + 0x00);
    palette = read_be32(dst + 0x04);
    start_x = read_be16(dst + 0x08);
    start_y = read_be16(dst + 0x0A);
    width = read_be16(dst + 0x0C);
    height = read_be16(dst + 0x0E);

    if (!background_ptr_valid(raster, out_size) || !background_ptr_valid(palette, out_size)
        || width == 0 || height == 0 || width > 2048 || height > 2048) {
        return;
    }

    if (native_header_size > PC_BACKGROUND_ROM_HEADER_SIZE) {
        memmove(dst + native_header_size, dst + PC_BACKGROUND_ROM_HEADER_SIZE,
                out_size - PC_BACKGROUND_ROM_HEADER_SIZE);
    }

    ((void **)dst)[0] = dst + native_header_size + (raster - PC_BACKGROUND_VRAM_BASE - PC_BACKGROUND_ROM_HEADER_SIZE);
    ((void **)dst)[1] = dst + native_header_size + (palette - PC_BACKGROUND_VRAM_BASE - PC_BACKGROUND_ROM_HEADER_SIZE);
    write_native16(dst + 2u * sizeof(void *) + 0, start_x);
    write_native16(dst + 2u * sizeof(void *) + 2, start_y);
    write_native16(dst + 2u * sizeof(void *) + 4, width);
    write_native16(dst + 2u * sizeof(void *) + 6, height);
}

static int hit_header_plausible(const u8 *dst, u32 total_size, u32 offset) {
    const u8 *header;
    u32 colliders_offset;
    u32 vertices_offset;
    u32 bounding_boxes_offset;
    u16 num_colliders;
    u16 num_vertices;
    u16 bounding_boxes_words;

    if (!range_valid(offset, 0x18, total_size)) {
        return 0;
    }

    header = dst + offset;
    num_colliders = read_be16(header + 0x00);
    colliders_offset = read_be32(header + 0x04);
    num_vertices = read_be16(header + 0x08);
    vertices_offset = read_be32(header + 0x0C);
    bounding_boxes_words = read_be16(header + 0x10);
    bounding_boxes_offset = read_be32(header + 0x14);

    return num_colliders < 4096
        && num_vertices < 32768
        && bounding_boxes_words < 32768
        && range_valid(colliders_offset, (u32)num_colliders * 0x0Cu, total_size)
        && range_valid(vertices_offset, (u32)num_vertices * 0x06u, total_size)
        && range_valid(bounding_boxes_offset, (u32)bounding_boxes_words * 0x04u, total_size);
}

static void swap_s16_range(u8 *data, u32 count) {
    for (u32 i = 0; i < count; i++) {
        write_native16(data + i * 2, read_be16(data + i * 2));
    }
}

static void swap_u32_range(u8 *data, u32 count) {
    for (u32 i = 0; i < count; i++) {
        write_native32(data + i * 4, read_be32(data + i * 4));
    }
}

static void swap_hit_section(u8 *dst, u32 total_size, u32 offset) {
    u8 *header = dst + offset;
    u16 num_colliders = read_be16(header + 0x00);
    u32 colliders_offset = read_be32(header + 0x04);
    u16 num_vertices = read_be16(header + 0x08);
    u32 vertices_offset = read_be32(header + 0x0C);
    u16 bounding_boxes_words = read_be16(header + 0x10);
    u32 bounding_boxes_offset = read_be32(header + 0x14);

    (void)total_size;

    write_native16(header + 0x00, num_colliders);
    write_native32(header + 0x04, colliders_offset);
    write_native16(header + 0x08, num_vertices);
    write_native32(header + 0x0C, vertices_offset);
    write_native16(header + 0x10, bounding_boxes_words);
    write_native32(header + 0x14, bounding_boxes_offset);

    swap_s16_range(dst + vertices_offset, (u32)num_vertices * 3u);
    swap_u32_range(dst + bounding_boxes_offset, bounding_boxes_words);

    for (u32 i = 0; i < num_colliders; i++) {
        u8 *collider = dst + colliders_offset + i * 0x0C;
        s16 num_triangles = read_be_s16(collider + 0x06);
        u32 triangles_offset = read_be32(collider + 0x08);

        write_native16(collider + 0x00, read_be16(collider + 0x00));
        write_native16(collider + 0x02, read_be16(collider + 0x02));
        write_native16(collider + 0x04, read_be16(collider + 0x04));
        write_native16(collider + 0x06, (u16)num_triangles);
        write_native32(collider + 0x08, triangles_offset);

        if (num_triangles > 0 && range_valid(triangles_offset, (u32)num_triangles * 4u, total_size)) {
            swap_u32_range(dst + triangles_offset, (u32)num_triangles);
        }
    }
}

static void try_swap_hit_file(u8 *dst, u32 out_size) {
    u32 collision_offset;
    u32 zone_offset;

    if (out_size < 8) {
        return;
    }

    collision_offset = read_be32(dst + 0x00);
    zone_offset = read_be32(dst + 0x04);

    if (collision_offset == 0 || !hit_header_plausible(dst, out_size, collision_offset)) {
        return;
    }
    if (zone_offset != 0 && !hit_header_plausible(dst, out_size, zone_offset)) {
        return;
    }

    write_native32(dst + 0x00, collision_offset);
    write_native32(dst + 0x04, zone_offset);
    swap_hit_section(dst, out_size, collision_offset);
    if (zone_offset != 0) {
        swap_hit_section(dst, out_size, zone_offset);
    }
}

static void try_swap_title_data_file(u8 *dst, u32 out_size) {
    u32 logo;
    u32 copyright;
    u32 press_start;
    u32 copyright_palette;
    u32 press_start_end;

    if (out_size < 0x10) {
        return;
    }

    logo = read_be32(dst + 0x00);
    copyright = read_be32(dst + 0x04);
    press_start = read_be32(dst + 0x08);
    copyright_palette = read_be32(dst + 0x0C);

    if (logo < 0x10 || logo >= out_size
        || copyright < 0x10 || copyright >= out_size
        || press_start < 0x10 || press_start >= out_size
        || logo == copyright || logo == press_start || copyright == press_start) {
        return;
    }

    if (copyright_palette != 0
        && (copyright_palette < 0x10 || copyright_palette >= out_size
            || copyright_palette == logo || copyright_palette == copyright
            || copyright_palette == press_start)) {
        return;
    }

    write_native32(dst + 0x00, logo);
    write_native32(dst + 0x04, copyright);
    write_native32(dst + 0x08, press_start);
    write_native32(dst + 0x0C, copyright_palette);

    press_start_end = out_size;
    if (logo > press_start && logo < press_start_end) {
        press_start_end = logo;
    }
    if (copyright > press_start && copyright < press_start_end) {
        press_start_end = copyright;
    }
    if (copyright_palette > press_start && copyright_palette < press_start_end) {
        press_start_end = copyright_palette;
    }

    // The title "press start" IA8 mask is primitive-tinted. Some source pixels
    // carry the visible shape in alpha while their intensity nibble is zero,
    // which becomes an opaque dark mask on PC. Promote alpha into intensity for
    // this title-only texture so the primitive colour can tint the full glyph.
    for (u32 i = press_start; i < press_start_end; i++) {
        u8 intensity = dst[i] >> 4;
        u8 alpha = dst[i] & 0x0F;

        if (alpha > intensity) {
            dst[i] = (u8)((alpha << 4) | alpha);
        }
    }
}

// YAY0 header layout (all big-endian)
//   +0x00  magic / flags  (ignored)
//   +0x04  output size (uncompressed byte count)
//   +0x08  byte offset from header start to the offset table
//   +0x0C  byte offset from header start to the uncompressed data stream
//   +0x10  control-bit words (one u32 per 32 literals/back-refs)
//
// Control bit = 1 → next byte from the uncompressed stream is a literal.
// Control bit = 0 → next u16 from the offset table is a back-reference:
//     bits [15:12]  run length field N  (count = N == 0 ? extra_byte + 18 : N + 2)
//     bits [11:0]   back-reference distance D  (copy_src = current_dst - D - 1)

void decode_yay0(void *src_ptr, void *dst_ptr) {
    const u8 *src        = (const u8 *)src_ptr;

    u32       out_size   = read_be32(src + 0x04);
    u32       off_offset = read_be32(src + 0x08);
    u32       uncomp_offset = read_be32(src + 0x0C);
    const u8 *ctrl_ptr   = src + 0x10;

    if (!yay0_header_values_plausible(out_size, off_offset, uncomp_offset)) {
        out_size = read_native32(src + 0x04);
        off_offset = read_native32(src + 0x08);
        uncomp_offset = read_native32(src + 0x0C);
    }

    // PC needs a larger arena for Shape files target gMapShapeData in shared code
    void *original_dst_ptr = dst_ptr;
    if (dst_ptr == gMapShapeData) {
        dst_ptr = gPcShapeArena;
    }
    u8 *dst = (u8 *)dst_ptr;

    const u8 *off_table  = src + off_offset;
    const u8 *uncomp     = src + uncomp_offset;
    const u8 *dst_start  = dst;
    const u8 *dst_end    = dst + out_size;
    u32       ctrl       = 0;
    int       bits_left  = 0;

    while (dst < dst_end) {
        if (bits_left == 0) {
            ctrl      = read_be32(ctrl_ptr);
            ctrl_ptr += 4;
            bits_left = 32;
        }

        if (ctrl & 0x80000000u) {
            *dst++ = *uncomp++;
        } else {
            u16 pair   = read_be16(off_table);
            off_table += 2;

            int  dist  = pair & 0x0FFF;
            int  count = pair >> 12;

            if (count == 0) {
                count = (int)*uncomp++ + 18;
            } else {
                count += 2;
            }

            if (count > dst_end - dst) {
                count = (int)(dst_end - dst);
            }

            const u8 *copy_src = dst - dist - 1;
            if (copy_src < dst_start) {
                break;
            }
            while (count-- > 0) {
                *dst++ = *copy_src++;
            }
        }

        ctrl <<= 1;
        bits_left--;
    }

    try_convert_background_file((u8 *)dst_ptr, out_size);
    try_convert_shape_file((u8 *)dst_ptr, out_size);
    try_swap_hit_file((u8 *)dst_ptr, out_size);
    try_swap_title_data_file((u8 *)dst_ptr, out_size);

    // Keep gMapShapeData.header pointing at the converted arena data.
    if (dst_ptr == gPcShapeArena && original_dst_ptr == gMapShapeData && gPcMapShapeDataSize != 0) {
        memcpy(original_dst_ptr, dst_ptr, shape_native_header_size());
    }

    texture_cache_invalidate_all();
}
