#include <stdlib.h>
#include <PR/os_thread.h>
#include <PR/os_message.h>
#include <PR/os_exception.h>
#include <PR/os_system.h>
#include <PR/os_host.h>

s32  osTvType    = OS_TV_NTSC;
s32  osRomType   = 0;
void* osRomBase  = NULL;
s32  osResetType = 0;
s32  osCicId     = 0;
s32  osVersion   = 0;
u32  osMemSize   = 8 * 1024 * 1024;
s32  osAppNMIBuffer[OS_APP_NMI_BUFSIZE / sizeof(s32)];
u64  osClockRate = 62500000LL;

OSIntMask __OSGlobalIntMask = OS_IM_ALL;

OSIntMask osSetIntMask(OSIntMask mask) {
    OSIntMask prev = __OSGlobalIntMask;
    __OSGlobalIntMask = mask;
    return prev;
}

OSIntMask osGetIntMask(void) {
    return __OSGlobalIntMask;
}

void __osInitialize_common(void) {}
void __osInitialize_autodetect(void) {}
void __osInitialize_msp(void) {}
void __osInitialize_kmc(void) {}
void __osInitialize_isv(void) {}
void __osInitialize_emu(void) {}

void osExit(void) { exit(0); }
u32  osGetMemSize(void) { return osMemSize; }
s32  osAfterPreNMI(void) { return 0; }
