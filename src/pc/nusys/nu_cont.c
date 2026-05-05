#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_cont.h>
#include <PR/os_message.h>
#include "pc_cont.h"

// Globals consumed by nuContDataGet and the game's input code.
OSContPad    nuContData[MAXCONTROLLERS];
OSContStatus nuContStatus[MAXCONTROLLERS];
u32          nuContNum         = 0;
u32          nuContDataLockKey = 0;
void        *nuContReadFunc    = NULL;
void        *nuContPfs[MAXCONTROLLERS];

// Detect connected controllers via osContInit
u8 nuSiMgrInit(void) {
    OSMesgQueue mq;
    OSMesg      buf;
    u8          pattern = 0;

    osCreateMesgQueue(&mq, &buf, 1);
    osContInit(&mq, &pattern, nuContStatus);
    osRecvMesg(&mq, NULL, OS_MESG_BLOCK);

    nuContNum = 0;
    for (int i = 0; i < MAXCONTROLLERS; i++) {
        if ((pattern >> i) & 1)
            nuContNum++;
    }
    return pattern;
}

u8 nuContMgrInit(void) {
    // nuContDataGet polls directly.
    return (u8)(nuContNum > 0 ? 1 : 0);
}

void nuContPakMgrInit(void) {}
void nuContRmbMgrInit(void) {}

// Public entry point called from the game's boot sequence.
u8 nuContInit(void) {
    u8 pattern = nuSiMgrInit();
    nuContMgrInit();
    nuContPakMgrInit();
    nuContRmbMgrInit();
    return pattern;
}

void nuContDataGet(OSContPad *contdata, u32 padno) {
    pc_cont_poll(nuContData, MAXCONTROLLERS);
    *contdata = nuContData[padno];
}

void nuContDataClose(void) {}
void nuContDataOpen(void)  {}
void nuContQueryRead(void) {}

s32  nuSiSendMesg(short mesg, void *dataPtr)  { (void)mesg; (void)dataPtr; return 0; }
void nuSiSendMesgNW(short mesg, void *dataPtr){ (void)mesg; (void)dataPtr; }
void nuSiCallBackAdd(void *list)              { (void)list; }
void nuSiCallBackRemove(void *list)           { (void)list; }

// Extern globals declared in include/nu/nusys.h and referenced by some game code.
OSMesgQueue  nuSiMesgQ;
OSMesgQueue  nuSiMgrMesgQ;
void        *nuSiCallBackList = NULL;
