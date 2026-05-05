#include <PR/os_thread.h>
#include <PR/os_pi.h>

OSPiHandle *nuPiCartHandle = NULL;

void nuPiInit(void) {
    // On PC, ROM DMA is replaced by file I/O in Phase 3.
}
