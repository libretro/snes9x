/***********************************************************************************
  Zip archive reader.

  Replaces the vendored minizip (unzip/) and its zlib dependency. Reads the
  central directory itself; the only decompression dependency is rinflate,
  libretro-common's cleanroom RFC 1951 decoder, which this core already builds
  for the MSU1 pack path.

  Two layers:

    struct zip_archive  - the directory. Open once, enumerate entries by index
                          or look one up by name / extension / suffix.
    struct zip_file     - one mounted entry, read by offset.

  Random access. Stored entries (method 0) seek for free: they are a plain byte
  range of the archive. Deflated entries (method 8) cannot be seeked, so a
  backward seek restarts the inflate at the entry's first byte and skips
  forward - the same cost minizip paid in unzGoToFilePos + skip, and
  forward-only reads never trigger it.

  Entries using any other compression method are reported but cannot be read;
  callers should skip them.
 ***********************************************************************************/

#ifndef _ZIPFILE_H_
#define _ZIPFILE_H_

#include <stdint.h>
#include <streams/file_stream.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZIP_METHOD_STORE      0
#define ZIP_METHOD_DEFLATE    8

#define ZIP_MAX_NAME          256

struct zip_entry
{
	char		name[ZIP_MAX_NAME];
	uint16_t	method;
	uint32_t	comp_size;
	uint32_t	uncomp_size;
	uint32_t	data_off;     /* offset of the entry's data in the archive */
};

struct zip_archive
{
	RFILE			*file;
	struct zip_entry	*entries;
	int			 count;
};

struct zip_file
{
	RFILE			*file;      /* borrowed from the archive */
	struct zip_entry	 entry;

	/* Streaming inflate state, only used for method 8. */
	void		*inf;
	uint32_t	 out_pos;     /* uncompressed bytes produced so far */
	uint32_t	 in_left;     /* compressed bytes not yet fed */
	uint32_t	 in_have;     /* valid bytes in in_buf */
	uint32_t	 in_pos;      /* consumed bytes in in_buf */
	uint8_t		 eof;
	uint8_t		 in_buf[16384];
};

/* Open an archive and read its central directory. Returns FALSE if the file is
   missing or is not a zip. */
int  zip_archive_open  (struct zip_archive *ar, const char *path);
void zip_archive_close (struct zip_archive *ar);

/* Entry lookup. Each returns the entry index, or -1. `from` lets a caller
   resume a scan past a previous hit; pass 0 to start at the beginning.
     _name    exact match, case-insensitive
     _ext     the part after the final '.' matches, case-insensitive
     _suffix  the name ends with this, case-insensitive */
int  zip_find_name   (const struct zip_archive *ar, const char *name, int from);
int  zip_find_ext    (const struct zip_archive *ar, const char *ext, int from);
int  zip_find_suffix (const struct zip_archive *ar, const char *suffix, int from);

/* Mount an entry for reading. The archive must outlive the zip_file. */
int      zip_file_open  (struct zip_file *zf, struct zip_archive *ar, int index);
void     zip_file_close (struct zip_file *zf);
uint32_t zip_file_size  (const struct zip_file *zf);

/* Read `len` bytes at `offset` within the mounted entry; returns bytes read. */
uint32_t zip_file_read  (struct zip_file *zf, uint32_t offset, uint8_t *out, uint32_t len);

/* Convenience: mount `index`, read the whole entry into a malloc'd buffer and
   unmount. Returns the buffer (caller frees) and writes its length to *len, or
   NULL on failure. */
uint8_t *zip_read_entry (struct zip_archive *ar, int index, uint32_t *len);

#ifdef __cplusplus
}
#endif

#endif
