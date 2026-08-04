/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef _GFX_H_
#define _GFX_H_

#include "port.h"

/* Dimension constants live in snes9x.h, which is a C++ header (it pulls
   stream.h). tile.c is C and includes this header only, so mirror them
   here; the static assert in gfx.cpp keeps the two in sync. */
#ifndef SNES_WIDTH
#define SNES_WIDTH				256
#define SNES_HEIGHT				224
#define SNES_HEIGHT_EXTENDED	239
#define MAX_SNES_WIDTH			(SNES_WIDTH * 2)
#define MAX_SNES_HEIGHT			(SNES_HEIGHT_EXTENDED * 2)
#endif

#ifndef MAX_SNES_WIDTH_4X
#define MAX_SNES_WIDTH_4X		(SNES_WIDTH * 4)	/* Mode 7 hires 4x */
#endif

#ifdef __cplusplus
#include <vector>
#include <string>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct SGFX
{
	/* Pitch/RealPPL/ScreenSize were C++11 in-class consts and ScreenBuffer
	   a std::vector; both made this struct invisible to C. Plain fields
	   now, set in S9xGraphicsInit before anything reads them; the screen
	   allocation lives in gfx.cpp (GFXScreenBuffer). */
	uint32	Pitch;
	uint32	RealPPL;
	uint32	ScreenSize;
	uint16	*Screen;
	uint16	*SubScreen;
	uint8	*ZBuffer;
	uint8	*SubZBuffer;
	uint16	*S;
	uint8	*DB;
	uint16	*ZERO;
	uint32	PPL;				// number of pixels on each of Screen buffer
	uint32	LinesPerTile;		// number of lines in 1 tile (4 or 8 due to interlace)
	uint16	*ScreenColors;		// screen colors for rendering main
	uint16	*RealScreenColors;	// screen colors, ignoring color window clipping
	uint8	Z1;					// depth for comparison
	uint8	Z2;					// depth to save
	uint32	FixedColour;
	uint8	DoInterlace;
	uint32	StartY;
	uint32	EndY;
	bool8	ClipColors;
	uint8	OBJWidths[128];
	uint8	OBJVisibleTiles[128];

	struct ClipData	*Clip;

	struct
	{
		uint8	RTOFlags;
		int16	Tiles;

		struct
		{
			int8	Sprite;
			uint8	Line;
		}	OBJ[128];
	}	OBJLines[SNES_HEIGHT_EXTENDED];

	void	(*DrawBackdropMath) (uint32, uint32, uint32);
	void	(*DrawBackdropNomath) (uint32, uint32, uint32);
	void	(*DrawTileMath) (uint32, uint32, uint32, uint32);
	void	(*DrawTileNomath) (uint32, uint32, uint32, uint32);
	void	(*DrawClippedTileMath) (uint32, uint32, uint32, uint32, uint32, uint32);
	void	(*DrawClippedTileNomath) (uint32, uint32, uint32, uint32, uint32, uint32);
	void	(*DrawMosaicPixelMath) (uint32, uint32, uint32, uint32, uint32, uint32);
	void	(*DrawMosaicPixelNomath) (uint32, uint32, uint32, uint32, uint32, uint32);
	void	(*DrawMode7BG1Math) (uint32, uint32, int);
	void	(*DrawMode7BG1Nomath) (uint32, uint32, int);
	void	(*DrawMode7BG2Math) (uint32, uint32, int);
	void	(*DrawMode7BG2Nomath) (uint32, uint32, int);

	uint32	InfoStringTimeout;
	char	FrameDisplayString[256];
};

struct SBG
{
	uint8	(*ConvertTile) (uint8 *, uint32, uint32);
	uint8	(*ConvertTileFlip) (uint8 *, uint32, uint32);

	uint32	TileSizeH;
	uint32	TileSizeV;
	uint32	OffsetSizeH;
	uint32	OffsetSizeV;
	uint32	TileShift;
	uint32	TileAddress;
	uint32	NameSelect;
	uint32	SCBase;

