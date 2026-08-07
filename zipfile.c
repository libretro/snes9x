/***********************************************************************************
  Zip archive reader. See zipfile.h for the shape of the API and the seek-cost
  notes; this file is the format handling.
 ***********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"
#include "zipfile.h"

#include <streams/file_stream.h>
#include <encodings/deflate.h>

#define ZIP_EOCD_SIG          0x06054b50
#define ZIP_CDIR_SIG          0x02014b50
#define ZIP_LFH_SIG           0x04034b50
#define ZIP_EOCD_MIN          22
#define ZIP_EOCD_MAX_COMMENT  0xffff

#define ZIP_IN_BUFSZ          16384
#define ZIP_SKIP_BUFSZ        16384

static uint32_t rd32 (const uint8_t *p)
{
	return ((uint32_t) p[0]) | ((uint32_t) p[1] << 8) |
	       ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint16_t rd16 (const uint8_t *p)
{
	return (uint16_t) (((uint16_t) p[0]) | ((uint16_t) p[1] << 8));
}

static char lower (char c)
{
	return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static int ci_equal (const char *a, const char *b)
{
	while (*a && *b)
	{
		if (lower(*a) != lower(*b))
			return (FALSE);
		a++; b++;
	}
	return (*a == '\0' && *b == '\0');
}

static int ends_with (const char *name, const char *suffix)
{
	size_t nl = strlen(name);
	size_t sl = strlen(suffix);
	size_t i;

	if (sl > nl)
		return (FALSE);

	for (i = 0; i < sl; i++)
		if (lower(name[nl - sl + i]) != lower(suffix[i]))
			return (FALSE);

	return (TRUE);
}

/* ---- central directory ---------------------------------------------------- */

static int find_eocd (RFILE *f, long fsize, uint32_t *cdir_off, uint16_t *entries)
{
	long     window = ZIP_EOCD_MIN + ZIP_EOCD_MAX_COMMENT;
	long     start;
	uint8_t *buf;
	long     got;
	long     i;
	int      found = FALSE;

	if (fsize < ZIP_EOCD_MIN)
		return (FALSE);

	if (window > fsize)
		window = fsize;

	buf = (uint8_t *) malloc((size_t) window);
	if (!buf)
		return (FALSE);

	start = fsize - window;
	filestream_seek(f, start, RETRO_VFS_SEEK_POSITION_START);
	got = (long) filestream_read(f, buf, (int64_t) window);
	if (got < ZIP_EOCD_MIN)
	{
		free(buf);
		return (FALSE);
	}

	for (i = got - ZIP_EOCD_MIN; i >= 0; i--)
	{
		if (rd32(buf + i) == ZIP_EOCD_SIG)
		{
			*entries  = rd16(buf + i + 10);
			*cdir_off = rd32(buf + i + 16);
			found = TRUE;
			break;
		}
	}

	free(buf);
	return (found);
}

int zip_archive_open (struct zip_archive *ar, const char *path)
{
	long     fsize;
	uint32_t cdir_off = 0;
	uint16_t entries  = 0;
	uint16_t n;

	memset(ar, 0, sizeof(*ar));

	ar->file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
	                           RETRO_VFS_FILE_ACCESS_HINT_NONE);
	if (!ar->file)
		return (FALSE);

	filestream_seek(ar->file, 0, RETRO_VFS_SEEK_POSITION_END);
	fsize = (long) filestream_tell(ar->file);

	if (!find_eocd(ar->file, fsize, &cdir_off, &entries) || (long) cdir_off >= fsize)
	{
		zip_archive_close(ar);
		return (FALSE);
	}

	if (entries == 0)
		return (TRUE);

	ar->entries = (struct zip_entry *) calloc(entries, sizeof(struct zip_entry));
	if (!ar->entries)
	{
		zip_archive_close(ar);
		return (FALSE);
	}

	filestream_seek(ar->file, (long) cdir_off, RETRO_VFS_SEEK_POSITION_START);

	for (n = 0; n < entries; n++)
	{
		uint8_t  hdr[46];
		uint8_t  lfh[30];
		uint16_t name_len, extra_len, comment_len;
		uint32_t lfh_off;
		long     next;
		struct zip_entry *e = &ar->entries[ar->count];

		if (filestream_read(ar->file, hdr, (int64_t) sizeof(hdr)) != (int64_t) sizeof(hdr))
			break;
		if (rd32(hdr) != ZIP_CDIR_SIG)
			break;

		e->method      = rd16(hdr + 10);
		e->comp_size   = rd32(hdr + 20);
		e->uncomp_size = rd32(hdr + 24);
		name_len       = rd16(hdr + 28);
		extra_len      = rd16(hdr + 30);
		comment_len    = rd16(hdr + 32);
		lfh_off        = rd32(hdr + 42);

		next = (long) filestream_tell(ar->file) + name_len + extra_len + comment_len;

		if (name_len < ZIP_MAX_NAME)
		{
			if (filestream_read(ar->file, e->name, (int64_t) name_len) != (int64_t) name_len)
				break;
			e->name[name_len] = '\0';

			/* The local header repeats the name and extra lengths, and its
			   extra field may differ from the central one, so the data
			   offset has to come from there rather than from the directory. */
			filestream_seek(ar->file, (long) lfh_off, RETRO_VFS_SEEK_POSITION_START);
			if (filestream_read(ar->file, lfh, (int64_t) sizeof(lfh)) == (int64_t) sizeof(lfh) &&
			    rd32(lfh) == ZIP_LFH_SIG)
			{
				e->data_off = lfh_off + (uint32_t) sizeof(lfh)
				            + rd16(lfh + 26) + rd16(lfh + 28);
				ar->count++;
			}
		}

		filestream_seek(ar->file, next, RETRO_VFS_SEEK_POSITION_START);
	}

	return (TRUE);
}

