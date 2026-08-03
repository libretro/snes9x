/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _TILE_H_
#define _TILE_H_

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

void S9xInitTileRenderer (void);
void S9xSelectTileRenderers (int, uint8, uint8);
void S9xSelectTileConverter (int, uint8, uint8, uint8);
void S9xSelectTileRenderers_SFXSpeedup (void);
void S9xMode7DeinterleaveVRAM (void);

#ifdef __cplusplus
}
#endif

#endif
