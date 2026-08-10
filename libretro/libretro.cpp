#include "libretro.h"
#include <streams/file_stream.h>
#include <vfs/vfs_hybrid.h>
#include <file/file_path.h>
#include "libretro_core_options.h"

#include "snes9x.h"
extern "C" { extern uint8 TileMode7Hires; extern uint8 TileMode7HiresBilinear; }
#include "fxemu.h"
#include "memmap.h"
#include "srtc.h"
#include "apu/apu.h"
#include "apu/bapu/snes/snes.hpp"
#include "gfx.h"
#include "snapshot.h"
#include "controls.h"
#include "cheats.h"
#include "display.h"
#include "crosshairs.h"
#include <stdio.h>
#include <vector>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "filter/snes_ntsc.h"

static void reset_button_cache(void);

#define RETRO_DEVICE_JOYPAD_MULTITAP ((1 << 8) | RETRO_DEVICE_JOYPAD)
#define RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE ((1 << 8) | RETRO_DEVICE_LIGHTGUN)
#define RETRO_DEVICE_LIGHTGUN_JUSTIFIER ((2 << 8) | RETRO_DEVICE_LIGHTGUN)
#define RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2 ((3 << 8) | RETRO_DEVICE_LIGHTGUN)
#define RETRO_DEVICE_LIGHTGUN_MACS_RIFLE ((4 << 8) | RETRO_DEVICE_LIGHTGUN)

static int g_screen_gun_width = SNES_WIDTH;
static int g_screen_gun_height = SNES_HEIGHT;

#define RETRO_MEMORY_SNES_BSX_RAM ((1 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_SNES_BSX_PRAM ((2 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_SNES_SUFAMI_TURBO_A_RAM ((3 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_SNES_SUFAMI_TURBO_B_RAM ((4 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_SNES_GAME_BOY_RAM ((5 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_SNES_GAME_BOY_RTC ((6 << 8) | RETRO_MEMORY_RTC)

#define RETRO_GAME_TYPE_BSX             0x101 | 0x1000
#define RETRO_GAME_TYPE_BSX_SLOTTED     0x102 | 0x1000
#define RETRO_GAME_TYPE_SUFAMI_TURBO    0x103 | 0x1000
#define RETRO_GAME_TYPE_SUPER_GAME_BOY  0x104 | 0x1000
#define RETRO_GAME_TYPE_MULTI_CART      0x105 | 0x1000


#define SNES_4_3 4.0f / 3.0f

uint16 *screen_buffer = NULL;

char g_rom_dir[1024];
char g_basename[1024];

bool g_geometry_update = false;

int hires_blend = 0;
bool randomize_memory = false;
int disabled_channels = 0;

char retro_system_directory[4096];
char retro_save_directory[4096];

retro_log_printf_t log_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_t audio_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;

static bool libretro_supports_option_categories = false;
static bool libretro_supports_bitmasks = false;

static snes_ntsc_t *snes_ntsc = NULL;
static int blargg_filter = 0;
static uint16 *ntsc_screen_buffer, *snes_ntsc_buffer;

const int MAX_SNES_WIDTH_NTSC = ((SNES_NTSC_OUT_WIDTH(256) + 3) / 4) * 4;

static bool show_lightgun_settings = true;
static bool show_advanced_av_settings = true;
/* snes9x_msu1_enhanced_audio core option; latched into the playback rate at
   content load by msu1_update_playback_rate(). */
static bool msu1_enhanced_pref = true;

static void extract_basename(char *buf, const char *path, size_t size)
{
    const char *base = strrchr(path, '/');
    if (!base)
        base = strrchr(path, '\\');
    if (!base)
        base = path;

    if (*base == '\\' || *base == '/')
        base++;

    strncpy(buf, base, size - 1);
    buf[size - 1] = '\0';

    char *ext = strrchr(buf, '.');
    if (ext)
        *ext = '\0';
}

static void extract_directory(char *buf, const char *path, size_t size)
{
    strncpy(buf, path, size - 1);
    buf[size - 1] = '\0';

    char *base = strrchr(buf, '/');
    if (!base)
        base = strrchr(buf, '\\');

    if (base)
        *base = '\0';
    else
        buf[0] = '\0';
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

enum overscan_mode {
    OVERSCAN_CROP_ON,
    OVERSCAN_CROP_OFF,
    OVERSCAN_CROP_12,
    OVERSCAN_CROP_16,
    OVERSCAN_CROP_AUTO
};
enum aspect_mode {
    ASPECT_RATIO_4_3,
    ASPECT_RATIO_4_3_SCALED,
    ASPECT_RATIO_1_1,
    ASPECT_RATIO_NTSC,
    ASPECT_RATIO_PAL,
    ASPECT_RATIO_AUTO
};
static retro_environment_t environ_cb;
static overscan_mode crop_overscan_mode = OVERSCAN_CROP_ON; // default to crop
static aspect_mode aspect_ratio_mode = ASPECT_RATIO_4_3; // default to 4:3
static bool rom_loaded = false;

enum lightgun_mode
{
	SETTING_GUN_INPUT_LIGHTGUN,
	SETTING_GUN_INPUT_POINTER
};
static lightgun_mode setting_gun_input = SETTING_GUN_INPUT_LIGHTGUN;

// Touchscreen sensitivity vars
static int pointer_pressed = 0;
static const int POINTER_PRESSED_CYCLES = 4;
static int pointer_cycles_after_released = 0;
static int pointer_pressed_last_x = 0;
static int pointer_pressed_last_y = 0;

static bool setting_superscope_reverse_buttons = false;

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

#ifndef STATIC_LINKING
    /*
       Hybrid VFS replaces the wholesale v1 adoption. MSU-1 makes this
       core an unusually direct beneficiary: PCM sidecars stream
       continuously during gameplay, and every one of those reads paid
       the frontend-callback indirection on every platform. Plain
       paths now serve through the local implementation directly;
       the frontend is consulted only for URI-shaped paths or after a
       local failure on sandboxed platforms, so Android keeps exactly
       the reachability it had - plus dirent coverage the old v1-only
       wiring never provided. log_cb is unset this early; the hybrid
       treats NULL as no-log.
    */
    vfs_hybrid_init(cb, NULL);
#else
    /*
       Statically linked frontends share one libretro-common with the
       core: the local implementation and the "frontend VFS" are the
       same code, so the hybrid is semantically void there and its
       object is excluded from the archive (see Makefile.common).
    */
#endif

    static const struct retro_subsystem_memory_info multi_a_memory[] = {
        { "srm", RETRO_MEMORY_SNES_SUFAMI_TURBO_A_RAM },
    };

    static const struct retro_subsystem_memory_info multi_b_memory[] = {
        { "srm", RETRO_MEMORY_SNES_SUFAMI_TURBO_B_RAM },
    };

    static const struct retro_subsystem_rom_info multicart_roms[] = {
        { "Cart A", "smc|sfc|swc|fig|bs|st", false, false, false, multi_a_memory, 1 },
        { "Cart B", "smc|sfc|swc|fig|bs|st", false, false, false, multi_b_memory, 1 },
    };

    static const struct retro_subsystem_info subsystems[] = {
        { "Multi-Cart Link", "multicart_addon", multicart_roms, 2, RETRO_GAME_TYPE_MULTI_CART },
        {}
    };

    cb(RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO,  (void*)subsystems);

    /* An annoyance: retro_set_environment() can be called
     * multiple times, and depending upon the current frontend
     * state various environment callbacks may be disabled.
     * This means the reported 'categories_supported' status
     * may change on subsequent iterations. We therefore have
     * to record whether 'categories_supported' is true on any
     * iteration, and latch the result */
    bool option_categories = false;
    libretro_set_core_options(environ_cb, &option_categories);
    libretro_supports_option_categories |= option_categories;

    /* If frontend supports core option categories,
     * show/hide toggle options are unused and should
     * themselves be hidden */
    if (libretro_supports_option_categories)
    {
        struct retro_core_option_display option_display;

        option_display.visible = false;
        option_display.key     = "snes9x_show_lightgun_settings";

        environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                &option_display);

        option_display.key     = "snes9x_show_advanced_av_settings";

        environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,
                &option_display);
    }

    static const struct retro_controller_description port_1[] = {
        { "None", RETRO_DEVICE_NONE },
        { "SNES Joypad", RETRO_DEVICE_JOYPAD },
        { "SNES Mouse", RETRO_DEVICE_MOUSE },
        { "Multitap", RETRO_DEVICE_JOYPAD_MULTITAP },
    };

    static const struct retro_controller_description port_2[] = {
        { "None", RETRO_DEVICE_NONE },
        { "SNES Joypad", RETRO_DEVICE_JOYPAD },
        { "SNES Mouse", RETRO_DEVICE_MOUSE },
        { "Multitap", RETRO_DEVICE_JOYPAD_MULTITAP },
        { "SuperScope", RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE },
        { "Justifier", RETRO_DEVICE_LIGHTGUN_JUSTIFIER },
        { "M.A.C.S. Rifle", RETRO_DEVICE_LIGHTGUN_MACS_RIFLE },
    };

    static const struct retro_controller_description port_3[] = {
        { "None", RETRO_DEVICE_NONE },
        { "SNES Joypad", RETRO_DEVICE_JOYPAD },
        { "Justifier (2P)", RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2 },
    };

    static const struct retro_controller_description port_extra[] = {
        { "None", RETRO_DEVICE_NONE },
        { "SNES Joypad", RETRO_DEVICE_JOYPAD },
    };

    static const struct retro_controller_info ports[] = {
        { port_1, 4 },
        { port_2, 7 },
        { port_3, 3 },
        { port_extra, 2 },
        { port_extra, 2 },
        { port_extra, 2 },
        { port_extra, 2 },
        { port_extra, 2 },
        {},
    };

    environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

char *get_cursor_color(const char *name)
{
	static char color_names[][32] = {
		"Black", "Black",
		"Black (blend)", "tBlack",
		"25% Grey", "25Grey",
		"25% Grey (blend)", "t25Grey",
		"50% Grey", "50Grey",
		"50% Grey (blend)", "t50Grey",
		"75% Grey", "75Grey",
		"75% Grey (blend)", "t75Grey",
		"White", "White",
		"White (blend)", "tWhite",
		"Red", "Red",
		"Red (blend)", "tRed",
		"Orange", "Orange",
		"Orange (blend)", "tOrange",
		"Yellow", "Yellow",
		"Yellow (blend)", "tYellow",
		"Green", "Green",
		"Green (blend)", "tGreen",
		"Cyan", "Cyan",
		"Cyan (blend)", "tCyan",
		"Sky", "Sky",
		"Sky (blend)", "tSky",
		"Blue", "Blue",
		"Blue (blend)", "tBlue",
		"Violet", "Violet",
		"Violet (blend)", "tViolet",
		"Pink", "MagicPink",
		"Pink (blend)", "tMagicPink",
		"Purple", "Purple",
		"Purple (blend)", "tPurple",
		"\0", "\0"
	};

	int lcv = 0;
	while (color_names[lcv][0]) {
		if (strcmp(color_names[lcv], name) == 0) {
			return color_names[lcv+1];
		}

		lcv += 2;
	}

	return color_names[16]; // White
}

