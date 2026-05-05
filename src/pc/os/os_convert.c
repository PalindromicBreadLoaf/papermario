#include <stdint.h>
#include <PR/os_convert.h>

u32 osVirtualToPhysical(void* addr) {
    return (u32)(uintptr_t)addr;
}

void* osPhysicalToVirtual(u32 phys) {
    return (void*)(uintptr_t)phys;
}
