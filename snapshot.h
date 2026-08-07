/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _SNAPSHOT_H_
#define _SNAPSHOT_H_

#include "snes9x.h"

#define SNAPSHOT_MAGIC			"#!s9xsnp"
#define SNAPSHOT_VERSION_IRQ		7
#define SNAPSHOT_VERSION_BAPU		8
#define SNAPSHOT_VERSION_IRQ_2018	11		// irq changes were introduced earlier, since this we store NextIRQTimer directly
#define SNAPSHOT_VERSION			14

#define SUCCESS					1
#define WRONG_FORMAT			(-1)
#define WRONG_VERSION			(-2)
#define FILE_NOT_FOUND			(-3)

uint32 S9xFreezeSize (void);
bool8 S9xFreezeGameMem (uint8 *,uint32);
int S9xUnfreezeGameMem (const uint8 *,uint32);
struct ByteStream;
void S9xFreezeToStream (struct ByteStream *);
int	 S9xUnfreezeFromStream (struct ByteStream *);

#endif
