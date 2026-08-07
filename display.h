/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include "snes9x.h"

// Routines the port has to implement even if it doesn't use them

void S9xInitDisplay (int, char **);
void S9xDeinitDisplay (void);
void S9xTextMode (void);
bool8 S9xOpenSnapshotFile (const char *, bool8, STREAM *);
void S9xCloseSnapshotFile (STREAM);
const char * S9xStringInput (const char *);

// Routines the port may implement as needed

void S9xInitInputDevices (void);
const char * S9xSelectFilename (const char *, const char *, const char *, const char *);

#endif