void zip_archive_close (struct zip_archive *ar)
{
	if (ar->file)
	{
		filestream_close(ar->file);
		ar->file = NULL;
	}
	if (ar->entries)
	{
		free(ar->entries);
		ar->entries = NULL;
	}
	ar->count = 0;
}

/* ---- lookup --------------------------------------------------------------- */

/* Lookup skips entries this reader cannot decode, so a match is always
   readable. Without this a pack whose first name match happens to use, say,
   lzma would shadow a perfectly good deflated copy later in the directory:
   the caller gets a hit, zip_file_open then refuses it, and it has no way to
   know a usable entry existed. */
static int readable (const struct zip_entry *e)
{
	return (e->method == ZIP_METHOD_STORE || e->method == ZIP_METHOD_DEFLATE);
}

int zip_find_name (const struct zip_archive *ar, const char *name, int from)
{
	int i;
	for (i = (from < 0) ? 0 : from; i < ar->count; i++)
		if (readable(&ar->entries[i]) && ci_equal(ar->entries[i].name, name))
			return (i);
	return (-1);
}

int zip_find_ext (const struct zip_archive *ar, const char *ext, int from)
{
	int i;
	for (i = (from < 0) ? 0 : from; i < ar->count; i++)
	{
		const char *dot = strrchr(ar->entries[i].name, '.');
		if (readable(&ar->entries[i]) && dot && ci_equal(dot + 1, ext))
			return (i);
	}
	return (-1);
}

int zip_find_suffix (const struct zip_archive *ar, const char *suffix, int from)
{
	int i;
	for (i = (from < 0) ? 0 : from; i < ar->count; i++)
		if (readable(&ar->entries[i]) && ends_with(ar->entries[i].name, suffix))
			return (i);
	return (-1);
}

/* ---- entry reading -------------------------------------------------------- */

static void inflate_reset (struct zip_file *zf)
{
	if (zf->inf)
	{
		rinflate_free(zf->inf);
		zf->inf = NULL;
	}
	zf->out_pos = 0;
	zf->in_left = zf->entry.comp_size;
	zf->in_have = 0;
	zf->in_pos  = 0;
	zf->eof     = FALSE;
}

static int inflate_start (struct zip_file *zf)
{
	inflate_reset(zf);

	/* Raw deflate: zip entries carry no zlib header, hence negative window
	   bits in the zlib convention rinflate follows. */
	zf->inf = rinflate_new(-15);
	if (!zf->inf)
		return (FALSE);

	filestream_seek(zf->file, (long) zf->entry.data_off, RETRO_VFS_SEEK_POSITION_START);
	return (TRUE);
}

