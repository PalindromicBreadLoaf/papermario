#ifndef PC_SYN_DRIVER_PC_H
#define PC_SYN_DRIVER_PC_H

#include <PR/ultratypes.h>
#include <PR/abi.h>
#include "audio_mix.h"

// Initialise the PC audio synthesis engine and register it with audio_mix.
// Must be called once before any au_syn_* function or alAudioFrame.
void pc_syn_init(void);

// Drives the per-frame game audio callbacks and fills outBuf with interleaved
// stereo s16 PCM. cmdList is returned unchanged; *cmdLen is set to 0.
Acmd *alAudioFrame(Acmd *cmdList, s32 *cmdLen, s16 *outBuf, s32 outLen);

#endif /* PC_SYN_DRIVER_PC_H */
