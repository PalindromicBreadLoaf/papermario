#include <stdint.h>
#include <PR/ultratypes.h>

static inline u32 read_be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static inline u16 read_be16(const u8 *p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
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
    u8       *dst        = (u8 *)dst_ptr;

    u32       out_size   = read_be32(src + 0x04);
    const u8 *off_table  = src + read_be32(src + 0x08);
    const u8 *uncomp     = src + read_be32(src + 0x0C);
    const u8 *ctrl_ptr   = src + 0x10;

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

            const u8 *copy_src = dst - dist - 1;
            while (count-- > 0) {
                *dst++ = *copy_src++;
            }
        }

        ctrl <<= 1;
        bits_left--;
    }
}
