/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _APU_H_
#define _APU_H_

#include "../snes9x.h"

typedef void (*apu_callback) (void *);

#define SPC_SAVE_STATE_BLOCK_SIZE (1024 * 65)

bool8 S9xInitAPU (void);
void S9xDeinitAPU (void);
void S9xResetAPU (void);
void S9xSoftResetAPU (void);
uint8 S9xAPUReadPort (int);
void S9xAPUWritePort (int, uint8);
void S9xAPUExecute (void);
void S9xAPUEndScanline (void);
void S9xAPUSetReferenceTime (int32);
void S9xAPUTimingSetSpeedup (int);
void S9xAPULoadState (uint8 *);
void S9xAPULoadBlarggState(uint8 *oldblock);
void S9xAPUSaveState (uint8 *);

bool8 S9xInitSound (int);

int S9xGetSampleCount (void);
void S9xSetSoundControl (uint8);
void S9xSetSoundMute (bool8);
void S9xLandSamples (void);
void S9xClearSamples (void);
/* Hand over this frame's samples without copying them; see apu.cpp. */
const int16 * S9xDrainAudio (int *sample_count);
uint32 S9xGetAudioSampleRate (void);
void S9xSetSamplesAvailableCallback (apu_callback, void *);

#define DSP_INTERPOLATION_NONE     0
#define DSP_INTERPOLATION_LINEAR   1
#define DSP_INTERPOLATION_GAUSSIAN 2
#define DSP_INTERPOLATION_CUBIC    3
#define DSP_INTERPOLATION_SINC     4

#endif