	uint32	StartPalette;
	uint32	PaletteShift;
	uint32	PaletteMask;
	uint8	EnableMath;
	uint8	InterlaceLine;

	uint8	*Buffer;
	uint8	*BufferFlip;
	uint8	*Buffered;
	uint8	*BufferedFlip;
	bool8	DirectColourMode;
};

struct SLineData
{
	struct
	{
		uint16	VOffset;
		uint16	HOffset;
	}	BG[4];
};

struct SLineMatrixData
{
	short	MatrixA;
	short	MatrixB;
	short	MatrixC;
	short	MatrixD;
	short	CentreX;
	short	CentreY;
	short	M7HOFS;
	short	M7VOFS;
};

extern uint16		BlackColourMap[256];
extern uint16		DirectColourMaps[8][256];
extern uint8		mul_brightness[16][32];
extern uint8		brightness_cap[64];
extern struct SBG	BG;
extern struct SGFX	GFX;

#define H_FLIP		0x4000
#define V_FLIP		0x8000
#define BLANK_TILE	2

#ifndef __cplusplus
/* C expression forms of the color math for tile.c, lifted verbatim from
   snes9x2010 (the C++ side uses the struct forms below instead). */
#define COLOR_ADD1_2(C1, C2) \
	((((((C1) & RGB_REMOVE_LOW_BITS_MASK) + \
	((C2) & RGB_REMOVE_LOW_BITS_MASK)) >> 1) + \
	((C1) & (C2) & RGB_LOW_BITS_MASK)) | ALPHA_BITS_MASK)

/* Exact per-channel saturating RGB addition, transplanted from mainline
 * snes9x's COLOR_ADD::fn with its RGB565 constants (5-bit lanes at bit
 * 11 / 6 / 0, then the green top bit propagated into the extra low
 * green bit). The previous form approximated the full-strength add
 * through the X2 half-add table (halve, table-double), which loses the
 * low bit per channel and saturates through the table instead of per
 * channel; visible as off-by-one channels in additive color math.
 * Expression macro with the same argument rules as COLOR_SUB below. */
#define CADD_RB_MASK   ((0x1F << 11) | 0x1F)
#define CADD_G_MASK    (0x1F << 6)
#define CADD_RB_CARRY  ((0x20 << 11) | 0x20)
#define CADD_G_CARRY   (0x20 << 6)
#define COLOR_ADD_RAW(C1, C2) \
	((uint16_t) ((((((C1) & CADD_RB_MASK) + ((C2) & CADD_RB_MASK)) & CADD_RB_MASK) \
		| ((((C1) & CADD_G_MASK) + ((C2) & CADD_G_MASK)) & CADD_G_MASK)) \
		| ((uint16_t) ((((((C1) & CADD_G_MASK) + ((C2) & CADD_G_MASK)) & CADD_G_CARRY) \
			| ((((C1) & CADD_RB_MASK) + ((C2) & CADD_RB_MASK)) & CADD_RB_CARRY)) >> 5) * 0x1f)))
#define COLOR_ADD(C1, C2) \
	((uint16_t) (COLOR_ADD_RAW((C1), (C2)) | ((COLOR_ADD_RAW((C1), (C2)) & 0x0400) >> 5)))

/* Brightness-capped additive math, from mainline snes9x. ScreenColors
 * are pre-scaled by master brightness, so a plain saturating add clamps
 * at 31 instead of at the brightness-scaled maximum; on hardware the
 * math runs on raw CGRAM values and brightness is applied at the DAC
 * (ares packs displayBrightness into the output and scales afterward).
 * brightness_cap[] clamps each 5-bit channel sum to XB[0x1f]. Selected
 * by S9xSelectTileRenderers when PPU.Brightness != 0xf. The half-add
 * form is unaffected (halving cannot exceed the scaled maximum), so
 * COLOR_ADD_BRIGHTNESS1_2 aliases COLOR_ADD1_2, and the token-pasted
 * REGMATH/MATHS1_2 selectors work with Op = ADD_BRIGHTNESS unchanged. */
