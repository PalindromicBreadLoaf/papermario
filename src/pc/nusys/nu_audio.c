#include <PR/os_thread.h>
#include <PR/os_message.h>
#include <nu/nusys.h>
#include "audio_mix.h"
#include "audio_pc.h"

#define AU_N_VOICES         24
#define AU_THREAD_PRI       70      // NU_AU_MGR_THREAD_PRI
#define AU_THREAD_ID        6       // NU_AU_MGR_THREAD_ID
#define AU_STACK_SIZE       0x2000  // NU_AU_STACK_SIZE
#define SAMPLES_PER_RETRACE (3 * PC_AUDIO_SAMPLES)

static PcVoiceInfo   sVoices[AU_N_VOICES];
static PcAudioDriver sDriver;
static NUScClient    sAuClient;
static OSMesgQueue   sAuMesgQ;
static OSMesg        sAuMsgBuf[NU_SC_MAX_MESGS];
static OSThread      sAuThread;
static u8            sAuStack[AU_STACK_SIZE];

static void audio_thread(void *arg) {
    static s16 sBuf[SAMPLES_PER_RETRACE * 2];
    NUScMsg *mesgType;

    (void)arg;
    osCreateMesgQueue(&sAuMesgQ, sAuMsgBuf, NU_SC_MAX_MESGS);
    nuScAddClient(&sAuClient, &sAuMesgQ, NU_SC_RETRACE_MSG);

    while (1) {
        osRecvMesg(&sAuMesgQ, (OSMesg *)&mesgType, OS_MESG_BLOCK);
        if (*mesgType == NU_SC_RETRACE_MSG) {
            pc_audio_frame(sBuf, SAMPLES_PER_RETRACE);
            audio_pc_push_samples(sBuf, SAMPLES_PER_RETRACE);
        }
    }
}

void nu_audio_init(void) {
    for (int i = 0; i < AU_N_VOICES; i++) {
        sVoices[i].frame_cache_idx = -1;
    }
    sDriver.voices          = sVoices;
    sDriver.n_voices        = AU_N_VOICES;
    sDriver.pre_video_frame = NULL;
    sDriver.pre_audio_frame = NULL;
    audio_mix_set_driver(&sDriver);
    osCreateThread(&sAuThread, AU_THREAD_ID, audio_thread, NULL,
                   sAuStack + AU_STACK_SIZE, AU_THREAD_PRI);
    osStartThread(&sAuThread);
}
