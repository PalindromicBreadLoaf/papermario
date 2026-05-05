#include "texture_cache.h"

void texture_cache_init(void) {
}

unsigned int texture_cache_get(const u8 *addr, u8 fmt, u8 siz,
                                u32 size_bytes, const u8 *tlut,
                                u8 cms, u8 cmt) {
    (void)addr; (void)fmt; (void)siz; (void)size_bytes;
    (void)tlut; (void)cms; (void)cmt;
    return 0;
}

void texture_cache_flush(void) {
}
