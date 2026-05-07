#include <PR/os_thread.h>
#include <PR/os_message.h>
#include <PR/abi.h>
#include <nu/nusys.h>
#include "syn_driver_pc.h"
#include "audio_pc.h"

#define SAMPLES_PER_RETRACE (3 * PC_AUDIO_SAMPLES)

#define AU_THREAD_PRI   70
#define AU_THREAD_ID    6
#define AU_STACK_SIZE   0x2000

static NUScClient sAuClient;
static OSMesgQueue sAuMesgQ;
static OSMesg sAuMsgBuf[NU_SC_MAX_MESGS];
static OSThread sAuThread;
static u8 sAuStack[AU_STACK_SIZE];

static void audio_thread(void *arg) {
    static s16 sBuf[SAMPLES_PER_RETRACE * 2];
    static Acmd sCmdDummy;
    s32 cmdLen;
    NUScMsg *mesgType;

    (void)arg;
    osCreateMesgQueue(&sAuMesgQ, sAuMsgBuf, NU_SC_MAX_MESGS);
    nuScAddClient(&sAuClient, &sAuMesgQ, NU_SC_RETRACE_MSG);

    while (1) {
        osRecvMesg(&sAuMesgQ, (OSMesg *)&mesgType, OS_MESG_BLOCK);
        if (*mesgType == NU_SC_RETRACE_MSG) {
            alAudioFrame(&sCmdDummy, &cmdLen, sBuf, SAMPLES_PER_RETRACE);
            audio_pc_push_samples(sBuf, SAMPLES_PER_RETRACE);
        }
    }
}

void nu_audio_init(void) {
    pc_syn_init();
    osCreateThread(&sAuThread, AU_THREAD_ID, audio_thread, NULL,
                   sAuStack + AU_STACK_SIZE, AU_THREAD_PRI);
    osStartThread(&sAuThread);
}