// always ensure this is only called in retro_run
void update_geometry(void)
{
    struct retro_system_av_info av_info;
    retro_get_system_av_info(&av_info);
    environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av_info);
    g_screen_gun_width = av_info.geometry.base_width;
    g_screen_gun_height = av_info.geometry.base_height;
    g_geometry_update = false;
}

static void update_variables(void)
{
    char key[256];
    struct retro_variable var;

    var.key = "snes9x_msu1_enhanced_audio";
    var.value = NULL;

    msu1_enhanced_pref = true;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        msu1_enhanced_pref = !strcmp(var.value, "enabled");

    var.key = "snes9x_mode7_hires";
    var.value = NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int prev = Settings.Mode7Hires;
        if (strcmp(var.value, "4x_hv") == 0)
        {
            Settings.Mode7Hires = 4;
            Settings.Mode7HiresVertical = 1;
        }
        else if (strcmp(var.value, "2x_hv") == 0)
        {
            Settings.Mode7Hires = 2;
            Settings.Mode7HiresVertical = 1;
        }
        else if (strcmp(var.value, "4x") == 0)
        {
            Settings.Mode7Hires = 4;
            Settings.Mode7HiresVertical = 0;
        }
        else if (strcmp(var.value, "2x") == 0)
        {
            Settings.Mode7Hires = 2;
            Settings.Mode7HiresVertical = 0;
        }
        else
        {
            Settings.Mode7Hires = 0;
            Settings.Mode7HiresVertical = 0;
        }
        TileMode7Hires = (uint8) Settings.Mode7Hires;
        /* Live option switch: refresh frontend geometry (max/base sizes,
           aspect) the same way the aspect option does. */
        if (prev != Settings.Mode7Hires)
            g_geometry_update = true;
    }

    var.key = "snes9x_mode7_hires_bilinear";
    var.value = NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "smooth") == 0)
            Settings.Mode7HiresBilinear = 2;
        else if (strcmp(var.value, "stable") == 0)
            Settings.Mode7HiresBilinear = 1;
        else
            Settings.Mode7HiresBilinear = 0;
        TileMode7HiresBilinear = (uint8) Settings.Mode7HiresBilinear;
    }

    var.key = "snes9x_hires_blend";
    var.value = NULL;

    hires_blend = 0;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        if (!strcmp (var.value, "blur"))
            hires_blend = 1;
        else if (!strcmp (var.value, "merge"))
            hires_blend = 2;
    }

    var.key = "snes9x_overclock_superfx";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        int freq = atoi(var.value);
        Settings.SuperFXClockMultiplier = freq;
        /* 6255000 is snes9x2010's default per-line budget (625500 per MHz at
           its "10 MHz" default); the GSU executor and its per-opcode cycle
           costs are snes9x2010's, so 100% here must mean the same speed the
           reference core ships with (libretro/snes9x#314). */
        SuperFXSpeedPerLineHz = (uint32) ((uint64) 6255000 * freq / 100);
        SuperFXHwTimingPct = freq;
        S9xSuperFXRecomputeSpeedPerLine();
    }

    var.key = "snes9x_superfx_timing";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
        fx_hw_timing = !strcmp(var.value, "hardware") ? 1 : 0;

    var.key = "snes9x_up_down_allowed";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        Settings.UpAndDown = !strcmp(var.value, "disabled") ? false : true;
    }
    else
        Settings.UpAndDown = false;
    
    strcpy(key, "snes9x_sndchan_volume_x");
    var.key=key;
    for (int i=0;i<8;i++)
    {
        key[strlen("snes9x_sndchan_volume_")]='1'+i;
        var.value=NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
        {
            Settings.ChannelsVolumePercent[i] = atoi(var.value);
        }
    }
    
    int disabled_layers=0;
    strcpy(key, "snes9x_layer_x");
    for (int i=0;i<5;i++)
    {
        key[strlen("snes9x_layer_")]='1'+i;
        var.value=NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && !strcmp("disabled", var.value))
            disabled_layers|=1<<i;
    }
    Settings.BG_Forced=disabled_layers;

    //for some reason, Transparency seems to control both the fixed color and the windowing registers?
    var.key="snes9x_gfx_transp";
    var.value=NULL;
    Settings.Transparency=!(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && !strcmp("disabled", var.value));

    var.key="snes9x_audio_interpolation";
    var.value=NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "gaussian") == 0)
            Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;
        else if (strcmp(var.value, "linear") == 0)
            Settings.InterpolationMethod = DSP_INTERPOLATION_LINEAR;
        else if (strcmp(var.value, "cubic") == 0)
            Settings.InterpolationMethod = DSP_INTERPOLATION_CUBIC;
        else if (strcmp(var.value, "sinc") == 0)
            Settings.InterpolationMethod = DSP_INTERPOLATION_SINC;
        else if (strcmp(var.value, "none") == 0)
            Settings.InterpolationMethod = DSP_INTERPOLATION_NONE;
    }
    else
        Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;


    Settings.OneClockCycle      = 6;
    Settings.OneSlowClockCycle  = 8;
    Settings.TwoClockCycles     = 12;

    var.key="snes9x_overclock_cycles";
    var.value=NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "max") == 0)
        {
            Settings.OneClockCycle      = 3;
            Settings.OneSlowClockCycle  = 3;
            Settings.TwoClockCycles     = 3;
        }
        else if (strcmp(var.value, "compatible") == 0)
        {
            Settings.OneClockCycle      = 4;
            Settings.OneSlowClockCycle  = 5;
            Settings.TwoClockCycles     = 6;
        }
        else if (strcmp(var.value, "light") == 0)
        {
            Settings.OneClockCycle      = 6;
            Settings.OneSlowClockCycle  = 6;
            Settings.TwoClockCycles     = 12;
        }
    }

    Settings.MaxSpriteTilesPerLine = 34;
    var.key="snes9x_reduce_sprite_flicker";
    var.value=NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        if (strcmp(var.value, "enabled") == 0)
            Settings.MaxSpriteTilesPerLine = 128;

    randomize_memory = false;
    var.key = "snes9x_randomize_memory";
    var.value = NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        if (strcmp(var.value, "enabled") == 0)
            randomize_memory = true;

    var.key = "snes9x_overscan";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        overscan_mode newval = OVERSCAN_CROP_AUTO;
        if (strcmp(var.value, "enabled") == 0)
            newval = OVERSCAN_CROP_ON;
        else if (strcmp(var.value, "12_pixels") == 0)
            newval = OVERSCAN_CROP_12;
        else if (strcmp(var.value, "16_pixels") == 0)
            newval = OVERSCAN_CROP_16;
        else if (strcmp(var.value, "disabled") == 0)
            newval = OVERSCAN_CROP_OFF;

        if (newval != crop_overscan_mode)
        {
            crop_overscan_mode = newval;
            g_geometry_update = true;
        }
    }

    var.key = "snes9x_aspect";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        aspect_mode newval = ASPECT_RATIO_AUTO;
        if (strcmp(var.value, "ntsc") == 0)
            newval = ASPECT_RATIO_NTSC;
        else if (strcmp(var.value, "pal") == 0)
            newval = ASPECT_RATIO_PAL;
        else if (strcmp(var.value, "4:3") == 0)
            newval = ASPECT_RATIO_4_3;
        else if (strcmp(var.value, "4:3 scaled") == 0)
            newval = ASPECT_RATIO_4_3_SCALED;
        else if (strcmp(var.value, "uncorrected") == 0)
            newval = ASPECT_RATIO_1_1;

        if (newval != aspect_ratio_mode)
        {
            aspect_ratio_mode = newval;
            g_geometry_update = true;
        }
    }

    var.key = "snes9x_region";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (!strcmp(var.value, "auto"))
        {
            Settings.ForceNTSC = false;
            Settings.ForcePAL = false;
        }
        else if (!strcmp(var.value, "ntsc"))
        {
            Settings.ForceNTSC = true;
            Settings.ForcePAL = false;
        }
        else if (!strcmp(var.value, "pal"))
        {
            Settings.ForceNTSC = false;
            Settings.ForcePAL = true;
        }
    }

    var.key="snes9x_lightgun_mode";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
      if ( !strcmp(var.value, "Touchscreen") ) {
         setting_gun_input = SETTING_GUN_INPUT_POINTER;
      } else {
         setting_gun_input = SETTING_GUN_INPUT_LIGHTGUN;
      }
    }

    var.key="snes9x_superscope_reverse_buttons";
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        setting_superscope_reverse_buttons = strcmp(var.value, "enabled") == 0;
    }

    var.key="snes9x_superscope_crosshair";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        int crosshair;

        sscanf (var.value,"%d",&crosshair);
        S9xSetControllerCrosshair(X_SUPERSCOPE, crosshair, 0, 0);
    }

    var.key="snes9x_superscope_color";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        char *color = get_cursor_color(var.value);

        if (color[0] == 't')
            S9xSetControllerCrosshair(X_SUPERSCOPE, -1, get_cursor_color(var.value), "tBlack");
        else
            S9xSetControllerCrosshair(X_SUPERSCOPE, -1, get_cursor_color(var.value), "Black");
    }

    var.key="snes9x_justifier1_crosshair";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        int crosshair;

        sscanf (var.value,"%d",&crosshair);
        S9xSetControllerCrosshair(X_JUSTIFIER1, crosshair, 0, 0);
    }

    var.key="snes9x_justifier1_color";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        char *color = get_cursor_color(var.value);

        if (color[0] == 't')
            S9xSetControllerCrosshair(X_JUSTIFIER1, -1, get_cursor_color(var.value), "tBlack");
        else
            S9xSetControllerCrosshair(X_JUSTIFIER1, -1, get_cursor_color(var.value), "Black");
    }

    var.key="snes9x_justifier2_crosshair";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        int crosshair;

        sscanf (var.value,"%d",&crosshair);
        S9xSetControllerCrosshair(X_JUSTIFIER2, crosshair, 0, 0);
    }

    var.key="snes9x_justifier2_color";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        char *color = get_cursor_color(var.value);

        if (color[0] == 't')
            S9xSetControllerCrosshair(X_JUSTIFIER2, -1, get_cursor_color(var.value), "tBlack");
        else
            S9xSetControllerCrosshair(X_JUSTIFIER2, -1, get_cursor_color(var.value), "Black");
    }

    var.key="snes9x_rifle_crosshair";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        int crosshair;

        sscanf (var.value,"%d",&crosshair);
        S9xSetControllerCrosshair(X_MACSRIFLE, crosshair, 0, 0);
    }

    var.key="snes9x_rifle_color";

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        char *color = get_cursor_color(var.value);

        if (color[0] == 't')
            S9xSetControllerCrosshair(X_MACSRIFLE, -1, get_cursor_color(var.value), "tBlack");
        else
            S9xSetControllerCrosshair(X_MACSRIFLE, -1, get_cursor_color(var.value), "Black");
    }

    var.key = "snes9x_block_invalid_vram_access";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
        Settings.BlockInvalidVRAMAccessMaster = !strcmp(var.value, "disabled") ? false : true;
    else
        Settings.BlockInvalidVRAMAccessMaster = true;

    var.key = "snes9x_echo_buffer_hack";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
        Settings.SeparateEchoBuffer = !strcmp(var.value, "disabled") ? false : true;
    else
        Settings.SeparateEchoBuffer = false;

    var.key = "snes9x_blargg";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
    {
        if (strcmp(var.value, "disabled") == 0)
            blargg_filter = 0;
        else
        {
            int old_filter = blargg_filter;

            if(!snes_ntsc) snes_ntsc = new snes_ntsc_t;
            snes_ntsc_setup_t setup = snes_ntsc_composite;

            if (strcmp(var.value, "monochrome") == 0)
            {
                blargg_filter = 1;

                setup = snes_ntsc_monochrome;
            }
            else if (strcmp(var.value, "rf") == 0)
            {
                blargg_filter = 2;

                setup = snes_ntsc_composite;
                setup.merge_fields = 0;
            }
            else if (strcmp(var.value, "composite") == 0)
            {
                blargg_filter = 3;

                setup = snes_ntsc_composite;
            }
            else if (strcmp(var.value, "s-video") == 0)
            {
                blargg_filter = 4;

                setup = snes_ntsc_svideo;
            }
            else if (strcmp(var.value, "rgb") == 0)
            {
                blargg_filter = 5;

                setup = snes_ntsc_rgb;
            }

            if (old_filter != blargg_filter)
                snes_ntsc_init( snes_ntsc, &setup );
        }
    }

    /* Show/hide core options
     * > If frontend supports core option categories,
     *   then show/hide toggle options are ignored,
     *   and no other options should be hidden */

    var.key = "snes9x_show_lightgun_settings";
    var.value = NULL;

    if (!libretro_supports_option_categories &&
        environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        bool show_lightgun_settings_prev = show_lightgun_settings;

        show_lightgun_settings = true;
        if (strcmp(var.value, "disabled") == 0)
            show_lightgun_settings = false;

        if (show_lightgun_settings != show_lightgun_settings_prev)
        {
            size_t i;
            struct retro_core_option_display option_display;
            char lightgun_keys[10][64] = {
                "snes9x_lightgun_mode",
                "snes9x_superscope_reverse_buttons",
                "snes9x_superscope_crosshair",
                "snes9x_superscope_color",
                "snes9x_justifier1_crosshair",
                "snes9x_justifier1_color",
                "snes9x_justifier2_crosshair",
                "snes9x_justifier2_color",
                "snes9x_rifle_crosshair",
                "snes9x_rifle_color"
            };

            option_display.visible = show_lightgun_settings;

            for (i = 0; i < 10; i++)
            {
                option_display.key = lightgun_keys[i];
                environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
            }
        }
    }

    var.key = "snes9x_show_advanced_av_settings";
    var.value = NULL;

    if (!libretro_supports_option_categories &&
        environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        bool show_advanced_av_settings_prev = show_advanced_av_settings;

        show_advanced_av_settings = true;
        if (strcmp(var.value, "disabled") == 0)
            show_advanced_av_settings = false;

        if (show_advanced_av_settings != show_advanced_av_settings_prev)
        {
            size_t i;
            struct retro_core_option_display option_display;
            char av_keys[15][32] = {
                "snes9x_layer_1",
                "snes9x_layer_2",
                "snes9x_layer_3",
                "snes9x_layer_4",
                "snes9x_layer_5",
                "snes9x_gfx_clip",
                "snes9x_gfx_transp",
                "snes9x_sndchan_1",
                "snes9x_sndchan_2",
                "snes9x_sndchan_3",
                "snes9x_sndchan_4",
                "snes9x_sndchan_5",
                "snes9x_sndchan_6",
                "snes9x_sndchan_7",
                "snes9x_sndchan_8"
            };

            option_display.visible = show_advanced_av_settings;

            for (i = 0; i < 15; i++)
            {
                option_display.key = av_keys[i];
                environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
            }
        }
    }
}

