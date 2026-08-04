/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include "port.h"
#include "crosshairs.h"
#include <streams/file_stream_transforms.h>

static const char	*crosshairs[32] =
{
	"`              "  // Crosshair 0 (no image)
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               ",

	"`              "  // Crosshair 1 (the classic small dot)
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"       #.      "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               "
	"               ",

	"`              "  // Crosshair 2 (a standard cross)
	"               "
	"               "
	"               "
	"      .#.      "
	"      .#.      "
	"    ...#...    "
	"    #######    "
	"    ...#...    "
	"      .#.      "
	"      .#.      "
	"               "
	"               "
	"               "
	"               ",

	"`     .#.      "  // Crosshair 3 (a standard cross)
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"      .#.      "
	".......#......."
	"###############"
	".......#......."
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"      .#.      ",

	"`              "  // Crosshair 4 (an X)
	"               "
	"               "
	"    .     .    "
	"   .#.   .#.   "
	"    .#. .#.    "
	"     .#.#.     "
	"      .#.      "
	"     .#.#.     "
	"    .#. .#.    "
	"   .#.   .#.   "
	"    .     .    "
	"               "
	"               "
	"               ",

	"`.           . "  // Crosshair 5 (an X)
	".#.         .#."
	" .#.       .#. "
	"  .#.     .#.  "
	"   .#.   .#.   "
	"    .#. .#.    "
	"     .#.#.     "
	"      .#.      "
	"     .#.#.     "
	"    .#. .#.    "
	"   .#.   .#.   "
	"  .#.     .#.  "
	" .#.       .#. "
	".#.         .#."
	" .           . ",

	"`              "  // Crosshair 6 (a combo)
	"               "
	"               "
	"               "
	"    #  .  #    "
	"     # . #     "
	"      #.#      "
	"    ...#...    "
	"      #.#      "
	"     # . #     "
	"    #  .  #    "
	"               "
	"               "
	"               "
	"               ",

	"`      .       "  // Crosshair 7 (a combo)
	" #     .     # "
	"  #    .    #  "
	"   #   .   #   "
	"    #  .  #    "
	"     # . #     "
	"      #.#      "
	".......#......."
	"      #.#      "
	"     # . #     "
	"    #  .  #    "
	"   #   .   #   "
	"  #    .    #  "
	" #     .     # "
	"       .       ",

	"`      #       "  // Crosshair 8 (a diamond cross)
	"      #.#      "
	"     # . #     "
	"    #  .  #    "
	"   #   .   #   "
	"  #    .    #  "
	" #     .     # "
	"#......#......#"
	" #     .     # "
	"  #    .    #  "
	"   #   .   #   "
	"    #  .  #    "
	"     # . #     "
	"      #.#      "
	"       #       ",

	"`     ###      "  // Crosshair 9 (a circle cross)
	"    ## . ##    "
	"   #   .   #   "
	"  #    .    #  "
	" #     .     # "
	" #     .     # "
	"#      .      #"
	"#......#......#"
	"#      .      #"
	" #     .     # "
	" #     .     # "
	"  #    .    #  "
	"   #   .   #   "
	"    ## . ##    "
	"      ###      ",

	"`     .#.      "  // Crosshair 10 (a square cross)
	"      .#.      "
	"      .#.      "
	"   ....#....   "
	"   .#######.   "
	"   .#     #.   "
	"....#     #...."
	"#####     #####"
	"....#     #...."
	"   .#     #.   "
	"   .#######.   "
	"   ....#....   "
	"      .#.      "
	"      .#.      "
	"      .#.      ",

	"`     .#.      "  // Crosshair 11 (an interrupted cross)
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"               "
	".....     ....."
	"#####     #####"
	".....     ....."
	"               "
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"      .#.      "
	"      .#.      ",

	"`.           . "  // Crosshair 12 (an interrupted X)
	".#.         .#."
	" .#.       .#. "
	"  .#.     .#.  "
	"   .#.   .#.   "
	"               "
	"               "
	"               "
	"               "
	"               "
	"   .#.   .#.   "
	"  .#.     .#.  "
	" .#.       .#. "
	".#.         .#."
	" .           . ",

	"`      .       "  // Crosshair 13 (an interrupted combo)
	" #     .     # "
	"  #    .    #  "
	"   #   .   #   "
	"    #  .  #    "
	"               "
	"               "
	".....     ....."
	"               "
	"               "
	"    #  .  #    "
	"   #   .   #   "
	"  #    .    #  "
	" #     .     # "
	"       .       ",

	"`####     #### "  // Crosshair 14
	"#....     ....#"
	"#.           .#"
	"#.           .#"
	"#.           .#"
	"       #       "
	"       #       "
	"     #####     "
	"       #       "
	"       #       "
	"#.           .#"
	"#.           .#"
	"#.           .#"
	"#....     ....#"
	" ####     #### ",

	"`  .#     #.   "  // Crosshair 15
	"   .#     #.   "
	"   .#     #.   "
	"....#     #...."
	"#####     #####"
	"               "
	"               "
	"               "
	"               "
	"               "
	"#####     #####"
	"....#     #...."
	"   .#     #.   "
	"   .#     #.   "
	"   .#     #.   ",

	"`      #       "  // Crosshair 16
	"       #       "
	"       #       "
	"   ....#....   "
	"   .   #   .   "
	"   .   #   .   "
	"   .   #   .   "
	"###############"
	"   .   #   .   "
	"   .   #   .   "
	"   .   #   .   "
	"   ....#....   "
	"       #       "
	"       #       "
	"       #       ",

	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};


