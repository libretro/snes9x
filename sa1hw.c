/* SA-1 hardware behaviors taken from snes9x2010 (sa1.c), kept as C.
 *
 * BW-RAM write protection and the type-1 character-conversion (CC1)
 * read engine, both verified against ares. The code operates on base
 * pointers assigned by S9xSA1Init() so it stays free of the C++
 * memory-map types.
 */

#include <stdint.h>
#include <string.h>

#include "sa1hw.h"

uint8_t  *SA1FillRAM   = 0;
uint8_t  *SA1BWRAMBase = 0;
uint32_t  SA1BWRAMMask = 0;

/* BW-RAM write protection.
 *
 * Mirrors ares' rule (SA1::BWRAM::writeCPU / writeLinear): a write to
 * the BW-RAM linear region is blocked only when BOTH write-enables are
 * off and the (18-bit) offset falls inside the protected window.
 *
 *   swen = $2226 bit7    (SNES-side BW-RAM write enable)
 *   cwen = $2227 bit7    (SA-1-side BW-RAM write enable)
 *   bwp  = $2228 bits0-3 (protect size; window = 0x100 << bwp)
 *
 * Protection applies to linear BW-RAM writes from both the S-CPU and
 * the SA-1; it does NOT apply to the bitmap write paths, matching
 * hardware. `bwoffset` is the BW-RAM-relative byte offset (same space
 * as the DSA register).
 *
 * Both write-enables reset to 0 and bwp resets to 0x0f, so BW-RAM is
 * fully protected at power-on until a game asserts SWEN or CWEN -- this
 * matches hardware and ares. */
int S9xSA1BWRAMWriteProtected (uint32_t bwoffset)
{
	if ((SA1FillRAM[0x2226] & 0x80) || (SA1FillRAM[0x2227] & 0x80))
		return (0); /* writes enabled */

	if ((bwoffset & 0x3ffff) < (0x100u << (SA1FillRAM[0x2228] & 0x0f)))
		return (1); /* inside protected window */

	return (0);
}

/* Type-1 character-conversion DMA read.
 *
 * When CC1 is armed (in_char_dma), the S-CPU reads of the BW-RAM
 * character window return SNES-tile-format data that the SA-1
 * converts on the fly from the linear bitmap in BW-RAM. Hardware
 * buffers one whole character into I-RAM on each character-boundary
 * read, then the individual bytes are read back out of that I-RAM
 * buffer. This mirrors ares' SA1::dmaCC1Read exactly (verified
 * bit-exact against it across all bpp/size/offset combinations).
 *
 * `bwoffset` is the BW-RAM-relative byte offset being read (same
 * address space as the DSA source register). Returns the converted
 * byte from the I-RAM buffer at FillRAM[0x3000].
 *
 * Registers:
 *   $2231: dmacb = bits0-1 (colour-depth code, clamped to 2),
 *          dmasize = bits2-4 (clamped to 5)
 *   $2232-4: DSA (BW-RAM source address)
 *   $2235-6: DDA (I-RAM destination address) */
uint8_t S9xSA1ReadCC1 (uint32_t bwoffset)
{
	uint32_t charmask, bpp, bpl, bwmask, tile, ty, tx, bwaddr, dsa, dda;
	int      dmacb, dmasize;
	uint32_t y, x, byte;

	dmacb   = SA1FillRAM[0x2231] & 3;
	if (dmacb > 2)
		dmacb = 2;
	dmasize = (SA1FillRAM[0x2231] >> 2) & 7;
	if (dmasize > 5)
		dmasize = 5;

	dsa = SA1FillRAM[0x2232] | (SA1FillRAM[0x2233] << 8) | (SA1FillRAM[0x2234] << 16);
	dda = SA1FillRAM[0x2235] | (SA1FillRAM[0x2236] << 8);

	charmask = (1u << (6 - dmacb)) - 1;

	if ((bwoffset & charmask) == 0)
	{
		bpp    = 2u << (2 - dmacb);
		bpl    = (8u << dmasize) >> dmacb;
		bwmask = SA1BWRAMMask;
		tile   = ((bwoffset - dsa) & bwmask) >> (6 - dmacb);
		ty     = tile >> dmasize;
		tx     = tile & ((1u << dmasize) - 1);
		bwaddr = dsa + ty * 8 * bpl + tx * bpp;

		for (y = 0; y < 8; y++)
		{
			uint64_t data = 0;
			uint8_t  out[8];

			for (byte = 0; byte < bpp; byte++)
				data |= (uint64_t) SA1BWRAMBase[(bwaddr + byte) & bwmask] << (byte << 3);

			bwaddr += bpl;

			memset(out, 0, sizeof(out));
			for (x = 0; x < 8; x++)
			{
				out[0] |= (data & 1) << (7 - x); data >>= 1;
				out[1] |= (data & 1) << (7 - x); data >>= 1;
				if (dmacb != 2)
				{
					out[2] |= (data & 1) << (7 - x); data >>= 1;
					out[3] |= (data & 1) << (7 - x); data >>= 1;
					if (dmacb != 1)
					{
						out[4] |= (data & 1) << (7 - x); data >>= 1;
						out[5] |= (data & 1) << (7 - x); data >>= 1;
						out[6] |= (data & 1) << (7 - x); data >>= 1;
						out[7] |= (data & 1) << (7 - x); data >>= 1;
					}
				}
			}

			for (byte = 0; byte < bpp; byte++)
			{
				uint32_t p = dda + (y << 1) + ((byte & 6) << 3) + (byte & 1);
				SA1FillRAM[0x3000 + (p & 0x7ff)] = out[byte];
			}
		}
	}

	return (SA1FillRAM[0x3000 + ((dda + (bwoffset & charmask)) & 0x7ff)]);
}