/* Silence sent while audio is hard-disabled. Sized for the worst frame the
   DSP can hand us: PAL at ~641 stereo frames plus headroom for the APU
   speedup hack. Never written to, so it costs nothing but BSS. */
#define MUTE_BUFFER_FRAMES 768

/* 44.1 kHz needs more room per frame than the SPC rate: PAL worst case is
   44100/50 = 882 frames, plus headroom for the APU speedup hack. */
#define MSU1_ENH_FRAMES 1024

/* True while the enhanced upsampler holds valid in-flight state; cleared
   whenever the enhanced path is not the one feeding the frontend, so state
   never leaks across a mode change. */
static bool msu1_enh_running = false;

static bool msu1_enhanced_active(void)
{
    return msu1_enhanced_pref && Settings.MSU1;
}

/* The frontend is clocked at 44.1 kHz scaled by however far the SPC rate has
   been pushed, so the ratio the upsampler works to stays exact. */
static uint32_t msu1_enhanced_output_rate(void)
{
    return (uint32_t) (44100.0 * (double) S9xGetAudioSampleRate() / 32040.0);
}

/* Frame-boundary hook from cpuexec. The audio upload used to happen here,
   mid-frame at the start of vblank; it now happens at the end of retro_run so
   one call to the core produces exactly one video frame and one consecutive
   run of audio samples. Nothing else needs doing at this point - throttling
   is the frontend's job. */
void S9xSyncSpeed() {}

/* Deliver this frame's audio. Zero-copy in the normal case: S9xDrainAudio
   returns a pointer into the DSP's landing buffer and that pointer goes
   straight to the frontend. */
static void audio_upload_samples(void)
{
    int count = 0;
    const int16_t *src = S9xDrainAudio(&count);

    if (count <= 0)
        return;

    /* Muted means hard-disabled (see retro_run): the APU output is not
       meaningful, but the callback still has to run or the frontend's ring
       drains. A static zero buffer covers the largest frame we can produce. */
    if (Settings.Mute || !msu1_enhanced_active())
        msu1_enh_running = false;

    if (Settings.Mute)
    {
        static const int16_t silence[MUTE_BUFFER_FRAMES * 2] = { 0 };

        int frames = count >> 1;
        while (frames > 0)
        {
            int chunk = (frames > MUTE_BUFFER_FRAMES) ? MUTE_BUFFER_FRAMES : frames;
            audio_batch_cb(silence, (size_t) chunk);
            frames -= chunk;
        }
        return;
    }

    /* MSU-1 Enhanced Audio: run the frame at 44.1 kHz so the MSU-1 stream
       mixes in at its native rate instead of being decimated to the SPC's
       ~32040 Hz (libretro/snes9x#309). The SPC side is linearly upsampled
       here; the interpolator's frame pair and 32.32 phase live in struct MSU1
       so they survive savestates and rollback. */
    if (msu1_enhanced_active())
    {
        static int16_t enh_buffer[MSU1_ENH_FRAMES * 2];

        int16_t  cur_l = (int16_t) MSU1.MSU1_EnhCurL;
        int16_t  cur_r = (int16_t) MSU1.MSU1_EnhCurR;
        int16_t  nxt_l = (int16_t) MSU1.MSU1_EnhNxtL;
        int16_t  nxt_r = (int16_t) MSU1.MSU1_EnhNxtR;
        uint64_t frac  = MSU1.MSU1_EnhFrac;
        int      fill  = (int) MSU1.MSU1_EnhFill;

        int      in_frames  = count >> 1;
        int      in_pos     = 0;
        int      out_frames = 0;
        uint32_t in_rate    = S9xGetAudioSampleRate();
        uint32_t out_rate   = msu1_enhanced_output_rate();
        uint64_t step       = ((uint64_t) in_rate << 32) / out_rate;

        if (!msu1_enh_running)
        {
            frac = 0;
            fill = 0;
            msu1_enh_running = true;
        }

        while (fill < 2 && in_pos < in_frames)
        {
            if (fill == 0) { cur_l = src[in_pos * 2]; cur_r = src[in_pos * 2 + 1]; }
            else           { nxt_l = src[in_pos * 2]; nxt_r = src[in_pos * 2 + 1]; }
            fill++;
            in_pos++;
        }

        while (fill == 2 && out_frames < MSU1_ENH_FRAMES)
        {
            /* 64-bit product: |nxt - cur| * t reaches 65535 * 65535, which
               overflows int32 on full-scale transients. */
            uint32_t t = (uint32_t) (frac >> 16) & 0xffff;
            enh_buffer[out_frames * 2] = (int16_t) (cur_l +
                (int32_t) (((int64_t) (nxt_l - cur_l) * (int32_t) t) >> 16));
            enh_buffer[out_frames * 2 + 1] = (int16_t) (cur_r +
                (int32_t) (((int64_t) (nxt_r - cur_r) * (int32_t) t) >> 16));
            out_frames++;

            frac += step;
            while (frac >= ((uint64_t) 1 << 32))
            {
                frac -= (uint64_t) 1 << 32;
                cur_l = nxt_l; cur_r = nxt_r;
                if (in_pos < in_frames)
                {
                    nxt_l = src[in_pos * 2];
                    nxt_r = src[in_pos * 2 + 1];
                    in_pos++;
                }
                else
                {
                    fill = 1;   /* nxt refills from the next batch */
                    break;
                }
            }
        }

        MSU1.MSU1_EnhCurL = cur_l; MSU1.MSU1_EnhCurR = cur_r;
        MSU1.MSU1_EnhNxtL = nxt_l; MSU1.MSU1_EnhNxtR = nxt_r;
        MSU1.MSU1_EnhFrac = frac;
        MSU1.MSU1_EnhFill = (uint8_t) fill;

        if (out_frames > 0)
        {
            S9xMSU1Mix(enh_buffer, (size_t) out_frames, out_rate);
            audio_batch_cb(enh_buffer, (size_t) out_frames);
        }
        return;
    }

    /* Normal path: mix MSU-1 into the landing buffer at the SPC's own rate. */
    if (Settings.MSU1)
        S9xMSU1Mix((int16_t *) src, (size_t)(count >> 1), S9xGetAudioSampleRate());

    audio_batch_cb(src, (size_t)(count >> 1));
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info,0,sizeof(retro_system_info));

    info->library_name = "Snes9x";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
    info->library_version = VERSION GIT_VERSION;
    info->valid_extensions = "smc|sfc|swc|fig|bs|st";
    info->need_fullpath = false;
    info->block_extract = false;
}

