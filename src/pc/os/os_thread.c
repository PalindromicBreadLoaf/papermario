#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/os_message.h>
#include <PR/os_exception.h>

// Overlaid on OSThread.context (0x190 bytes), which is MIPS register storage
// on N64 and unused on PC. sizeof(PCThreadCtx) must fit in __OSThreadContext.
typedef struct {
    SDL_Thread *sdl_thread;
    void (*entry)(void*);
    void *arg;
} PCThreadCtx;

_Static_assert(sizeof(PCThreadCtx) <= sizeof(__OSThreadContext),
               "PCThreadCtx too large for __OSThreadContext");

static PCThreadCtx *pc_ctx(OSThread *t) {
    return (PCThreadCtx *)&t->context;
}

static int SDLCALL thread_trampoline(void *raw) {
    OSThread *t = (OSThread *)raw;
    PCThreadCtx *ctx = pc_ctx(t);
    t->state = OS_STATE_RUNNING;
    ctx->entry(ctx->arg);
    t->state = OS_STATE_STOPPED;
    return 0;
}

void osCreateThread(OSThread *t, OSId id, void (*entry)(void*), void *arg, void *sp, OSPri pri) {
    (void)sp;
    (void)pri;
    PCThreadCtx *ctx = pc_ctx(t);
    t->id       = id;
    t->priority = pri;
    t->state    = OS_STATE_STOPPED;
    ctx->sdl_thread = NULL;
    ctx->entry  = entry;
    ctx->arg    = arg;
}

void osStartThread(OSThread *t) {
    PCThreadCtx *ctx = pc_ctx(t);
    t->state = OS_STATE_RUNNABLE;
    ctx->sdl_thread = SDL_CreateThread(thread_trampoline, "OSThread", t);
}

void osDestroyThread(OSThread *t) {
    PCThreadCtx *ctx = pc_ctx(t);
    if (ctx->sdl_thread) {
        SDL_DetachThread(ctx->sdl_thread);
        ctx->sdl_thread = NULL;
    }
}

void osStopThread(OSThread *t) {
    if (t == NULL) {
        SDL_Delay(SDL_MAX_SINT32);
    }
    // Stopping another thread safely is not possible without cooperation.
    // I believe this is only called on crash, so no-op should be fine.
}

void osYieldThread(void) {
    SDL_Delay(0);
}

OSId osGetThreadId(OSThread *t) {
    return t ? t->id : 0;
}

void osSetThreadPri(OSThread *t, OSPri pri) {
    (void)t;
    // N64 uses cooperative priority scheduling. On PC we let the OS scheduler
    // decide. It can hopefully do a good job.
    (void)pri;
}

OSPri osGetThreadPri(OSThread *t) {
    return t ? t->priority : 0;
}
