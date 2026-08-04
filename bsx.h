/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _BSX_H_
#define _BSX_H_

#include <stdio.h>
#include <streams/file_stream_transforms.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bridges to the C++ side (defined in bsx.c, wired in memmap.cpp). */
extern uint8_t **BSXMemMap;
extern uint8_t  *BSXBlockIsRAM;
extern uint8_t  *BSXBlockIsROM;
extern uint8_t  *BSXRAMBase;
extern uint8_t  *BSXSRAMBase;
extern uint8_t  *BSXPSRAMBase;
extern uint8_t  *BSXBIOSROMBase;
extern uint8_t  *BSXROMBase;


struct SBSX
{
	bool8	dirty;			/* Changed register values */
	bool8	dirty2;			/* Changed register values */
	bool8	bootup;			/* Start in bios mapping */
	bool8	flash_enable;	/* Flash state */
	bool8	write_enable;	/* ROM write protection */
	bool8	read_enable;	/* Allow card vendor reading */
	uint32	flash_command;	/* Flash command */
	uint32	old_write;		/* Previous flash write address */
	uint32	new_write;		/* Current flash write address */
	uint8	out_index;
	uint8	output[32];
	uint8	PPU[32];
	uint8	MMC[16];
	uint8	prevMMC[16];
	uint8	test2192[32];

	uint8_t	flash_csr;
	uint8_t	flash_gsr;
	uint8_t	flash_bsr;
	uint8_t	flash_cmd_done;

	RFILE			*sat_stream1;
	RFILE			*sat_stream2;

	uint8_t	sat_pf_latch1_enable, sat_dt_latch1_enable;
	uint8_t	sat_pf_latch2_enable, sat_dt_latch2_enable;

	uint8_t	sat_stream1_loaded, sat_stream2_loaded;
	uint8_t	sat_stream1_first, sat_stream2_first;
	uint8	sat_stream1_count, sat_stream2_count;
	uint16	sat_stream1_queue, sat_stream2_queue;
};

extern struct SBSX	BSX;

uint8 S9xGetBSX (uint32);
void S9xSetBSX (uint8, uint32);
uint8 S9xGetBSXPPU (uint16);
void S9xSetBSXPPU (uint8, uint16);
uint8 * S9xGetBasePointerBSX (uint32);
void S9xInitBSX (void);
void S9xResetBSX (void);
void S9xBSXPostLoadState (void);

#ifdef __cplusplus
}
#endif

#endif
