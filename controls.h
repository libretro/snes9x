/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _CONTROLS_H_
#define _CONTROLS_H_

#define S9xNoMapping			0
#define S9xButtonMouse			2
#define S9xButtonSuperscope		3
#define S9xButtonJustifier		4
#define S9xButtonMulti			6
#define S9xButtonMacsRifle		7
#define S9xPointer				9

#define S9xBadMapping			255
#define InvalidControlID		((uint32) -1)

typedef struct
{
	uint8	type;
	uint8	multi_press:2;

	union
	{
		union
		{
			struct
			{
				uint8	idx:1;				// Mouse number 0-1
				uint8	left:1;				// buttons
				uint8	right:1;
			}	mouse;

			struct
			{
				uint8	fire:1;
				uint8	cursor:1;
				uint8	turbo:1;
				uint8	pause:1;
				uint8	aim_offscreen:1;	// Pretend we're pointing the gun offscreen (ignore the pointer)
			}	scope;

			struct
			{
				uint8	idx:1;				// Justifier number 0-1
				uint8	trigger:1;			// buttons
				uint8	start:1;
				uint8	aim_offscreen:1;	// Pretend we're pointing the gun offscreen (ignore the pointer)
			}	justifier;

			struct
			{
				uint8	trigger:1;
			}	macsrifle;

			int32	multi_idx;
			uint16	command;
		}	button;

		struct								// Which SNES-pointers to control with this pointer
		{
			uint16	aim_mouse0:1;
			uint16	aim_mouse1:1;
			uint16	aim_scope:1;
			uint16	aim_justifier0:1;
			uint16	aim_justifier1:1;
			uint16	aim_macsrifle:1;
		}	pointer;

		uint8	port[4];
	};
}	s9xcommand_t;

// Starting out...

void S9xUnmapAllControls (void);

// Setting which controllers are plugged in.

enum controllers
{
	CTL_NONE,		// all ids ignored
	CTL_JOYPAD,		// use id1 to specify 0-7
	CTL_MOUSE,		// use id1 to specify 0-1
	CTL_SUPERSCOPE,
	CTL_JUSTIFIER,	// use id1: 0=one justifier, 1=two justifiers
	CTL_MP5,			// use id1-id4 to specify pad 0-7 (or -1)
	CTL_MACSRIFLE
};

void S9xSetController (int port, enum controllers controller, int8 id1, int8 id2, int8 id3, int8 id4); // port=0-1

// Call this when you're done with S9xSetController, or if you change any of the controller Settings.*Master flags. 
// Returns true if something was disabled.

bool S9xVerifyControllers (void);

// Functions for translation s9xcommand_t's into strings, and vice versa.
// free() the returned string after you're done with it.

s9xcommand_t S9xGetCommandT (const char *name);

// Generic mapping functions

void S9xUnmapID (uint32 id);

// Button mapping functions.
// Call S9xReportButton() whenever the button state changes.
// S9xMapButton() will fail and return FALSE if mapping.type isn't an S9xButton* type.

bool S9xMapButton (uint32 id, s9xcommand_t mapping);
void S9xReportButton (uint32 id, bool pressed);

// Pointer mapping functions.
// Call S9xReportPointer() whenever the pointer position changes.
// S9xMapPointer() will fail and return FALSE if mapping.type isn't an S9xPointer* type.

// Note that position [0,0] is considered the upper-left corner of the 'screen',
// and either [255,223] or [255,239] is the lower-right.
// Note that the SNES mouse doesn't aim at a particular point,
// so the SNES's idea of where the mouse pointer is will probably differ from your OS's idea.

bool S9xMapPointer (uint32 id, s9xcommand_t mapping);
void S9xReportPointer (uint32 id, int16 x, int16 y);

// Do whatever the s9xcommand_t says to do.
// If cmd.type is a button type, data1 should be TRUE (non-0) or FALSE (0) to indicate whether the 'button' is pressed or released.
// If cmd.type is a pointer, data1 and data2 are the positions of the pointer.

void S9xApplyCommand (s9xcommand_t cmd, int16 data1, int16 data2);

//////////
// These functions are called by snes9x into your port, so each port should implement them.

// Called before already-read SNES joypad data is being used by the game if your port defines SNES_JOY_READ_CALLBACKS.

#ifdef SNES_JOY_READ_CALLBACKS
void S9xOnSNESPadRead (void);
#endif

//////////
// These functions are called from snes9x into this subsystem. No need to use them from a port.

// Use when resetting snes9x.

void S9xControlsReset (void);
void S9xControlsSoftReset (void);

// Use when writing to $4016.

void S9xSetJoypadLatch (bool latch);

// Use when reading $4016/7 (JOYSER0 and JOYSER1).

uint8 S9xReadJOYSERn (int n);

// End-Of-Frame processing. Sets gun latch variables and tries to draw crosshairs

void S9xControlEOF (void);
void S9xSetJoypadButtons (int pad, uint16 buttons);

// Functions and a structure for snapshot.

struct SControlSnapshot
{
	uint8	ver;
	uint8	port1_read_idx[2];
	uint8	dummy1[4];					// for future expansion
	uint8	port2_read_idx[2];
	uint8	dummy2[4];
	uint8	mouse_speed[2];
	uint8	justifier_select;
	uint8	dummy3[8];
	bool8	pad_read, pad_read_last;
	uint8	internal[60];				// yes, we need to save this!
	uint8   internal_macs[5];
};

void S9xControlPreSaveState (struct SControlSnapshot *s);
void S9xControlPostLoadState (struct SControlSnapshot *s);

#endif