#define COLOR_ADD_BRIGHTNESS(C1, C2) \
	((uint16_t) (((uint16_t) brightness_cap[(((C1) >> 11) & 0x1f) + (((C2) >> 11) & 0x1f)] << 11) | \
		((uint16_t) brightness_cap[(((C1) >>  6) & 0x1f) + (((C2) >>  6) & 0x1f)] <<  6) | \
		(((uint16_t) brightness_cap[(((C1) >>  6) & 0x1f) + (((C2) >>  6) & 0x1f)] & 0x10) << 1) | \
		((uint16_t) brightness_cap[ ((C1)        & 0x1f) + ( (C2)        & 0x1f)])))
#define COLOR_ADD_BRIGHTNESS1_2(C1, C2) COLOR_ADD1_2((C1), (C2))

#define COLOR_SUB1_2(C1, C2) \
	GFX.ZERO[(((C1) | RGB_HI_BITS_MASKx2) - \
	((C2) & RGB_REMOVE_LOW_BITS_MASK)) >> 1]

/* Subtraction, mainline-exact (COLOR_SUB::fn as an expression macro):
 * borrow-guarded 5-bit lane math with joint saturate, then the RGB565
 * green-LSB propagation. The per-channel ternary form this replaces
 * (lifted from snes9x2010) lacked the green fixup, which showed up as
 * a -0x20 green delta on saturating subtractive math. Same argument
 * rules as the ADD macros above. */
#define CSUB_RBMASK  (THIRD_COLOR_MASK | FIRST_COLOR_MASK)
#define CSUB_RB(C1, C2) \
	(((int) (((C1) & CSUB_RBMASK) | ((0x20 << 0) | (0x20 << RED_SHIFT_BITS)))) - \
	 ((int) ((C2) & CSUB_RBMASK)))
#define CSUB_G(C1, C2) \
	(((int) (((C1) & SECOND_COLOR_MASK) | (0x20 << GREEN_SHIFT_BITS))) - \
	 ((int) ((C2) & SECOND_COLOR_MASK)))
#define CSUB_SAT(C1, C2) \
	((((CSUB_G((C1), (C2)) & (0x20 << GREEN_SHIFT_BITS)) | \
	   (CSUB_RB((C1), (C2)) & ((0x20 << RED_SHIFT_BITS) | (0x20 << 0)))) >> 5) * 0x1f)
#define COLOR_SUB_RAW(C1, C2) \
	((uint16) (((CSUB_RB((C1), (C2)) & CSUB_RBMASK) | \
		(CSUB_G((C1), (C2)) & SECOND_COLOR_MASK)) & CSUB_SAT((C1), (C2))))
#define COLOR_SUB(C1, C2) \
	((uint16) (COLOR_SUB_RAW((C1), (C2)) | ((COLOR_SUB_RAW((C1), (C2)) & 0x0400) >> 5)))
#endif /* !__cplusplus */

#ifdef __cplusplus
}	/* extern "C" */
/* C++ side only (gfx.cpp checkerboard blend); the tile renderer is C
   and carries its own expression-macro forms of these. */
struct COLOR_ADD
{
	static alwaysinline uint16 fn(uint16 C1, uint16 C2)
	{
		const int RED_MASK = 0x1F << RED_SHIFT_BITS;
		const int GREEN_MASK = 0x1F << GREEN_SHIFT_BITS;
		const int BLUE_MASK = 0x1F;

		int rb = C1 & (RED_MASK | BLUE_MASK);
		rb += C2 & (RED_MASK | BLUE_MASK);
		int rbcarry = rb & ((0x20 << RED_SHIFT_BITS) | (0x20 << 0));
		int g = (C1 & (GREEN_MASK)) + (C2 & (GREEN_MASK));
		int rgbsaturate = (((g & (0x20 << GREEN_SHIFT_BITS)) | rbcarry) >> 5) * 0x1f;
		uint16 retval = (rb & (RED_MASK | BLUE_MASK)) | (g & GREEN_MASK) | rgbsaturate;
#if GREEN_SHIFT_BITS == 6
		retval |= (retval & 0x0400) >> 5;
#endif
		return retval;
	}

