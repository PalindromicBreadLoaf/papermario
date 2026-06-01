#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <nu/nusys.h>

NUSched nusched;
u32            nuScRetraceCounter = 0;
NUScPreNMIFunc nuScPreNMIFunc     = NULL;
u8             nuScPreNMIFlag     = NU_SC_PRENMI_YET;

static SDL_mutex   *sClientMutex;
static SDL_Thread  *sRetraceThread;
static SDL_atomic_t sRetraceQuit;

void nuScEventBroadcast(NUScMsg *msg) {
    SDL_LockMutex(sClientMutex);
    for (NUScClient *c = nusched.clientList; c != NULL; c = c->next) {
        if (c->msgType & *msg)
            osSendMesg(c->msgQ, msg, OS_MESG_NOBLOCK);
    }
    SDL_UnlockMutex(sClientMutex);
}

// Scale retrace timing by nusched.retraceCount so audio buffer sizing matches
// the game retrace cadence.
static int retrace_thread_func(void *arg) {
    (void)arg;
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 period = (perf_freq * nusched.retraceCount + (nusched.frameRate / 2)) / nusched.frameRate;
    Uint64 next = SDL_GetPerformanceCounter() + period;

    while (!SDL_AtomicGet(&sRetraceQuit)) {
        Uint64 now = SDL_GetPerformanceCounter();

        if (now >= next) {
            next += period;
            nuScRetraceCounter++;
            nuScEventBroadcast(&nusched.retraceMsg);
        } else {
            Uint64 remaining_ms = ((next - now) * 1000) / perf_freq;
            SDL_Delay((remaining_ms > 1) ? (Uint32)(remaining_ms - 1) : 0);
        }
    }
    return 0;
}

void nuScCreateScheduler(u8 videoMode, u8 numFields) {
    sClientMutex = SDL_CreateMutex();
    SDL_AtomicSet(&sRetraceQuit, 0);

    nusched.retraceMsg   = NU_SC_RETRACE_MSG;
    nusched.prenmiMsg    = NU_SC_PRENMI_MSG;
    nusched.retraceCount = numFields;
    nusched.frameBufferNum = 2;
    nusched.frameRate    = (osTvType == OS_TV_PAL) ? 50 : 60;
    nusched.clientList            = NULL;
    nusched.curGraphicsTask       = NULL;
    nusched.curAudioTask          = NULL;
    nusched.graphicsTaskSuspended = NULL;

    osCreateMesgQueue(&nusched.retraceMQ,         nusched.retraceMsgBuf,      NU_SC_MAX_MESGS);
    osCreateMesgQueue(&nusched.rspMQ,             nusched.rspMsgBuf,          NU_SC_MAX_MESGS);
    osCreateMesgQueue(&nusched.rdpMQ,             nusched.rdpMsgBuf,          NU_SC_MAX_MESGS);
    osCreateMesgQueue(&nusched.graphicsRequestMQ, nusched.graphicsRequestBuf, NU_SC_MAX_MESGS);
    osCreateMesgQueue(&nusched.audioRequestMQ,    nusched.audioRequestBuf,    NU_SC_MAX_MESGS);
    osCreateMesgQueue(&nusched.waitMQ,            nusched.waitMsgBuf,         NU_SC_MAX_MESGS);

    osViSetMode(&osViModeTable[videoMode]);

    sRetraceThread = SDL_CreateThread(retrace_thread_func, "NuRetrace", NULL);
}

void nuScAddClient(NUScClient *c, OSMesgQueue *mq, NUScMsg msgType) {
    SDL_LockMutex(sClientMutex);
    c->msgQ    = mq;
    c->msgType = msgType;
    c->next    = nusched.clientList;
    nusched.clientList = c;
    if ((msgType & NU_SC_PRENMI_MSG) && nuScPreNMIFlag)
        osSendMesg(mq, &nusched.prenmiMsg, OS_MESG_NOBLOCK);
    SDL_UnlockMutex(sClientMutex);
}

void nuScRemoveClient(NUScClient *client) {
    SDL_LockMutex(sClientMutex);
    for (NUScClient **p = &nusched.clientList; *p; p = &(*p)->next) {
        if (*p == client) { *p = client->next; break; }
    }
    SDL_UnlockMutex(sClientMutex);
}

void nuScResetClientMesgType(NUScClient *client, NUScMsg msgType) {
    SDL_LockMutex(sClientMutex);
    client->msgType = msgType;
    SDL_UnlockMutex(sClientMutex);
}

OSMesgQueue *nuScGetAudioMQ(void)        { return &nusched.audioRequestMQ;    }
OSMesgQueue *nuScGetGfxMQ(void)          { return &nusched.graphicsRequestMQ; }
s32          nuScGetFrameRate(void)      { return nusched.frameRate;           }
void         nuScSetFrameBufferNum(u8 n) { nusched.frameBufferNum = n;        }
