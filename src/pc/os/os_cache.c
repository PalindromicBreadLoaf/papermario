#include <PR/os_cache.h>

void osWritebackDCache(void* addr, s32 nbytes) {
    (void)addr;
    (void)nbytes;
}

void osWritebackDCacheAll(void) {}

void osInvalDCache(void* addr, s32 nbytes) {
    (void)addr;
    (void)nbytes;
}

void osInvalICache(void* addr, s32 nbytes) {
    (void)addr;
    (void)nbytes;
}