	static alwaysinline uint16 fn1_2(uint16 C1, uint16 C2)
	{
		return ((((C1 & RGB_REMOVE_LOW_BITS_MASK) +
			(C2 & RGB_REMOVE_LOW_BITS_MASK)) >> 1) +
			(C1 & C2 & RGB_LOW_BITS_MASK)) | ALPHA_BITS_MASK;
	}
};

struct COLOR_ADD_BRIGHTNESS
{
	static alwaysinline uint16 fn(uint16 C1, uint16 C2)
	{
		return ((brightness_cap[ (C1 >> RED_SHIFT_BITS)           +  (C2 >> RED_SHIFT_BITS)          ] << RED_SHIFT_BITS)   |
				(brightness_cap[((C1 >> GREEN_SHIFT_BITS) & 0x1f) + ((C2 >> GREEN_SHIFT_BITS) & 0x1f)] << GREEN_SHIFT_BITS) |
	// Proper 15->16bit color conversion moves the high bit of green into the low bit.
	#if GREEN_SHIFT_BITS == 6
			   ((brightness_cap[((C1 >> 6) & 0x1f) + ((C2 >> 6) & 0x1f)] & 0x10) << 1) |
	#endif
				(brightness_cap[ (C1                      & 0x1f) +  (C2                      & 0x1f)]      ));
	}

	static alwaysinline uint16 fn1_2(uint16 C1, uint16 C2)
	{
		return COLOR_ADD::fn1_2(C1, C2);
	}
};


struct COLOR_SUB
{
	static alwaysinline uint16 fn(uint16 C1, uint16 C2)
	{
		int rb1 = (C1 & (THIRD_COLOR_MASK | FIRST_COLOR_MASK)) | ((0x20 << 0) | (0x20 << RED_SHIFT_BITS));
		int rb2 = C2 & (THIRD_COLOR_MASK | FIRST_COLOR_MASK);
		int rb = rb1 - rb2;
		int rbcarry = rb & ((0x20 << RED_SHIFT_BITS) | (0x20 << 0));
		int g = ((C1 & (SECOND_COLOR_MASK)) | (0x20 << GREEN_SHIFT_BITS)) - (C2 & (SECOND_COLOR_MASK));
		int rgbsaturate = (((g & (0x20 << GREEN_SHIFT_BITS)) | rbcarry) >> 5) * 0x1f;
		uint16 retval = ((rb & (THIRD_COLOR_MASK | FIRST_COLOR_MASK)) | (g & SECOND_COLOR_MASK)) & rgbsaturate;
#if GREEN_SHIFT_BITS == 6
		retval |= (retval & 0x0400) >> 5;
#endif
		return retval;
	}

	static alwaysinline uint16 fn1_2(uint16 C1, uint16 C2)
	{
		return GFX.ZERO[((C1 | RGB_HI_BITS_MASKx2) -
			(C2 & RGB_REMOVE_LOW_BITS_MASK)) >> 1];
	}
};
extern "C" {
#endif /* __cplusplus */

void S9xStartScreenRefresh (void);
void S9xEndScreenRefresh (void);
void S9xBuildDirectColourMaps (void);
void RenderLine (uint8);
void S9xComputeClipWindows (void);
void S9xDisplayChar (uint16 *, uint8);
void S9xGraphicsScreenResize (void);
// called automatically unless Settings.AutoDisplayMessages is false
void S9xDisplayMessages (uint16 *, int, int, int, int);

// external port interface which must be implemented or initialised for each port
bool8 S9xGraphicsInit (void);
void S9xGraphicsDeinit (void);
bool8 S9xInitUpdate (void);
bool8 S9xDeinitUpdate (int, int);
bool8 S9xContinueUpdate (int, int);
void S9xSyncSpeed (void);

#ifdef __cplusplus
}	/* extern "C" */

// called instead of S9xDisplayString if set to non-NULL
extern void (*S9xCustomDisplayString) (const char *, int, int, bool, int type);
#endif

#endif
