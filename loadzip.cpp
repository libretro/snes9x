/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include "snes9x.h"
#include "zipfile.h"
#include "memmap.h"

/* Selection rules, unchanged from the minizip implementation: prefer a file
   named program.rom or one whose extension is ".1", otherwise take the largest
   entry that fits in a cart. A .msu1 pack only ever loads program.rom, since
   its other entries are MSU1 companion data rather than the ROM. */
static int find_rom_entry (const struct zip_archive *ar, uint32 *out_size)
{
	int      best = -1;
	uint32   best_size = 0;
	int      i;

	for (i = 0; i < ar->count; i++)
	{
		const char *name = ar->entries[i].name;
		uint32      size = ar->entries[i].uncomp_size;
		int         len  = (int) strlen(name);

		if (size > CMemory::MAX_ROM_SIZE + 512)
			continue;

		/* Skip entries this reader cannot decode rather than selecting one
		   and failing the load. */
		if (ar->entries[i].method != ZIP_METHOD_STORE &&
		    ar->entries[i].method != ZIP_METHOD_DEFLATE)
			continue;

		if (size > best_size)
		{
			best = i;
			best_size = size;
		}

		if (len > 2 && name[len - 2] == '.' && name[len - 1] == '1')
		{
			best = i;
			best_size = size;
			break;
		}

		if (strncasecmp(name, "program.rom", 11) == 0)
		{
			best = i;
			best_size = size;
			break;
		}
	}

	*out_size = best_size;
	return (best);
}

bool8 LoadZip (const char *zipname, uint32 *TotalFileSize, uint8 *buffer)
{
	struct zip_archive	ar;
	char			filename[ZIP_MAX_NAME];
	uint32			filesize = 0;
	int			idx;
	int			len;

	*TotalFileSize = 0;

	if (!zip_archive_open(&ar, zipname))
		return (FALSE);

	idx = find_rom_entry(&ar, &filesize);
	if (idx < 0 || filesize == 0)
	{
		zip_archive_close(&ar);
		return (FALSE);
	}

	strcpy(filename, ar.entries[idx].name);

	len = (int) strlen(zipname);
	if (len > 5 && strcasecmp(zipname + len - 5, ".msu1") == 0 &&
	    strcasecmp(filename, "program.rom") != 0)
	{
		zip_archive_close(&ar);
		return (FALSE);
	}

	// find extension
	char	tmp[2] = { 0, 0 };
	char	*ext = strrchr(filename, '.');
	if (ext)
		ext++;
	else
		ext = tmp;

	uint8	*ptr = buffer;
	bool8	more = FALSE;

	do
	{
		uint32   FileSize = ar.entries[idx].uncomp_size;
		uint32   got;

		assert(FileSize <= CMemory::MAX_ROM_SIZE + 512);

		{
			struct zip_file zf;

			if (!zip_file_open(&zf, &ar, idx))
			{
				zip_archive_close(&ar);
				return (FALSE);
			}

			got = zip_file_read(&zf, 0, ptr, FileSize);
			zip_file_close(&zf);
		}

		if (got != FileSize)
		{
			zip_archive_close(&ar);
			return (FALSE);
		}

		FileSize = Memory.HeaderRemove(FileSize, ptr);
		ptr += FileSize;
		*TotalFileSize += FileSize;

		if (ptr - Memory.ROM < CMemory::MAX_ROM_SIZE + 512 && (isdigit(ext[0]) && ext[1] == 0 && ext[0] < '9'))
		{
			more = TRUE;
			ext[0]++;
		}
		else
		if (ptr - Memory.ROM < CMemory::MAX_ROM_SIZE + 512)
		{
			int	nlen;

			if (ext == tmp)
				nlen = (int) strlen(filename);
			else
				nlen = (int) (ext - filename - 1);

			if ((nlen == 7 || nlen == 8) && strncasecmp(filename, "sf", 2) == 0 &&
				isdigit(filename[2]) && isdigit(filename[3]) && isdigit(filename[4]) &&
				isdigit(filename[5]) && isalpha(filename[nlen - 1]))
			{
				more = TRUE;
				filename[nlen - 1]++;
			}
		}
		else
			more = FALSE;

		if (more)
		{
			idx = zip_find_name(&ar, filename, 0);
			if (idx < 0)
				break;
		}
	} while (more);

	zip_archive_close(&ar);

	return (TRUE);
}
