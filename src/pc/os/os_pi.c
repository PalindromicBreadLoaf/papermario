#include <PR/os_thread.h>
#include <PR/os_pi.h>
#include <PR/os_message.h>
#include "asset_loader.h"

void osCreatePiManager(OSPri pri, OSMesgQueue *mq, OSMesg *msg, s32 numMsgs) {
    (void)pri; (void)mq; (void)msg; (void)numMsgs;
}

OSPiHandle *osCartRomInit(void) { return NULL; }

s32 osPiStartDma(OSIoMesg *mb, s32 priority, s32 direction,
                 u32 devAddr, void *vAddr, u32 nbytes, OSMesgQueue *mq) {
    (void)mb; (void)priority;
    if (direction == OS_READ) {
        asset_loader_dma_read(devAddr, vAddr, nbytes);
    }
    osSendMesg(mq, NULL, OS_MESG_BLOCK);
    return 0;
}

s32 osEPiStartDma(OSPiHandle *handle, OSIoMesg *mb, s32 direction) {
    (void)handle;
    if (direction == OS_READ) {
        asset_loader_dma_read(mb->devAddr, mb->dramAddr, mb->size);
    }
    osSendMesg(mb->hdr.retQueue, NULL, OS_MESG_BLOCK);
    return 0;
}

s32 osEPiWriteIo(OSPiHandle *handle, u32 devAddr, u32 data) {
    (void)handle; (void)devAddr; (void)data;
    return 0;
}

s32 osEPiReadIo(OSPiHandle *handle, u32 devAddr, u32 *data) {
    (void)handle; (void)devAddr;
    if (data) *data = 0;
    return 0;
}
