#ifndef PC_ADDRESS_H
#define PC_ADDRESS_H

#include <stdbool.h>
#include <stdint.h>
#include <PR/ultratypes.h>

static inline uintptr_t pc_addr_sign_extend_u32(u32 addr) {
    return (uintptr_t)(intptr_t)(s32)addr;
}

static inline uintptr_t pc_normalize_guest_addr(uintptr_t addr) {
    u32 low = (u32)addr;

    if (addr == (uintptr_t)low || addr == pc_addr_sign_extend_u32(low)) {
        return (uintptr_t)low;
    }
    return addr;
}

static inline bool pc_addr_is_n64_kseg(void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    u32 low = (u32)addr;

    return low >= 0x80000000u && low < 0xC0000000u
        && (addr == (uintptr_t)low || addr == pc_addr_sign_extend_u32(low));
}

#endif
