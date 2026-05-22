#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_vi.h>
#include <nu/nusys.h>
#include "gbi_interpreter.h"
#include "gl_backend.h"

// nuGfxCfb, nuGfxZBuffer, and nuYieldBuf are defined in the game source (main_loop.c / main.c).
u16            *nuGfxCfb_ptr       = NULL;
u32             nuGfxCfbNum        = NU_GFX_FRAMEBUFFER_NUM;
u32             nuGfxDisplay       = NU_GFX_DISPLAY_OFF;
u32             nuGfxCfbCounter    = 0;
volatile u32    nuGfxTaskSpool     = 0;
NUScTask        nuGfxTask[NU_GFX_TASK_NUM];
NUUcode        *nuGfxUcode         = NULL;
NUGfxSwapCfbFunc nuGfxSwapCfbFunc  = NULL;
NUGfxFunc       nuGfxFunc          = NULL;
NUGfxPreNMIFunc nuGfxPreNMIFunc    = NULL;
OSThread        nuGfxThread;

extern u16 gFrameBuf0[];
extern u16 gFrameBuf1[];
extern u16 gFrameBuf2[];

static u16 *sFrameBuffers[] = {
    gFrameBuf0, gFrameBuf1, gFrameBuf2
};
static u16 sZBuffer[320 * 240];

// nuGfxMesgQ is used by the gfx thread; nuGfxTaskEndFunc is internal.
OSMesgQueue     nuGfxMesgQ;
static OSMesg   sGfxMesgBuf[NU_GFX_MESGS];

static void gfx_thread(void *arg) {
    NUScClient gfxClient;
    NUScMsg   *mesgType;

    (void)arg;
    gl_backend_make_context_current();

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

static bool s_frame_started = false;

void nuGfxTaskStart(Gfx *gfxList, u32 gfxListSize, u32 ucode, u32 flag) {
    (void)gfxListSize; (void)ucode;
    nuGfxTaskSpool++;

    if (!s_frame_started) {
        gl_backend_start_frame();
        s_frame_started = true;
    }

    gbi_run_dl(gfxList);

    if (flag & NU_SC_SWAPBUFFER) {
        gl_backend_end_frame();
        if (nuGfxCfb != NULL && nuGfxCfbNum != 0) {
            NUScTask task = { 0 };

            task.framebuffer = nuGfxCfb_ptr;
            if (nuGfxSwapCfbFunc != NULL) {
                nuGfxSwapCfbFunc(&task);
            }

            nuGfxCfbCounter = (nuGfxCfbCounter + 1) % nuGfxCfbNum;
            nuGfxCfb_ptr = nuGfxCfb[nuGfxCfbCounter];
        }
        s_frame_started = false;
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
    nuGfxSetCfb(sFrameBuffers, NU_GFX_FRAMEBUFFER_NUM);
    nuGfxSetZBuffer(sZBuffer);
}