float get_aspect_ratio(unsigned width, unsigned height)
{
    if (aspect_ratio_mode == ASPECT_RATIO_4_3)
    {
        return SNES_4_3;
    }
    else if (aspect_ratio_mode == ASPECT_RATIO_4_3_SCALED)
    {
        return (4.0f * (MAX_SNES_HEIGHT - height)) / (3.0f * (MAX_SNES_WIDTH - width));
    }
    else if (aspect_ratio_mode == ASPECT_RATIO_1_1)
    {
        return (float) width / (float) height;
    }

    // OV2: not sure if these really make sense - NTSC is similar to 4:3, PAL looks weird
    double sample_frequency_ntsc = 135000000.0f / 11.0f;
    double sample_frequency_pal = 14750000.0;

    double sample_freq = retro_get_region() == RETRO_REGION_NTSC ? sample_frequency_ntsc : sample_frequency_pal;
    double dot_rate = (Settings.PAL ? PAL_MASTER_CLOCK : NTSC_MASTER_CLOCK) / 4.0;

    if (aspect_ratio_mode == ASPECT_RATIO_NTSC) // ntsc
    {
        sample_freq = sample_frequency_ntsc;
        dot_rate = NTSC_MASTER_CLOCK / 4.0;
    }
    else if (aspect_ratio_mode == ASPECT_RATIO_PAL) // pal
    {
        sample_freq = sample_frequency_pal;
        dot_rate = PAL_MASTER_CLOCK / 4.0;
    }

    double par = sample_freq / 2.0 / dot_rate;
    return (float)(width * par / height);
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info,0,sizeof(retro_system_av_info));
    unsigned width = SNES_WIDTH;
    unsigned height = PPU.ScreenHeight;
    if (crop_overscan_mode == OVERSCAN_CROP_ON)
        height = SNES_HEIGHT;
    else if (crop_overscan_mode == OVERSCAN_CROP_12)
        height = 216;
    else if (crop_overscan_mode == OVERSCAN_CROP_16)
        height = 208;
    else if (crop_overscan_mode == OVERSCAN_CROP_OFF)
        height = SNES_HEIGHT_EXTENDED;

    info->geometry.base_width = width;
    info->geometry.base_height = height;
    info->geometry.max_width = MAX_SNES_WIDTH_NTSC > MAX_SNES_WIDTH_4X
                                   ? MAX_SNES_WIDTH_NTSC : MAX_SNES_WIDTH_4X;
    info->geometry.max_height = MAX_SNES_HEIGHT;
    info->geometry.aspect_ratio = get_aspect_ratio(width, height);
    /* What the core actually emits: the DSP's own rate, or the enhanced
       44.1 kHz cadence when the MSU-1 path is upsampling. */
    info->timing.sample_rate = msu1_enhanced_active()
                             ? (double) msu1_enhanced_output_rate()
                             : (double) S9xGetAudioSampleRate();
    info->timing.fps = retro_get_region() == RETRO_REGION_NTSC ? 21477272.0 / 357366.0 : 21281370.0 / 425568.0;

    g_screen_gun_width = width;
    g_screen_gun_height = height;
}

unsigned retro_api_version()
{
    return RETRO_API_VERSION;
}


void retro_reset()
{
    reset_button_cache();
    S9xSoftReset();
}

static unsigned snes_devices[8];
void retro_set_controller_port_device(unsigned port, unsigned device)
{
    reset_button_cache();
    if (port < 8)
    {
        int offset = snes_devices[0] == RETRO_DEVICE_JOYPAD_MULTITAP ? 4 : 1;
        switch (device)
        {
            case RETRO_DEVICE_JOYPAD:
                S9xSetController(port, CTL_JOYPAD, port * offset, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_JOYPAD;
                break;
            case RETRO_DEVICE_JOYPAD_MULTITAP:
                S9xSetController(port, CTL_MP5, port * offset, port * offset + 1, port * offset + 2, port * offset + 3);
                snes_devices[port] = RETRO_DEVICE_JOYPAD_MULTITAP;
                break;
            case RETRO_DEVICE_MOUSE:
                S9xSetController(port, CTL_MOUSE, port, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_MOUSE;
                break;
            case RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE:
                S9xSetController(port, CTL_SUPERSCOPE, 0, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE;
                break;
            case RETRO_DEVICE_LIGHTGUN_JUSTIFIER:
                S9xSetController(port, CTL_JUSTIFIER, 0, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_LIGHTGUN_JUSTIFIER;
                break;
            case RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2:
            	if ( port == 2 )
            	{
					S9xSetController(1, CTL_JUSTIFIER, 1, 0, 0, 0);
                	snes_devices[port] = RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2;
				}
				else
				{
					if (log_cb)
						log_cb(RETRO_LOG_ERROR, "Invalid Justifier (2P) assignment to port %d, must be port 2.\n", port);
					S9xSetController(port, CTL_NONE, 0, 0, 0, 0);
					snes_devices[port] = RETRO_DEVICE_NONE;
				}
                break;
            case RETRO_DEVICE_LIGHTGUN_MACS_RIFLE:
                S9xSetController(port, CTL_MACSRIFLE, 0, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_LIGHTGUN_MACS_RIFLE;
                break;
            case RETRO_DEVICE_NONE:
                S9xSetController(port, CTL_NONE, 0, 0, 0, 0);
                snes_devices[port] = RETRO_DEVICE_NONE;
                break;
            default:
                if (log_cb)
                    log_cb(RETRO_LOG_ERROR, "Invalid device (%d).\n", device);
                break;
        }

        S9xControlsSoftReset();
    }
    else if(device != RETRO_DEVICE_NONE)
        log_cb(RETRO_LOG_INFO, "Nonexistent Port (%d).\n", port);
}

void retro_cheat_reset()
{
    S9xDeleteCheats();
}

void retro_cheat_set(unsigned index, bool enabled, const char *codeline)
{
    char codeCopy[256];
    char* code;

    if (codeline == (char *)'\0') return;

    strcpy(codeCopy,codeline);
    code=strtok(codeCopy,"+,.; ");
    while (code != NULL) {
        //Convert GH RAW to PAR
        if (strlen(code)==9 && code[6]==':')
        {
            code[6]=code[7];
            code[7]=code[8];
            code[8]='\0';
        }

        /* Goldfinger was broken and nobody noticed. Removed */
        if (S9xAddCheatGroup (std::string("retro"), std::string(code)) >= 0)
        {
            if (enabled)
                S9xEnableCheatGroup (Cheat.group.size () - 1);
        }
        else
        {
            printf("CHEAT: Failed to recognize %s\n",code);
        }

        code=strtok(NULL,"+,.; "); // bad code, ignore
    }

    S9xCheatsEnable();
}

static void init_descriptors(void)
{
    struct retro_input_descriptor desc[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,		"D-Pad Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,		"B" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,		"A" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,		"X" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,		"Y" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,		"L" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,		"R" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,	"Select" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,		"Start" },
    
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,		"D-Pad Up" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,		"B" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,		"A" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,		"X" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,		"Y" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,		"L" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,		"R" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,	"Select" },
        { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,		"Start" },

        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,		"D-Pad Up" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,		"B" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,		"A" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,		"X" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,		"Y" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,		"L" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,		"R" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,	"Select" },
        { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,		"Start" },

        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,		"D-Pad Up" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,		"B" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,		"A" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,		"X" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,		"Y" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,		"L" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,		"R" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,	"Select" },
        { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,		"Start" },

        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,		"D-Pad Up" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,		"B" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,		"A" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,		"X" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,		"Y" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,		"L" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,		"R" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,	"Select" },
        { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,		"Start" },

        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,		"D-Pad Up" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,		"B" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,		"A" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,		"X" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,		"Y" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,		"L" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,		"R" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,	"Select" },
        { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,		"Start" },
        
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,		"D-Pad Up" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,		"B" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,		"A" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,		"X" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,		"Y" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,		"L" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,		"R" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,	"Select" },
        { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,		"Start" },

        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,		"D-Pad Up" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,		"B" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,		"A" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,		"X" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,		"Y" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,		"L" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,		"R" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,	"Select" },
        { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,		"Start" },
	    
        { 0, 0, 0, 0, NULL },
    };

    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

static bool valid_normal_bank (uint8 bankbyte)
{
    switch (bankbyte)
    {
        case 32: case 33: case 48: case 49:
            return (true);
    }

    return (false);
}

static int is_bsx (uint8 *p)
{
    if ((p[26] == 0x33 || p[26] == 0xFF) && (!p[21] || (p[21] & 131) == 128) && valid_normal_bank(p[24]))
    {
        unsigned char	m = p[22];

        if (!m && !p[23])
            return (2);

        if ((m == 0xFF && p[23] == 0xFF) || (!(m & 0xF) && ((m >> 4) - 1 < 12)))
            return (1);
    }

    return (0);
}

/* The header probe reads 32 bytes from p, so the caller must have that much
   left. Anything smaller than a header offset plus a header is not a cart of
   that layout anyway. */
static int is_bsx_at (const uint8 *data, size_t size, size_t offset)
{
    if (!data || offset + 32 > size)
        return (0);

    return (is_bsx((uint8 *) data + offset));
}

static bool8 LoadBIOS(uint8 *biosrom, const char *biosname, int biossize)
{
    RFILE	*fp;
    char	name[PATH_MAX + 1];
    bool8 r = FALSE;

    strcpy(name, S9xGetDirectory(ROMFILENAME_DIR).c_str());
    strcat(name, SLASH_STR);
    strcat(name, biosname);

    fp = rfopen(name, "rb");
    if (!fp)
    {
        strcpy(name, S9xGetDirectory(BIOS_DIR).c_str());
        strcat(name, SLASH_STR);
        strcat(name, biosname);

        fp = rfopen(name, "rb");
    }

    if (fp)
    {
        size_t size;

        size = rfread((void *) biosrom, 1, biossize, fp);
        rfclose(fp);
        if (size == (unsigned int) biossize)
            r = TRUE;
    }

    return (r);
}

