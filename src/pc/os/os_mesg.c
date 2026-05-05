#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_message.h>

// Per-queue PC state. A pointer to this struct is stored in mq->mtqueue,
// which is an OS-internal field never accessed by game code directly.
typedef struct {
    SDL_mutex *mutex;
    SDL_cond  *not_empty;
    SDL_cond  *not_full;
} PCMesgState;

static PCMesgState *pc_state(OSMesgQueue *mq) {
    return (PCMesgState *)(uintptr_t)mq->mtqueue;
}

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msgBuf, s32 count) {
    PCMesgState *s = SDL_malloc(sizeof(PCMesgState));
    s->mutex     = SDL_CreateMutex();
    s->not_empty = SDL_CreateCond();
    s->not_full  = SDL_CreateCond();

    mq->mtqueue    = (OSThread *)(uintptr_t)s;
    mq->fullqueue  = NULL;
    mq->validCount = 0;
    mq->first      = 0;
    mq->msgCount   = count;
    mq->msg        = msgBuf;
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag) {
    PCMesgState *s = pc_state(mq);
    SDL_LockMutex(s->mutex);

    if (flag == OS_MESG_BLOCK) {
        while (mq->validCount >= mq->msgCount) {
            SDL_CondWait(s->not_full, s->mutex);
        }
    } else if (mq->validCount >= mq->msgCount) {
        SDL_UnlockMutex(s->mutex);
        return -1;
    }

    s32 slot = (mq->first + mq->validCount) % mq->msgCount;
    mq->msg[slot] = msg;
    mq->validCount++;

    SDL_CondSignal(s->not_empty);
    SDL_UnlockMutex(s->mutex);
    return 0;
}

// osJamMesg inserts at the front of the queue (higher priority than osSendMesg).
s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag) {
    PCMesgState *s = pc_state(mq);
    SDL_LockMutex(s->mutex);

    if (flag == OS_MESG_BLOCK) {
        while (mq->validCount >= mq->msgCount) {
            SDL_CondWait(s->not_full, s->mutex);
        }
    } else if (mq->validCount >= mq->msgCount) {
        SDL_UnlockMutex(s->mutex);
        return -1;
    }

    mq->first = (mq->first - 1 + mq->msgCount) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;

    SDL_CondSignal(s->not_empty);
    SDL_UnlockMutex(s->mutex);
    return 0;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag) {
    PCMesgState *s = pc_state(mq);
    SDL_LockMutex(s->mutex);

    if (flag == OS_MESG_BLOCK) {
        while (mq->validCount == 0) {
            SDL_CondWait(s->not_empty, s->mutex);
        }
    } else if (mq->validCount == 0) {
        SDL_UnlockMutex(s->mutex);
        return -1;
    }

    if (msg != NULL) {
        *msg = mq->msg[mq->first];
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;

    SDL_CondSignal(s->not_full);
    SDL_UnlockMutex(s->mutex);
    return 0;
}

void osSetEventMesg(OSEvent event, OSMesgQueue *mq, OSMesg msg) {
    // TODO: Hardware events (VI retrace, SP/DP done, fault) are wired up in the
    // NuSystem scheduler stub.  No-op until then.
    (void)event;
    (void)mq;
    (void)msg;
}