static uint32_t inflate_forward (struct zip_file *zf, uint8_t *out, uint32_t len)
{
	uint32_t done = 0;

	if (!zf->inf && !inflate_start(zf))
		return (0);

	while (done < len && !zf->eof)
	{
		size_t   read = 0, wrote = 0;
		uint8_t  scratch[ZIP_SKIP_BUFSZ];
		uint8_t *dst  = out ? (out + done) : scratch;
		uint32_t want = len - done;
		int      ret;

		if (!out && want > ZIP_SKIP_BUFSZ)
			want = ZIP_SKIP_BUFSZ;

		/* Refill only while the entry still has compressed bytes. Running out
		   of input is not end of stream: rinflate can hold decoded bytes that
		   need no further input, so process() must still be called with an
		   empty input buffer and allowed to flush. */
		if (zf->in_pos >= zf->in_have && zf->in_left > 0)
		{
			uint32_t chunk = zf->in_left;
			if (chunk > ZIP_IN_BUFSZ)
				chunk = ZIP_IN_BUFSZ;
			zf->in_have = (uint32_t) filestream_read(zf->file, zf->in_buf, (int64_t) chunk);
			zf->in_pos  = 0;
			if (zf->in_have == 0)
			{
				/* Directory promised more data than the file holds. */
				zf->eof = TRUE;
				break;
			}
			zf->in_left -= zf->in_have;
		}

		rinflate_set_in(zf->inf, zf->in_buf + zf->in_pos, zf->in_have - zf->in_pos);
		rinflate_set_out(zf->inf, dst, want);

		ret = rinflate_process(zf->inf, &read, &wrote);

		zf->in_pos  += (uint32_t) read;
		done        += (uint32_t) wrote;
		zf->out_pos += (uint32_t) wrote;

		if (ret == RDEFLATE_PROCESS_ERROR || ret == RDEFLATE_PROCESS_END)
		{
			zf->eof = TRUE;
			break;
		}
		if (read == 0 && wrote == 0)
		{
			if (zf->in_left == 0 && zf->in_pos >= zf->in_have)
				zf->eof = TRUE;
			break;
		}
	}

	return (done);
}

int zip_file_open (struct zip_file *zf, struct zip_archive *ar, int index)
{
	memset(zf, 0, sizeof(*zf));

	if (!ar->file || index < 0 || index >= ar->count)
		return (FALSE);

	zf->entry = ar->entries[index];

	if (zf->entry.method != ZIP_METHOD_STORE && zf->entry.method != ZIP_METHOD_DEFLATE)
		return (FALSE);

	zf->file    = ar->file;
	zf->in_left = zf->entry.comp_size;
	return (TRUE);
}

void zip_file_close (struct zip_file *zf)
{
	if (zf->inf)
	{
		rinflate_free(zf->inf);
		zf->inf = NULL;
	}
	zf->file = NULL;
}

uint32_t zip_file_size (const struct zip_file *zf)
{
	return (zf->entry.uncomp_size);
}

uint32_t zip_file_read (struct zip_file *zf, uint32_t offset, uint8_t *out, uint32_t len)
{
	if (!zf->file)
		return (0);

	if (offset >= zf->entry.uncomp_size)
		return (0);

	if (len > zf->entry.uncomp_size - offset)
		len = zf->entry.uncomp_size - offset;

	if (zf->entry.method == ZIP_METHOD_STORE)
	{
		filestream_seek(zf->file, (long) (zf->entry.data_off + offset),
		                RETRO_VFS_SEEK_POSITION_START);
		return ((uint32_t) filestream_read(zf->file, out, (int64_t) len));
	}

	/* Deflated: forward-only. A backward seek restarts the stream. */
	if (!zf->inf || offset < zf->out_pos)
	{
		if (!inflate_start(zf))
			return (0);
	}

	while (zf->out_pos < offset && !zf->eof)
	{
		uint32_t skip = offset - zf->out_pos;
		if (inflate_forward(zf, NULL, skip) == 0)
			break;
	}

	if (zf->out_pos != offset)
		return (0);

	return (inflate_forward(zf, out, len));
}

uint8_t * zip_read_entry (struct zip_archive *ar, int index, uint32_t *len)
{
	struct zip_file zf;
	uint8_t  *buf;
	uint32_t  size, got;

	if (!zip_file_open(&zf, ar, index))
		return (NULL);

	size = zip_file_size(&zf);
	buf  = (uint8_t *) malloc(size ? size : 1);
	if (!buf)
	{
		zip_file_close(&zf);
		return (NULL);
	}

	got = size ? zip_file_read(&zf, 0, buf, size) : 0;
	zip_file_close(&zf);

	if (got != size)
	{
		free(buf);
		return (NULL);
	}

	if (len)
		*len = size;
	return (buf);
}