static bool8 is_SufamiTurbo_Cart (const uint8 *data, uint32 size)
{
    if (size >= 0x80000 && size <= 0x100000 &&
        strncmp((char *) data, "BANDAI SFC-ADX", 14) == 0 && strncmp((char * ) (data + 0x10), "SFC-ADX BACKUP", 14) != 0)
        return (TRUE);
    else
        return (FALSE);
}

/* MSU-1 tracks are 44.1 kHz PCM. Mixing them into the SPC's ~32040 Hz stream
   decimates them through an interpolator with no anti-alias filtering, which
   folds 16.02-22.05 kHz track content down into the 10-16 kHz band as audible
   hiss (libretro/snes9x#309). The enhanced path avoids that by running the
   frame at 44.1 kHz - see audio_upload_samples. The rate reported to the
   frontend changes with it, so the frontend has to be told. */
static void msu1_update_playback_rate(void)
{
    static bool last_enhanced = false;
    bool enhanced = msu1_enhanced_active();

    if (enhanced != last_enhanced)
    {
        struct retro_system_av_info av_info;
        last_enhanced = enhanced;
        msu1_enh_running = false;
        retro_get_system_av_info(&av_info);
        environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
    }
}

bool retro_load_game(const struct retro_game_info *game)
{
    init_descriptors();

    update_variables();

    if(game->data == NULL && game->size == 0 && game->path != NULL)
        rom_loaded = Memory.LoadROM(game->path);
    else
    {
        uint8 *biosrom = new uint8[0x100000];

        if (game->path != NULL)
        {
            extract_basename(g_basename, game->path, sizeof(g_basename));
            extract_directory(g_rom_dir, game->path, sizeof(g_rom_dir));
        }

        if (is_SufamiTurbo_Cart((uint8 *) game->data, game->size)) {
            if ((rom_loaded = LoadBIOS(biosrom,"STBIOS.bin",0x40000)))
            rom_loaded = Memory.LoadMultiCartMem((const uint8_t*)game->data, game->size, 0, 0, biosrom, 0x40000);
        }

        else
        if (is_bsx_at((const uint8 *) game->data, game->size, 0x7fc0) == 1 ||
            is_bsx_at((const uint8 *) game->data, game->size, 0xffc0) == 1) {
            if ((rom_loaded = LoadBIOS(biosrom,"BS-X.bin",0x100000)))
            rom_loaded = Memory.LoadMultiCartMem(biosrom, 0x100000, (const uint8_t*)game->data, game->size, 0, 0);
        }

        else
            rom_loaded = Memory.LoadROMMem((const uint8_t*)game->data ,game->size, g_basename);

        if(biosrom) delete[] biosrom;
    }

    if (rom_loaded)
    {
        /* If we're in RGB565 format, switch frontend to that */
        if (RED_SHIFT_BITS == 11)
        {
            enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
            if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
            {
                return false;
            }
        }

        g_geometry_update = true;

        if (randomize_memory)
        {
            srand(time(NULL));
            for(size_t lcv = 0; lcv < sizeof(Memory.RAM); lcv++)
                Memory.RAM[lcv] = rand() % 256;
        }
    }

    if (!rom_loaded && log_cb)
        log_cb(RETRO_LOG_ERROR, "ROM loading failed...\n");

    msu1_update_playback_rate();

    Memory.ClearSRAM();

    return rom_loaded;
}

static void remove_header(uint8_t *&romptr, size_t &romsize, bool multicart_sufami)
{
    if (romptr==0 || romsize==0) return;

    uint32 calc_size = (romsize / 0x2000) * 0x2000;
    if ((romsize - calc_size == 512 && !Settings.ForceNoHeader) || Settings.ForceHeader)
    {
        romptr += 512;
        romsize -= 512;

        if(log_cb) log_cb(RETRO_LOG_INFO,"ROM header removed\n");
    }

    if (multicart_sufami && (romptr + romsize) >= (romptr + 0x100000))
    {
        if (strncmp((const char*)(romptr + 0x100000), "BANDAI SFC-ADX", 14) == 0 &&
            strncmp((const char*)(romptr + 0x000000), "BANDAI SFC-ADX", 14) == 0)
        {
            romptr += 0x100000;
            romsize -= 0x100000;

            if(log_cb) log_cb(RETRO_LOG_INFO,"Sufami Turbo Multi-ROM bios removed\n");
        }
    }
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    uint8_t *romptr[3];
    size_t romsize[3];

    for(size_t i=0; i < num_info; i++)
    {
        romptr[i] = (uint8_t *) info[i].data;
        romsize[i] = info[i].size;
        remove_header(romptr[i], romsize[i], true);
    }

    init_descriptors();
    rom_loaded = false;

    update_variables();
    switch (game_type)
    {
        case RETRO_GAME_TYPE_BSX:
            if(num_info == 1)
            {
              rom_loaded = Memory.LoadROMMem((const uint8_t*)romptr[0],romsize[0]);
            }
            else if(num_info == 2)
            {
                memcpy(Memory.BIOSROM,(const uint8_t*)romptr[0],info[0].size);
                rom_loaded = Memory.LoadROMMem((const uint8_t*)romptr[1],info[1].size);
            }

            if (!rom_loaded && log_cb)
                log_cb(RETRO_LOG_ERROR, "BSX ROM loading failed...\n");
            break;
        case RETRO_GAME_TYPE_BSX_SLOTTED:
        case RETRO_GAME_TYPE_MULTI_CART:
            if(num_info == 2)
            {
                if (is_SufamiTurbo_Cart((const uint8_t*)romptr[0], romsize[0]))
                {
                    log_cb(RETRO_LOG_ERROR, "Cart is Sufami Turbo...\n");
                    uint8 *biosrom = new uint8[0x40000];
                    uint8 *biosptr = biosrom;

                    if (LoadBIOS(biosptr, "STBIOS.bin", 0x40000))
                    {
                        if (log_cb)
                            log_cb(RETRO_LOG_INFO, "Loading Sufami Turbo link game\n");

                        rom_loaded = Memory.LoadMultiCartMem((const uint8_t*)romptr[0], romsize[0],
                               (const uint8_t*)romptr[1], romsize[1], biosptr, 0x40000);
                    }

                if (biosrom)
                    delete[] biosrom;
                }
                else
                {
                    if (log_cb)
                        log_cb(RETRO_LOG_INFO, "Loading Multi-Cart link game\n");

                    rom_loaded = Memory.LoadMultiCartMem((const uint8_t*)romptr[0], romsize[0],
                        (const uint8_t*)romptr[1], romsize[1], NULL, 0);
                }
            }

            if (!rom_loaded && log_cb)
                log_cb(RETRO_LOG_ERROR, "Multirom loading failed...\n");
            break;

        case RETRO_GAME_TYPE_SUFAMI_TURBO:
            if(num_info == 2)
            {
                uint8 *biosrom = new uint8[0x100000];

                if ((rom_loaded = LoadBIOS(biosrom,"STBIOS.bin",0x100000)))
                    rom_loaded = Memory.LoadMultiCartMem((const uint8_t*)romptr[0], romsize[0],
                        (const uint8_t*)romptr[1], romsize[1], biosrom, 0x40000);

                if (biosrom)
                    delete[] biosrom;
            }

            if (!rom_loaded && log_cb)
                log_cb(RETRO_LOG_ERROR, "Sufami Turbo ROM loading failed...\n");
            break;

        default:
            rom_loaded = false;
            log_cb(RETRO_LOG_ERROR, "Multi-cart ROM loading failed...\n");
            break;
    }

    if (rom_loaded)
    {
        if(RED_SHIFT_BITS == 11)
        {
            enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
            if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
                return false;
        }

        g_geometry_update = true;
    }

    msu1_update_playback_rate();

    return rom_loaded;
}

void retro_unload_game(void)
{}

static void map_buttons();

static void check_system_specs(void)
{
    /* TODO - might have to variably set performance level based on SuperFX/SA-1/etc */
    unsigned level = 12;
    environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_init(void)
{
    struct retro_log_callback log;

    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;
    else
        log_cb = NULL;

    const char *dir = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
        snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", dir);
    else
        snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", ".");

    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
        snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", dir);
    else
        snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", ".");

    // State that SNES9X supports achievements.
    bool achievements = true;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &achievements);

    memset(&Settings, 0, sizeof(Settings));
    Settings.MouseMaster = TRUE;
    Settings.SuperScopeMaster = TRUE;
    Settings.JustifierMaster = TRUE;
    Settings.MultiPlayer5Master = TRUE;
    Settings.MacsRifleMaster = TRUE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.Stereo = TRUE;
    Settings.Transparency = TRUE;
    Settings.InitialInfoStringTimeout = 120;
    Settings.HDMATimingHack = 100;
    Settings.BlockInvalidVRAMAccessMaster = TRUE;
    Settings.SeparateEchoBuffer = FALSE;
    Settings.CartAName[0] = 0;
    Settings.CartBName[0] = 0;
    Settings.AutoSaveDelay = 1;
    Settings.DontSaveOopsSnapshot = TRUE;
    Settings.ChannelsVolumePercent[0] = 100;
    Settings.ChannelsVolumePercent[1] = 100;
    Settings.ChannelsVolumePercent[2] = 100;
    Settings.ChannelsVolumePercent[3] = 100;
    Settings.ChannelsVolumePercent[4] = 100;
    Settings.ChannelsVolumePercent[5] = 100;
    Settings.ChannelsVolumePercent[6] = 100;
    Settings.ChannelsVolumePercent[7] = 100;
    Settings.ChannelsVolumePercent[8] = 100;

    CPU.Flags = 0;

    if (!Memory.Init() || !S9xInitAPU())
    {
        Memory.Deinit();
        S9xDeinitAPU();

        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "Failed to init Memory or APU.\n");
        exit(1);
    }

    S9xInitSound(32);

    S9xSetSoundMute(FALSE);
    S9xSetSamplesAvailableCallback(NULL, NULL);

    ntsc_screen_buffer = (uint16*) calloc(1, MAX_SNES_WIDTH_NTSC * 2 * (MAX_SNES_HEIGHT + 16));
    snes_ntsc_buffer = ntsc_screen_buffer + (MAX_SNES_WIDTH_NTSC >> 1) * 16;
    S9xGraphicsInit();

    S9xInitInputDevices();
    for (int i = 0; i < 2; i++)
    {
        S9xSetController(i, CTL_JOYPAD, i, 0, 0, 0);
        snes_devices[i] = RETRO_DEVICE_JOYPAD;
    }

    S9xUnmapAllControls();
    map_buttons();
    check_system_specs();
	
    if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
        libretro_supports_bitmasks = true;
}

