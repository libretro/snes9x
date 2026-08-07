/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

/* A cursor over a byte buffer, which is all the Stream class hierarchy was
   ever used for here once file-backed savestates went away:

     - savestates are written to and read from a caller-supplied buffer
       (retro_serialize / retro_unserialize)
     - S9xFreezeSize measures a savestate by writing with no buffer at all
     - patches are read from a buffer, whether that came from a loose file,
       a zip entry or an .msu1 pack member

   Passing buf = NULL makes writes count bytes without storing them, which is
   the sizing pass. Reads past the end return short, and seeks are clamped, so
   a truncated or malformed input cannot walk off the buffer. */

#ifndef _BYTESTREAM_H_
#define _BYTESTREAM_H_

#include <stddef.h>
#include <string.h>
#include "port.h"

struct ByteStream
{
	uint8	*buf;    /* NULL to count bytes without writing them */
	size_t	 size;   /* capacity when writing, length when reading */
	size_t	 pos;
};

static inline void bs_init (struct ByteStream *s, void *buf, size_t size)
{
	s->buf  = (uint8 *) buf;
	s->size = size;
	s->pos  = 0;
}

static inline size_t bs_pos (const struct ByteStream *s)
{
	return (s->pos);
}

static inline void bs_seek (struct ByteStream *s, size_t pos)
{
	s->pos = (pos > s->size && s->buf) ? s->size : pos;
}

static inline size_t bs_read (struct ByteStream *s, void *dst, size_t len)
{
	size_t avail = (s->pos < s->size) ? (s->size - s->pos) : 0;

	if (len > avail)
		len = avail;

	if (len && s->buf)
		memcpy(dst, s->buf + s->pos, len);

	s->pos += len;
	return (len);
}

static inline int bs_get_char (struct ByteStream *s)
{
	uint8 c;
	if (bs_read(s, &c, 1) != 1)
		return (-1);
	return ((int) c);
}

static inline size_t bs_write (struct ByteStream *s, const void *src, size_t len)
{
	/* No buffer means this is the sizing pass: advance and count only. */
	if (s->buf)
	{
		size_t avail = (s->pos < s->size) ? (s->size - s->pos) : 0;

		if (len > avail)
			len = avail;

		if (len)
			memcpy(s->buf + s->pos, src, len);
	}

	s->pos += len;
	return (len);
}

#endif
