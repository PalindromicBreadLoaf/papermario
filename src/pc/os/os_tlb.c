#include <PR/os_tlb.h>
#include <PR/osint.h>

#include <stdbool.h>
#include <stdint.h>

extern u8 gEffectGlobals[];
extern u8 gEffectDataBuffer[];

#define PC_TLB_ENTRY_COUNT 32
#define PC_TLB_PAGE_SIZE 0x1000u
#define PC_EFFECT_DATA_SIZE (15u * PC_TLB_PAGE_SIZE)

typedef struct PcTlbEntry {
    uintptr_t vaddr;
    uintptr_t host;
    u32 size;
    bool valid;
} PcTlbEntry;

static PcTlbEntry sPcTlbEntries[PC_TLB_ENTRY_COUNT];

static uintptr_t pc_recover_tlb_host(u32 phys) {
    uintptr_t globals = (uintptr_t)gEffectGlobals;
    uintptr_t effectData = (uintptr_t)gEffectDataBuffer;
    uintptr_t physLow = phys & 0xFFFFFFu;

    if (physLow == (globals & 0xFFFFFFu)) {
        return globals;
    }

    if (physLow >= (effectData & 0xFFFFFFu) && physLow < ((effectData + PC_EFFECT_DATA_SIZE) & 0xFFFFFFu)) {
        return (effectData & ~(uintptr_t)0xFFFFFFu) | physLow;
    }

    return phys;
}

void* pc_tlb_translate(void* vaddr) {
    uintptr_t addr = (uintptr_t)vaddr;
    s32 i;

    for (i = 0; i < PC_TLB_ENTRY_COUNT; i++) {
        PcTlbEntry* entry = &sPcTlbEntries[i];

        if (entry->valid && addr >= entry->vaddr && addr < entry->vaddr + entry->size) {
            return (void*)(entry->host + (addr - entry->vaddr));
        }
    }
    return vaddr;
}

void osMapTLB(s32 tlbIndex, OSPageMask pageSize, void *vaddr, u32 even, u32 odd, s32 asid) {
    PcTlbEntry* entry;

    (void)pageSize;
    (void)odd;
    (void)asid;

    if (tlbIndex < 0 || tlbIndex >= PC_TLB_ENTRY_COUNT) {
        return;
    }

    entry = &sPcTlbEntries[tlbIndex];
    entry->vaddr = (uintptr_t)vaddr;
    entry->host = pc_recover_tlb_host(even);
    entry->size = PC_TLB_PAGE_SIZE;
    entry->valid = true;
}

void osMapTLBRdb(void) {}

void osUnmapTLB(s32 tlbIndex) {
    if (tlbIndex >= 0 && tlbIndex < PC_TLB_ENTRY_COUNT) {
        sPcTlbEntries[tlbIndex].valid = false;
    }
}

void osUnmapTLBAll(void) {
    s32 i;

    for (i = 0; i < PC_TLB_ENTRY_COUNT; i++) {
        sPcTlbEntries[i].valid = false;
    }
}

void osSetTLBASID(s32 asid) { (void)asid; }

u32 __osProbeTLB(void *vaddr) {
    void* translated = pc_tlb_translate(vaddr);

    if (translated == vaddr) {
        return (u32)-1;
    }
    return (u32)(uintptr_t)translated;
}
