#include <PR/os_tlb.h>
#include <PR/osint.h>

// TLB management is N64-specific hardware.  All routines are no-ops.

void osMapTLB(s32 tlbIndex, OSPageMask pageSize, void *vaddr, u32 even, u32 odd, s32 asid) {
    (void)tlbIndex; (void)pageSize; (void)vaddr;
    (void)even; (void)odd; (void)asid;
}

void osMapTLBRdb(void) {}

void osUnmapTLB(s32 tlbIndex) { (void)tlbIndex; }

void osUnmapTLBAll(void) {}

void osSetTLBASID(s32 asid) { (void)asid; }

// Return (u32)-1 to signal "not found in TLB", which callers treat as a miss.
u32 __osProbeTLB(void *vaddr) { (void)vaddr; return (u32)-1; }