#define MAP_BUTTON(id, name) S9xMapButton((id), S9xGetCommandT((name)))
#define MAKE_BUTTON(pad, btn) (((pad)<<4)|(btn))

/* Peripheral reports are edges, not state. The Super Scope's
   phys/next/read triple emulates one shot per trigger pull unless turbo
   is engaged: S9xSetJoypadLatch clears FIRE/CURSOR out of next_buttons
   after every read, so re-reporting a held trigger each frame re-arms it
   and the scope free-fires with turbo off. The Justifier and mouse
   handlers are written to the same press/release contract. Pads do not
   go through here at all - they are absolute state via
   S9xSetJoypadButtons - so this cache cannot desync the way the old
   whole-input one did in libretro/snes9x#313. It still has to be
   dropped whenever the core's own copy can change underneath it:
   savestate load restores the peripheral button state, and a device
   change or reset rebuilds it. */
static uint8 peripheral_button_cache[MAKE_BUTTON(3 + 1, 0)];

static void reset_button_cache(void)
{
    memset(peripheral_button_cache, 0, sizeof(peripheral_button_cache));
}

static void report_button(uint32 id, bool pressed)
{
    if (id < sizeof(peripheral_button_cache))
    {
        if (peripheral_button_cache[id] == (uint8) pressed)
            return;
        peripheral_button_cache[id] = (uint8) pressed;
    }

    S9xReportButton(id, pressed);
}


#define PAD_1 1
#define PAD_2 2
#define PAD_3 3
#define PAD_4 4
#define PAD_5 5
#define PAD_6 6
#define PAD_7 7
#define PAD_8 8

/* RETRO_DEVICE_ID_JOYPAD_* (0..11) -> SNES joypad register bits. Pad
   state is written straight into the core as an absolute mask every
   frame; the pads are not in the keymap at all. */
#define RETRO_PAD_ID_COUNT (RETRO_DEVICE_ID_JOYPAD_R + 1)

static const uint16 retro_to_snes_mask[RETRO_PAD_ID_COUNT] =
{
    SNES_B_MASK,       /* B      */
    SNES_Y_MASK,       /* Y      */
    SNES_SELECT_MASK,  /* SELECT */
    SNES_START_MASK,   /* START  */
    SNES_UP_MASK,      /* UP     */
    SNES_DOWN_MASK,    /* DOWN   */
    SNES_LEFT_MASK,    /* LEFT   */
    SNES_RIGHT_MASK,   /* RIGHT  */
    SNES_A_MASK,       /* A      */
    SNES_X_MASK,       /* X      */
    SNES_TL_MASK,      /* L      */
    SNES_TR_MASK,      /* R      */
};

static uint16 snes_pad_mask(int16_t joy_bits)
{
    uint16 mask = 0;
    for (int i = 0; i < RETRO_PAD_ID_COUNT; i++)
        if (joy_bits & (1 << i))
            mask |= retro_to_snes_mask[i];
    return mask;
}

#define MOUSE_X RETRO_DEVICE_ID_MOUSE_X
#define MOUSE_Y RETRO_DEVICE_ID_MOUSE_Y
#define MOUSE_LEFT RETRO_DEVICE_ID_MOUSE_LEFT
#define MOUSE_RIGHT RETRO_DEVICE_ID_MOUSE_RIGHT
#define MOUSE_FIRST MOUSE_X
#define MOUSE_LAST MOUSE_RIGHT

static int scope_buttons[] =
{
	RETRO_DEVICE_ID_LIGHTGUN_TRIGGER, // 2
	RETRO_DEVICE_ID_LIGHTGUN_CURSOR, // 3
	RETRO_DEVICE_ID_LIGHTGUN_TURBO, // 4
	RETRO_DEVICE_ID_LIGHTGUN_START, // 5
	RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN, // 6
};
static int scope_button_count = sizeof( scope_buttons ) / sizeof( int );

#define SUPER_SCOPE_TRIGGER 2
#define SUPER_SCOPE_CURSOR 3
#define SUPER_SCOPE_TURBO 4
#define SUPER_SCOPE_START 5
#define SUPER_SCOPE_OFFSCREEN 6

#define JUSTIFIER_TRIGGER 2
#define JUSTIFIER_START 3
#define JUSTIFIER_OFFSCREEN 4

#define MACS_RIFLE_TRIGGER 2

/* Keymap IDs. Only the peripherals are in the keymap now, occupying
   MAKE_BUTTON(pad 1-3, slot 2-6) i.e. 0x12-0x36, so the two pointer IDs
   just have to sit clear of that range. */
#define BTN_POINTER 0x90
#define BTN_POINTER2 (BTN_POINTER + 1)


static void map_buttons()
{
    reset_button_cache();
    /* Joypads deliberately absent: report_buttons() writes their
       absolute state into the core directly. Only the peripherals go
       through the keymap, on exactly the MAKE_BUTTON slots the report
       paths already use - mouse buttons on pad-N slots 2/3, Super Scope
       on pad-2 slots 2-6, Justifier 1 on pad-2 slots 2-4, Justifier 2
       and the MACS Rifle likewise. */
    MAP_BUTTON(MAKE_BUTTON(PAD_1, MOUSE_LEFT), "Mouse1 L");
    MAP_BUTTON(MAKE_BUTTON(PAD_1, MOUSE_RIGHT), "Mouse1 R");

    MAP_BUTTON(MAKE_BUTTON(PAD_2, SUPER_SCOPE_TRIGGER), "{Mouse2 L,Superscope Fire,Justifier1 Trigger,MacsRifle Trigger}");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, SUPER_SCOPE_CURSOR), "{Mouse2 R,Superscope Cursor,Justifier1 Start}");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, SUPER_SCOPE_TURBO), "{Superscope ToggleTurbo,Justifier1 AimOffscreen}");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, SUPER_SCOPE_START), "Superscope Pause");
    MAP_BUTTON(MAKE_BUTTON(PAD_2, SUPER_SCOPE_OFFSCREEN), "Superscope AimOffscreen");

    MAP_BUTTON(MAKE_BUTTON(PAD_3, JUSTIFIER_TRIGGER), "Justifier2 Trigger");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, JUSTIFIER_START), "Justifier2 Start");
    MAP_BUTTON(MAKE_BUTTON(PAD_3, JUSTIFIER_OFFSCREEN), "Justifier2 AimOffscreen");

    S9xMapPointer((BTN_POINTER), S9xGetCommandT("Pointer Mouse1+Superscope+Justifier1+MacsRifle"));
    S9xMapPointer((BTN_POINTER2), S9xGetCommandT("Pointer Mouse2+Justifier2"));
}

static int16_t snes_mouse_state[2][2] = {{0}, {0}};
static bool snes_superscope_turbo_latch = false;

static void input_report_gun_position( unsigned port, int s9xinput )
{
	int x, y;

    x = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
    y = input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);

	/*scale & clamp*/
	x = ( ( x + 0x7FFF ) * g_screen_gun_width ) / 0xFFFF;
	if ( x < 0 )
		x = 0;
	else if ( x >= g_screen_gun_width )
		x = g_screen_gun_width - 1;

	/*scale & clamp*/
	y = ( ( y + 0x7FFF ) * g_screen_gun_height ) / 0xFFFF;
	if ( y < 0 )
		y = 0;
	else if ( y >= g_screen_gun_height )
		y = g_screen_gun_height - 1;

	S9xReportPointer(s9xinput, (int16_t)x, (int16_t)y);
}

