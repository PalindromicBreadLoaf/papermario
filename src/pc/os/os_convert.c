#include <stdint.h>
#include <stdio.h>
#include <PR/os_convert.h>

void *pc_resolve_physical_addr(uintptr_t addr);

static int pc_ptr_range_readable(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;
    uintptr_t covered = start;
    char line[256];
    FILE *maps;

    if (ptr == NULL || size == 0 || end < start) {
        return 0;
    }

    maps = fopen("/proc/self/maps", "r");
    if (maps == NULL) {
        return 1;
    }

    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long long map_start;
        unsigned long long map_end;
        char perms[5];

        if (sscanf(line, "%llx-%llx %4s", &map_start, &map_end, perms) != 3) {
            continue;
        }

        if (perms[0] != 'r' || map_end <= covered || map_start > covered) {
            continue;
        }

        covered = (uintptr_t)map_end;
        if (covered >= end) {
            fclose(maps);
            return 1;
        }
    }

    fclose(maps);
    return 0;
}

static void *pc_resolve_low32_addr(u32 low_addr) {
    char line[256];
    FILE *maps = fopen("/proc/self/maps", "r");

    if (maps == NULL) {
        return NULL;
    }

    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long long map_start;
        unsigned long long map_end;
        char perms[5];

        if (sscanf(line, "%llx-%llx %4s", &map_start, &map_end, perms) != 3 || perms[0] != 'r') {
            continue;
        }

        u32 start32 = (u32)map_start;
        u32 end32 = (u32)map_end;

        if ((start32 <= end32 && low_addr >= start32 && low_addr < end32)
                || (start32 > end32 && (low_addr >= start32 || low_addr < end32))) {
            fclose(maps);
            return (void *)((uintptr_t)map_start + (u32)(low_addr - start32));
        }
    }

    fclose(maps);
    return NULL;
}

u32 osVirtualToPhysical(void* addr) {
    return (u32)(uintptr_t)addr;
}

void* osPhysicalToVirtual(u32 phys) {
    return pc_resolve_physical_addr((uintptr_t)phys);
}

void *pc_resolve_physical_addr(uintptr_t addr) {
    void *direct = (void *)addr;

    if (addr == 0 || pc_ptr_range_readable(direct, 1)) {
        return direct;
    }

#if UINTPTR_MAX > 0xffffffffu
    void *resolved = pc_resolve_low32_addr((u32)addr);

    if (resolved != NULL) {
        return resolved;
    }

    resolved = pc_resolve_low32_addr((u32)(addr + 0x80000000u));
    if (resolved != NULL) {
        return resolved;
    }
#endif

    return direct;
}
