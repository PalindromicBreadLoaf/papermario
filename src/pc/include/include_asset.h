#ifndef _H_INCLUDE_ASSET

#define ASTRINGIFY_(x) #x
#define ASTRINGIFY(x) ASTRINGIFY_(x)

// Use .p2align 3 (= 8 bytes) because x86 GAS interprets .align n as n bytes, not 2^n bytes.
#define _INCLUDE_IMG(FILENAME, SYMBOLNAME) \
    extern unsigned char SYMBOLNAME[]; \
    __asm__( \
        ".globl " #SYMBOLNAME"\n" \
        ".pushsection .data\n" \
        ".p2align 3\n" \
        ".type " #SYMBOLNAME", @object\n" \
        #SYMBOLNAME":\n" \
        ".incbin \"" PC_ASSET_DIR "/" FILENAME ".bin\"\n" \
        ".popsection\n" \
    )

#define INCLUDE_IMG(FILENAME, SYMBOLNAME) \
    _INCLUDE_IMG(FILENAME, SYMBOLNAME)

#define INCLUDE_PAL(FILENAME, SYMBOLNAME) \
    extern unsigned short SYMBOLNAME[]; \
    __asm__( \
        ".globl " #SYMBOLNAME"\n" \
        ".pushsection .data\n" \
        ".p2align 3\n" \
        ".type " #SYMBOLNAME", @object\n" \
        #SYMBOLNAME":\n" \
        ".incbin \"" PC_ASSET_DIR "/" FILENAME ".bin\"\n" \
        ".popsection\n" \
    )

#define INCLUDE_RAW(FILENAME, SYMBOLNAME) \
    extern unsigned char SYMBOLNAME[]; \
    __asm__( \
        ".globl " #SYMBOLNAME"\n" \
        ".pushsection .data\n" \
        ".p2align 3\n" \
        ".type " #SYMBOLNAME", @object\n" \
        #SYMBOLNAME":\n" \
        ".incbin \"" PC_RAW_ASSET_DIR "/" FILENAME "\"\n" \
        ".popsection\n" \
    )

#endif // _H_INCLUDE_ASSET