static void input_handle_pointer_lightgun( unsigned port, unsigned gun_device, int s9xinput )
{
    int x, y;
    x = input_state_cb(port, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
    y = input_state_cb(port, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);

	/*scale & clamp*/
	x = ( ( x + 0x7FFF ) * g_screen_gun_width ) / 0xFFFF;
	if ( x < 0 )
		x = 0;
	else if ( x >= g_screen_gun_width )
		x = g_screen_gun_width - 1;

	/*scale & clamp*/
	y = ( ( y + 0x7FFF ) * g_screen_gun_height ) / 0xFFFF;
	if ( y < 0 )
		y = 0;
	else if ( y >= g_screen_gun_height )
		y = g_screen_gun_height - 1;

    // Touch sensitivity: Keep the gun position held for a fixed number of cycles after touch is released
    // because a very light touch can result in a misfire
    if ( pointer_cycles_after_released > 0 && pointer_cycles_after_released < POINTER_PRESSED_CYCLES ) {
        pointer_cycles_after_released++;
        x = pointer_pressed_last_x;
        y = pointer_pressed_last_y;
        S9xReportPointer(s9xinput, (int16_t)x, (int16_t)y);
        return;
    }

    if ( input_state_cb( port, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED ) )
    {
        pointer_pressed = 1;
        pointer_cycles_after_released = 0;
        pointer_pressed_last_x = x;
        pointer_pressed_last_y = y;
    } else if ( pointer_pressed ) {
        pointer_cycles_after_released++;
        pointer_pressed = 0;
        x = pointer_pressed_last_x;
        y = pointer_pressed_last_y;
        // unpress the primary trigger
        switch (gun_device)
        {
        case RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE:
            report_button(MAKE_BUTTON(PAD_2, setting_superscope_reverse_buttons ? SUPER_SCOPE_CURSOR : SUPER_SCOPE_TRIGGER), false);
            break;
        case RETRO_DEVICE_LIGHTGUN_JUSTIFIER:
            report_button(MAKE_BUTTON(PAD_2, JUSTIFIER_TRIGGER), false);
            break;
        case RETRO_DEVICE_LIGHTGUN_MACS_RIFLE:
            report_button(MAKE_BUTTON(PAD_2, MACS_RIFLE_TRIGGER), false);
            break;
        default:
            break;
        }
        return;
    }
    S9xReportPointer(s9xinput, (int16_t)x, (int16_t)y);

    // triggers
    switch (gun_device)
    {
        case RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE:
        {
            bool start_pressed = false;
            bool trigger_pressed = false;
            bool turbo_pressed = false;
            bool cursor_pressed = false;
            if ( input_state_cb(port, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED) ) {
                int touch_count = input_state_cb(port, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_COUNT);
                if ( touch_count == 4 ) {
                    // start button
                    start_pressed = true;
                } else if ( touch_count == 3 ) {
                    turbo_pressed = true;
                } else if ( touch_count == 2 ) {
                    if ( setting_superscope_reverse_buttons )
                    {
                        trigger_pressed = true;
                    } else
                    {
                        cursor_pressed = true;
                    }
                } else {
                    if ( setting_superscope_reverse_buttons )
                    {
                        cursor_pressed = true;
                    } else
                    {
                        trigger_pressed = true;
                    }
                }
            }
            report_button(MAKE_BUTTON(PAD_2, SUPER_SCOPE_START), start_pressed);
            report_button(MAKE_BUTTON(PAD_2, SUPER_SCOPE_TRIGGER), trigger_pressed);
            report_button(MAKE_BUTTON(PAD_2, SUPER_SCOPE_CURSOR), cursor_pressed);
            bool old_turbo = turbo_pressed;
            turbo_pressed = turbo_pressed && !snes_superscope_turbo_latch;
            snes_superscope_turbo_latch = old_turbo;
            report_button(MAKE_BUTTON(PAD_2, SUPER_SCOPE_TURBO), turbo_pressed);
            break;
        }

        case RETRO_DEVICE_LIGHTGUN_JUSTIFIER:
        {
            bool trigger_pressed = false;
            bool start_pressed = false;
            bool offscreen = false;
            if ( input_state_cb(port, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED) ) {
                int touch_count = input_state_cb(port, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_COUNT);
                if ( touch_count == 3 ) {
                    start_pressed = true;
                } else if ( touch_count == 2 ) {
                    offscreen = true;
                } else {
                    trigger_pressed = true;
                }
            }
            report_button(MAKE_BUTTON(PAD_2, JUSTIFIER_TRIGGER), trigger_pressed || offscreen);
            report_button(MAKE_BUTTON(PAD_2, JUSTIFIER_START), start_pressed ? 1 : 0 );
            report_button(MAKE_BUTTON(PAD_2, JUSTIFIER_OFFSCREEN), offscreen);
            break;
        }
        case RETRO_DEVICE_LIGHTGUN_MACS_RIFLE:
        {
            int pressed = input_state_cb(port, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
            report_button(MAKE_BUTTON(PAD_2, MACS_RIFLE_TRIGGER),pressed);
            break;
        }
        case RETRO_DEVICE_NONE:
            break;
        default:
            if (log_cb)
                log_cb(RETRO_LOG_ERROR, "Unknown device for touchscreen lightgun...\n");
    }
}

static void report_buttons()
{
    int offset = snes_devices[0] == RETRO_DEVICE_JOYPAD_MULTITAP ? 4 : 1;
    int _x, _y;
    int16_t joy_bits;

    for (int port = 0; port <= 1; port++)
    {
        switch (snes_devices[port])
        {
            case RETRO_DEVICE_JOYPAD:
                if (libretro_supports_bitmasks)
                    joy_bits = input_state_cb(port * offset, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
                else
                {
                    joy_bits = 0;
                    for (int i = 0; i < RETRO_PAD_ID_COUNT; i++)
                        joy_bits |= input_state_cb(port * offset, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
                }

                S9xSetJoypadButtons(port * offset, snes_pad_mask(joy_bits));
                break;

            case RETRO_DEVICE_JOYPAD_MULTITAP:
                for (int j = 0; j < 4; j++)
                {
                    if (libretro_supports_bitmasks)
                        joy_bits = input_state_cb(port * offset + j, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
                    else
                    {
                        joy_bits = 0;
                        for (int i = 0; i < RETRO_PAD_ID_COUNT; i++)
                            joy_bits |= input_state_cb(port * offset + j, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
                    }

                    S9xSetJoypadButtons(port * offset + j, snes_pad_mask(joy_bits));
                }
                break;

            case RETRO_DEVICE_MOUSE:
                _x = input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
                _y = input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
                snes_mouse_state[port][0] += _x;
                snes_mouse_state[port][1] += _y;
                S9xReportPointer(BTN_POINTER + port, snes_mouse_state[port][0], snes_mouse_state[port][1]);
                for (int i = MOUSE_LEFT; i <= MOUSE_LAST; i++)
                    report_button(MAKE_BUTTON(port + 1, i), input_state_cb(port, RETRO_DEVICE_MOUSE, 0, i));
                break;

            case RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE:

                if ( setting_gun_input == SETTING_GUN_INPUT_POINTER ) {
                    input_handle_pointer_lightgun(port, RETRO_DEVICE_LIGHTGUN_SUPER_SCOPE, BTN_POINTER);
                } else {
                    // Lightgun is default
                    input_report_gun_position( port, BTN_POINTER );

                    for (int i = 0; i < scope_button_count; i++)
                    {
                        int id = scope_buttons[i];
                        bool btn = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, id )?true:false;

                        /* RETRO_DEVICE_ID_LIGHTGUN_TURBO special case - core needs a rising-edge trigger */
                        if ( id == RETRO_DEVICE_ID_LIGHTGUN_TURBO )
                        {
                            bool old = btn;
                            btn = btn && !snes_superscope_turbo_latch;
                            snes_superscope_turbo_latch = old;
                        }
                        int super_scope_button_id = i+2;
                        if ( setting_superscope_reverse_buttons )
                        {
                            if ( super_scope_button_id == SUPER_SCOPE_TRIGGER ) {
                                super_scope_button_id = SUPER_SCOPE_CURSOR;
                            } else if ( super_scope_button_id == SUPER_SCOPE_CURSOR ) {
                                super_scope_button_id = SUPER_SCOPE_TRIGGER;
                            }
                        }
                        report_button(MAKE_BUTTON(PAD_2, super_scope_button_id), btn);
                    }
                }
                break;

            case RETRO_DEVICE_LIGHTGUN_JUSTIFIER:

                if ( setting_gun_input == SETTING_GUN_INPUT_POINTER ) {
                    input_handle_pointer_lightgun(port, RETRO_DEVICE_LIGHTGUN_JUSTIFIER, BTN_POINTER);
                } else {
                    // Lightgun is default
                    input_report_gun_position( port, BTN_POINTER );

                    {
                        /* Special Reload Button */
                        int btn_offscreen_shot = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD );

                        /* Trigger ? */
                        int btn_trigger = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER );
                        report_button(MAKE_BUTTON(PAD_2, JUSTIFIER_TRIGGER), btn_trigger || btn_offscreen_shot);

                        /* Start Button ? */
                        int btn_start = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START );
                        report_button(MAKE_BUTTON(PAD_2, JUSTIFIER_START), btn_start ? 1 : 0 );

                        /* Aiming off-screen ? */
                        int btn_offscreen = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN );
                        report_button(MAKE_BUTTON(PAD_2, JUSTIFIER_OFFSCREEN), btn_offscreen || btn_offscreen_shot);
                    }

                    /* Second Gun? */
                    if ( snes_devices[port+1] == RETRO_DEVICE_LIGHTGUN_JUSTIFIER_2 )
                    {
                        int second = port+1;

                        input_report_gun_position( second, BTN_POINTER2 );

                        /* Special Reload Button */
                        int btn_offscreen_shot = input_state_cb( second, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD );

                        /* Trigger ? */
                        int btn_trigger = input_state_cb( second, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER );
                        report_button(MAKE_BUTTON(PAD_3, JUSTIFIER_TRIGGER), btn_trigger || btn_offscreen_shot);

                        /* Start Button ? */
                        int btn_start = input_state_cb( second, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START );
                        report_button(MAKE_BUTTON(PAD_3, JUSTIFIER_START), btn_start ? 1 : 0 );

                        /* Aiming off-screen ? */
                        int btn_offscreen = input_state_cb( second, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN );
                        report_button(MAKE_BUTTON(PAD_3, JUSTIFIER_OFFSCREEN), btn_offscreen || btn_offscreen_shot);
                    }
                }
                break;

            case RETRO_DEVICE_LIGHTGUN_MACS_RIFLE:

                if ( setting_gun_input == SETTING_GUN_INPUT_POINTER ) {
                    input_handle_pointer_lightgun(port, RETRO_DEVICE_LIGHTGUN_MACS_RIFLE, BTN_POINTER);
                } else {
                    input_report_gun_position( port, BTN_POINTER );

                    {
                        /* Trigger ? */
                        int btn_trigger = input_state_cb( port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER );
                        report_button(MAKE_BUTTON(PAD_2, MACS_RIFLE_TRIGGER), btn_trigger);
                    }
                }
                break;

            case RETRO_DEVICE_NONE:
                break;

            default:
                if (log_cb)
                    log_cb(RETRO_LOG_ERROR, "Unknown device...\n");
        }
    }
}

void retro_run()
{
    static uint16 height = PPU.ScreenHeight;
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
        update_variables();

    if (g_geometry_update || height != PPU.ScreenHeight)
    {
        update_geometry();
        height = PPU.ScreenHeight;
    }

    int result = -1;
    bool okay = environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &result);
    if (okay)
    {
        bool videoEnabled     = 0 != (result & 1);
        bool hardDisableAudio = 0 != (result & 8);

        IPPU.RenderThisFrame = videoEnabled;

        /* RETRO_AV_ENABLE_AUDIO (0x02) is deliberately ignored. It says the
           frontend will discard these samples, not that the core may stop
           producing them - and the state the frames leave behind is kept.
           Preemptive Frames replays frames with audio suspended and
           hard-disable clear, then carries the replayed state forward, so
           muting there freezes every audio-side cursor - the MSU-1 play
           offset above all - while the CPU and APU advance. The stream falls
           a replay window behind on every rollback, which is heard as the
           music slowing and popping.

           RETRO_AV_ENABLE_HARD_DISABLE_AUDIO (0x08) is the bit that grants
           permission to skip the work: RetroArch only sets it where the
           resulting state is discarded or restored afterwards. Suspension
           costs output, never state. */
        S9xSetSoundMute(hardDisableAudio);
    }
    else
    {
        IPPU.RenderThisFrame = true;
        S9xSetSoundMute(false);
    }

    poll_cb();
    report_buttons();
    S9xMainLoop();

    audio_upload_samples();
}

void retro_deinit()
{
    S9xDeinitAPU();
    Memory.Deinit();
    S9xGraphicsDeinit();
    S9xUnmapAllControls();

    free(screen_buffer);
    free(ntsc_screen_buffer);

    libretro_supports_option_categories = false;
    libretro_supports_bitmasks = false;
}