bool S9xLoadCrosshairFile (int idx, const char *filename)
{
	if (idx < 1 || idx > 31)
		return (false);

	char	*s = (char *) calloc(15 * 15 + 1, sizeof(char));
	if (s == NULL)
	{
		fprintf(stderr, "S9xLoadCrosshairFile: malloc error while reading ");
		perror(filename);
		return (false);
	}

	RFILE *fp = rfopen(filename, "rb");
	if (fp == NULL)
	{
		fprintf(stderr, "S9xLoadCrosshairFile: Couldn't open ");
		perror(filename);
		free(s);
		return (false);
	}

	size_t	l = rfread(s, 1, 8, fp);
	if (l != 8)
	{
		fprintf(stderr, "S9xLoadCrosshairFile: File is too short!\n");
		free(s);
		rfclose(fp);
		return (false);
	}

/* PNG crosshair support removed with the libretro VFS conversion: it
	   was dead in this build (HAVE_LIBPNG never defined) and png_init_io
	   requires stdio FILE I/O. Text-format crosshair files remain. */
	{
		l = rfread(s + 8, 1, 15 - 8, fp);
		if (l != 15 - 8)
		{
			fprintf(stderr, "S9xLoadCrosshairFile: File is too short!\n");
			free(s);
			rfclose(fp);
			return (false);
		}

		if (rfgetc(fp) != '\n')
		{
			fprintf(stderr, "S9xLoadCrosshairFile: Invalid file format! (note: PNG support is not available)\n");
			free(s);
			rfclose(fp);
			return (false);
		}

		for (int r = 1; r < 15; r++)
		{
			l = rfread(s + r * 15, 1, 15, fp);
			if (l != 15)
			{
				fprintf(stderr, "S9xLoadCrosshairFile: File is too short! (note: PNG support is not available)\n");
				free(s);
				rfclose(fp);
				return (false);
			}

			if (rfgetc(fp) != '\n')
			{
				fprintf(stderr, "S9xLoadCrosshairFile: Invalid file format! (note: PNG support is not available)\n");
				free(s);
				rfclose(fp);
				return (false);
			}
		}

		for (int i = 0; i < 15 * 15; i++)
		{
			if (s[i] != ' ' && s[i] != '#' && s[i] != '.')
			{
				fprintf(stderr, "S9xLoadCrosshairFile: Invalid file format! (note: PNG support is not available)\n");
				free(s);
				rfclose(fp);
				return (false);
			}
		}
	}

	rfclose(fp);

	if (crosshairs[idx] != NULL && crosshairs[idx][0] != '`')
		free((void *) crosshairs[idx]);
	crosshairs[idx] = s;

	return (true);
}

const char * S9xGetCrosshair (int idx)
{
	if (idx < 0 || idx > 31)
		return (NULL);

	return (crosshairs[idx]);
}
