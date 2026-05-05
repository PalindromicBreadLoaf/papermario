#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_vi.h>
#include <nu/nusys.h>

u16           **nuGfxCfb           = NULL;
u16            *nuGfxCfb_ptr       = NULL;
u32             nuGfxCfbNum        = NU_GFX_FRAMEBUFFER_NUM;
u16            *nuGfxZBuffer       = NULL;
u32             nuGfxDisplay       = NU_GFX_DISPLAY_OFF;
u32             nuGfxCfbCounter    = 0;
volatile u32    nuGfxTaskSpool     = 0;
NUScTask        nuGfxTask[NU_GFX_TASK_NUM];
NUUcode        *nuGfxUcode         = NULL;
NUGfxSwapCfbFunc nuGfxSwapCfbFunc  = NULL;
NUGfxFunc       nuGfxFunc          = NULL;
NUGfxPreNMIFunc nuGfxPreNMIFunc    = NULL;
OSThread        nuGfxThread;
u8              nuYieldBuf[NU_GFX_YIELD_BUF_SIZE];

// nuGfxMesgQ is used by the gfx thread; nuGfxTaskEndFunc is internal.
OSMesgQueue     nuGfxMesgQ;
static OSMesg   sGfxMesgBuf[NU_GFX_MESGS];

static void gfx_thread(void *arg) {
    NUScClient gfxClient;
    NUScMsg   *mesgType;

    (void)arg;
    osCreateMesgQueue(&nuGfxMesgQ, sGfxMesgBuf, NU_GFX_MESGS);
    nuScAddClient(&gfxClient, &nuGfxMesgQ, NU_SC_RETRACE_MSG | NU_SC_PRENMI_MSG);

    while (1) {
        osRecvMesg(&nuGfxMesgQ, (OSMesg *)&mesgType, OS_MESG_BLOCK);
        switch (*mesgType) {
            case NU_SC_RETRACE_MSG:
                if (nuGfxFunc) nuGfxFunc(nuGfxTaskSpool);
                break;
            case NU_SC_PRENMI_MSG:
                if (nuGfxPreNMIFunc) nuGfxPreNMIFunc();
                break;
        }
    }
}

void nuGfxThreadStart(void) {
    osCreateThread(&nuGfxThread, NU_GFX_THREAD_ID, gfx_thread, NULL, NULL, NU_GFX_THREAD_PRI);
    osStartThread(&nuGfxThread);
}

void nuGfxTaskMgrInit(void) {
    nuGfxTaskSpool = 0;
}

void nuGfxTaskStart(Gfx *gfxList, u32 gfxListSize, u32 ucode, u32 flag) {
    (void)gfxList; (void)gfxListSize; (void)ucode;
    nuGfxTaskSpool++;

    if (flag & NU_SC_SWAPBUFFER) {
        if (nuGfxSwapCfbFunc && nuGfxCfbNum > 0) {
            static NUScTask sTmpTask;
            sTmpTask.framebuffer = nuGfxCfb_ptr;
            nuGfxSwapCfbFunc(&sTmpTask);
        }
        if (nuGfxCfbNum > 0) {
            nuGfxCfbCounter = (nuGfxCfbCounter + 1) % nuGfxCfbNum;
            nuGfxCfb_ptr    = nuGfxCfb[nuGfxCfbCounter];
        }
    }

    nuGfxTaskSpool--;
}

void nuGfxTaskAllEndWait(void) {
    while (nuGfxTaskSpool) {
        SDL_Delay(1);
    }
}

void nuGfxSetCfb(u16 **framebuf, u32 framebufnum) {
    nuGfxCfb        = framebuf;
    nuGfxCfbNum     = framebufnum;
    nuGfxCfbCounter = 0;
    nuGfxCfb_ptr    = framebuf[0];
    nuScSetFrameBufferNum((u8)framebufnum);
    nuGfxRetraceWait(1);
}

void nuGfxSwapCfb(void *task) {
    osViSwapBuffer(((NUScTask *)task)->framebuffer);
}

void nuGfxFuncSet(NUGfxFunc func) {
    nuGfxTaskAllEndWait();
    nuGfxFunc = func;
}

void nuGfxPreNMIFuncSet(NUGfxPreNMIFunc func) {
    nuGfxPreNMIFunc = func;
}

void nuGfxSwapCfbFuncSet(NUGfxSwapCfbFunc func) {
    nuGfxSwapCfbFunc = func;
}

void nuGfxDisplayOn(void) {
    nuGfxDisplay = NU_GFX_DISPLAY_ON_TRIGGER;
    osViBlack(FALSE);
}

void nuGfxDisplayOff(void) {
    nuGfxDisplay = NU_GFX_DISPLAY_OFF;
    osViBlack(TRUE);
}

void nuGfxRetraceWait(u32 retrace_num) {
    NUScClient client;
    OSMesg     mesgBuf;
    OSMesgQueue mesgQ;

    osCreateMesgQueue(&mesgQ, &mesgBuf, 1);
    nuScAddClient(&client, &mesgQ, NU_SC_RETRACE_MSG);
    while (retrace_num-- > 0) {
        osRecvMesg(&mesgQ, NULL, OS_MESG_BLOCK);
    }
    nuScRemoveClient(&client);
}

void nuGfxInitEX2(void) {
    nuGfxSwapCfbFuncSet(nuGfxSwapCfb);
    nuGfxTaskMgrInit();
    nuGfxThreadStart();
}