unsigned retro_get_region()
{
    return Settings.PAL ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

void* retro_get_memory_data(unsigned type)
{
    void* data;

    switch(type)
    {
        case RETRO_MEMORY_SNES_SUFAMI_TURBO_A_RAM:
        case RETRO_MEMORY_SAVE_RAM:
            data = Memory.SRAM;
            break;
        case RETRO_MEMORY_SNES_SUFAMI_TURBO_B_RAM:
            data = Multi.sramB;
            break;
        case RETRO_MEMORY_RTC:
            data = RTCData.reg;
            break;
        case RETRO_MEMORY_SYSTEM_RAM:
            data = Memory.RAM;
            break;
        case RETRO_MEMORY_VIDEO_RAM:
            data = Memory.VRAM;
            break;
        case RETRO_MEMORY_ROM:
            data = Memory.ROM;
            break;
        default:
            data = NULL;
            break;
    }

    return data;
}

size_t retro_get_memory_size(unsigned type)
{
    size_t size;

    switch(type) {
        case RETRO_MEMORY_SNES_SUFAMI_TURBO_A_RAM:
        case RETRO_MEMORY_SAVE_RAM:
            size = (unsigned) (Memory.SRAMSize ? (1 << (Memory.SRAMSize + 3)) * 128 : 0);
            if (size > 0x20000)
            size = 0x20000;
            break;
        case RETRO_MEMORY_SNES_SUFAMI_TURBO_B_RAM:
            size = (unsigned) (Multi.cartType==4 && Multi.sramSizeB ? (1 << (Multi.sramSizeB + 3)) * 128 : 0);
            break;
        case RETRO_MEMORY_RTC:
            size = (Settings.SRTC || Settings.SPC7110RTC)?20:0;
            break;
        case RETRO_MEMORY_SYSTEM_RAM:
            size = 128 * 1024;
            break;
        case RETRO_MEMORY_VIDEO_RAM:
            size = 64 * 1024;
            break;
        case RETRO_MEMORY_ROM:
            size = Memory.CalculatedSize;
            break;
        default:
            size = 0;
            break;
    }

    return size;
}

/* Decide whether this (de)serialisation may take the fast in-place path.
 *
 * "Fast" means the state is one the core itself produced moments ago and is
 * about to consume again, so the big blocks can be read straight into
 * VRAM/WRAM/SRAM/fillram instead of being staged through local copies. The
 * staging exists so a truncated or corrupt state cannot half-apply, which
 * matters for a file off disk and does not matter for runahead, preemptive
 * frames or netplay rollback.
 *
 * RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT is the supported query; every
 * context other than NORMAL is a frontend-internal state and wants the fast
 * path. RETRO_AV_ENABLE_FAST_SAVESTATES carries the same information from the
 * same frontend flag but libretro.h marks it deprecated, so it stays only as
 * the fallback for frontends that do not answer the context query. */
static bool savestate_wants_fast_path(void)
{
    enum retro_savestate_context context = RETRO_SAVESTATE_CONTEXT_NORMAL;
    int                          av      = 0;

    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT, &context))
        return context != RETRO_SAVESTATE_CONTEXT_NORMAL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av))
        return 0 != (av & RETRO_AV_ENABLE_FAST_SAVESTATES);

    return false;
}

size_t retro_serialize_size()
{
    return rom_loaded ? S9xFreezeSize() : 0;
}

bool retro_serialize(void *data, size_t size)
{
    Settings.FastSavestates = savestate_wants_fast_path();

    if (S9xFreezeGameMem((uint8_t*)data,size) == FALSE)
        return false;

    return true;
}

bool retro_unserialize(const void* data, size_t size)
{
    reset_button_cache();

    Settings.FastSavestates = savestate_wants_fast_path();

    if (S9xUnfreezeGameMem((const uint8_t*)data,size) != SUCCESS)
        return false;

    // restore disabled sound channels
    if (disabled_channels)
    {
        S9xSetSoundControl(disabled_channels^0xFF);
    }

    return true;
}

bool8 S9xDeinitUpdate(int width, int height)
{
    static int burst_phase = 0;
    int overscan_offset = 0;

    if (crop_overscan_mode == OVERSCAN_CROP_ON)
    {
        if (height > SNES_HEIGHT * 2)
        {
            overscan_offset = 14;
            height = SNES_HEIGHT * 2;
        }
        else if ((height > SNES_HEIGHT) && (height != SNES_HEIGHT * 2))
        {
            overscan_offset = 7;
            height = SNES_HEIGHT;
        }
    }
    else if (crop_overscan_mode == OVERSCAN_CROP_12)
    {
        if (height > 216 * 2)
        {
            overscan_offset = 8;
            height = 216 * 2;
        }
        else if ((height > 216) && (height != 216 * 2))
        {
            overscan_offset = 4;
            height = 216;
        }
    }
    else if (crop_overscan_mode == OVERSCAN_CROP_16)
    {
        if (height > 208 * 2)
        {
            overscan_offset = 16;
            height = 208 * 2;
        }
        else if ((height > 208) && (height != 208 * 2))
        {
            overscan_offset = 8;
            height = 208;
        }
    }
    else if (crop_overscan_mode == OVERSCAN_CROP_OFF)
    {
        if (height > SNES_HEIGHT_EXTENDED)
        {
            if (height < SNES_HEIGHT_EXTENDED * 2)
            {
                overscan_offset = -16;
                memset(GFX.Screen + (GFX.Pitch >> 1) * height,0,GFX.Pitch * ((SNES_HEIGHT_EXTENDED << 1) - height));
            }
            height = SNES_HEIGHT_EXTENDED * 2;
        }
        else
        {
            if (height < SNES_HEIGHT_EXTENDED)
            {
                overscan_offset = -8;
                memset(GFX.Screen + (GFX.Pitch >> 1) * height,0,GFX.Pitch * (SNES_HEIGHT_EXTENDED - height));
            }
            height = SNES_HEIGHT_EXTENDED;
        }
    }


    if (blargg_filter)
    {
        burst_phase = (burst_phase + 1) % 3;

        if (width > 512)
        {
            /* HD Mode 7 4x frame. The NTSC filter models a composite
               signal whose output tops out at SNES_NTSC_OUT_WIDTH(256)
               = 602 px, so input columns beyond 512 add no information
               -- and the lores blitter, fed 1024-px rows, would write
               ~2400 px per line into a 604-px-pitch buffer (garbage
               plus heap overflow). Box-downsample each row 2:1 in
               place (per-channel floor average via the LSB-exact
               halving-add identity, as in S9xMode7VertResample) and
               use the hires path; the 4x sub-pixel detail survives as
               anti-aliasing. In-place is safe: x ascends, so reads at
               2x/2x+1 stay ahead of the write at x. */
            for (int y = 0; y < height; y++)
            {
                uint16 *row = GFX.Screen + (size_t) y * (GFX.Pitch >> 1);
                for (int x = 0; x < 512; x++)
                {
                    uint16 a = row[2 * x], b = row[2 * x + 1];
                    row[x] = ((a & 0xF7DE) >> 1) + ((b & 0xF7DE) >> 1) + (a & b & 0x0821);
                }
            }
            width = 512;
        }

        if (width == 512)
            snes_ntsc_blit_hires(snes_ntsc, GFX.Screen, GFX.Pitch / 2, burst_phase, width, height, snes_ntsc_buffer, MAX_SNES_WIDTH_NTSC * 2);
        else
            snes_ntsc_blit(snes_ntsc, GFX.Screen, GFX.Pitch / 2, burst_phase, width, height, snes_ntsc_buffer, MAX_SNES_WIDTH_NTSC * 2);

        video_cb(snes_ntsc_buffer + ((int)(MAX_SNES_WIDTH_NTSC) * overscan_offset), SNES_NTSC_OUT_WIDTH(256), height, MAX_SNES_WIDTH_NTSC * 2);
    }
    else if (width == MAX_SNES_WIDTH && hires_blend)
    {
        #define AVERAGE_565(el0, el1) (((el0) & (el1)) + ((((el0) ^ (el1)) & 0xF7DE) >> 1))

        if (hires_blend == 1) /* Blur method */
        {
            for (int y = 0; y < height; y++)
            {
                uint16 *input = (uint16 *) ((uint8 *) GFX.Screen + y * GFX.Pitch);
                uint16 *output = (uint16 *) ((uint8 *) GFX.Screen + y * GFX.Pitch);
                uint16 l, r;

                l = 0;
                for (int x = 0; x < (width >> 1); x++)
                {
                    r = *input++;
                    *output++ = AVERAGE_565 (l, r);
                    l = r;

                    r = *input++;
                    *output++ = AVERAGE_565 (l, r);
                    l = r;
                }
            }
        }
        else if (hires_blend == 2) /* Merge method */
        {
            for (int y = 0; y < height; y++)
            {
                uint16 *input = (uint16 *) ((uint8 *) GFX.Screen + y * GFX.Pitch);
                uint16 *output = (uint16 *) ((uint8 *) GFX.Screen + y * GFX.Pitch);
                uint16 l, r;

                for (int x = 0; x < (width >> 1); x++)
                {
                    l = *input++;
                    r = *input++;
                    *output++ = AVERAGE_565 (l, r);
                }
            }

            width >>= 1;
        }

        video_cb(GFX.Screen + ((int)(GFX.Pitch >> 1) * overscan_offset), width, height, GFX.Pitch);
    }
    else
    {
        video_cb(GFX.Screen + ((int)(GFX.Pitch >> 1) * overscan_offset), width, height, GFX.Pitch);
    }

    return TRUE;
}

bool8 S9xContinueUpdate(int width, int height)
{
    S9xDeinitUpdate(width, height);
    return true;
}

// Dummy functions that should probably be implemented correctly later.
const char* S9xStringInput(const char* in) { return in; }

#ifdef _WIN32
#define SLASH '\\'
#else
#define SLASH '/'
#endif

std::string S9xGetDirectory(s9x_getdirtype type)
{
    switch (type)
    {
        case BIOS_DIR:
            return std::string(retro_system_directory);
        default:
            return std::string(g_rom_dir);
    }

    return std::string("");
}
void S9xInitInputDevices() {}
const char* S9xBasename(const char* in) { return in; }
bool8 S9xInitUpdate() { return TRUE; }

void S9xMessage(int type, int, const char* s)
{
    if (!log_cb) return;

    switch (type)
    {
        case S9X_DEBUG:
            log_cb(RETRO_LOG_DEBUG, "%s\n", s);
            break;
        case S9X_WARNING:
            log_cb(RETRO_LOG_WARN, "%s\n", s);
            break;
        case S9X_INFO:
            log_cb(RETRO_LOG_INFO, "%s\n", s);
            break;
        case S9X_ERROR:
            log_cb(RETRO_LOG_ERROR, "%s\n", s);
            break;
        default:
            log_cb(RETRO_LOG_DEBUG, "%s\n", s);
            break;
    }
}

void S9xAutoSaveSRAM()
{
    return;
}
