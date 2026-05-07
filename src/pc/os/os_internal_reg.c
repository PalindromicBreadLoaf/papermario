#include <PR/os_internal_reg.h>

u32  __osGetCause(void)      { return 0; }
void __osSetCause(u32 x)     { (void)x; }
u32  __osGetCompare(void)    { return 0; }
void __osSetCompare(u32 x)   { (void)x; }
u32  __osGetConfig(void)     { return 0; }
void __osSetConfig(u32 x)    { (void)x; }
void __osSetCount(u32 x)     { (void)x; }
u32  __osGetSR(void)         { return 0; }
void __osSetSR(u32 x)        { (void)x; }
u32  __osGetWatchLo(void)    { return 0; }
void __osSetWatchLo(u32 x)   { (void)x; }

u32  __osDisableInt(void)    { return 1; }
void __osRestoreInt(u32 x)   { (void)x; }

u32 __osSetFpcCsr(u32 x) { (void)x; return 0; }
u32 __osGetFpcCsr(void)  { return 0; }
