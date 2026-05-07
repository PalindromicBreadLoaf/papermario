#include <SDL2/SDL.h>
#include <PR/os_thread.h>
#include <PR/osint.h>

struct __osThreadTail __osThreadTail  = { NULL, -1 };
OSThread *__osRunQueue      = (OSThread *)&__osThreadTail;
OSThread *__osActiveQueue   = (OSThread *)&__osThreadTail;
OSThread *__osRunningThread = NULL;
OSThread *__osFaultedThread = NULL;

void __osEnqueueThread(OSThread **queue, OSThread *t) {
    OSThread *pred = (OSThread *)queue;
    OSThread *succ = pred->next;

    while (succ != NULL && succ->priority >= t->priority) {
        pred = succ;
        succ = pred->next;
    }

    pred->next = t;
    t->next    = succ;
    t->queue   = queue;
}

void __osDequeueThread(OSThread **queue, OSThread *t) {
    OSThread *pred = (OSThread *)queue;
    OSThread *succ = pred->next;

    while (succ != NULL) {
        if (succ == t) {
            pred->next = t->next;
            t->queue   = NULL;
            return;
        }
        pred = succ;
        succ = pred->next;
    }
}

OSThread *__osPopThread(OSThread **queue) {
    OSThread *t = *queue;
    if (t == (OSThread *)&__osThreadTail || t == NULL) {
        return NULL;
    }
    *queue = t->next;
    t->next  = NULL;
    t->queue = NULL;
    return t;
}

void __osDispatchThread(void) {
    SDL_Delay(0);
}

void __osEnqueueAndYield(OSThread **queue) {
    (void)queue;
    SDL_Delay(0);
}

void __osCleanupThread(void) {}
