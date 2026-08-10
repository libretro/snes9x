/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>
#include <assert.h>
#include <ctype.h>

#include "snes9x.h"
#include "memmap.h"
#include "apu/apu.h"
#include "snapshot.h"
#include "controls.h"
#include "crosshairs.h"
#include "display.h"

using namespace	std;

#define NONE					(-2)
#define MP5						(-1)
#define JOYPAD0					0
#define JOYPAD1					1
#define JOYPAD2					2
#define JOYPAD3					3
#define JOYPAD4					4
#define JOYPAD5					5
#define JOYPAD6					6
#define JOYPAD7					7
#define MOUSE0					8
#define MOUSE1					9
#define SUPERSCOPE				10
#define ONE_JUSTIFIER			11
#define TWO_JUSTIFIERS			12
#define MACSRIFLE				13
#define NUMCTLS					14 // This must be LAST


#define SUPERSCOPE_FIRE			0x80
#define SUPERSCOPE_CURSOR		0x40
#define SUPERSCOPE_TURBO		0x20
#define SUPERSCOPE_PAUSE		0x10
#define SUPERSCOPE_OFFSCREEN	0x02

#define JUSTIFIER_TRIGGER		0x80
#define JUSTIFIER_START			0x20
#define JUSTIFIER_SELECT		0x08

#define MACSRIFLE_TRIGGER		0x01

#define MAP_UNKNOWN				(-1)
#define MAP_NONE				0
#define MAP_BUTTON				1
#define MAP_POINTER				3

#define FLAG_IOBIT0				(Memory.FillRAM[0x4213] & 0x40)
#define FLAG_IOBIT1				(Memory.FillRAM[0x4213] & 0x80)
#define FLAG_IOBIT(n)			((n) ? (FLAG_IOBIT1) : (FLAG_IOBIT0))

bool8	pad_read = 0, pad_read_last = 0;
uint8	read_idx[2 /* ports */][2 /* per port */];

struct exemulti
{
	int32				pos;
	bool8				data1;
	s9xcommand_t		*script;
};

struct crosshair
{
	uint8				set;
	uint8				img;
	uint8				fg, bg;
};

static struct
{
	uint16				buttons;
}	joypad[8];

static struct
{
	uint8				delta_x, delta_y;
	int16				old_x, old_y;
	int16				cur_x, cur_y;
	uint8				buttons;
	uint32				ID;
	struct crosshair	crosshair;
}	mouse[2];

static struct
{
	int16				x, y;
	uint8				phys_buttons;
	uint8				next_buttons;
	uint8				read_buttons;
	uint32				ID;
	struct crosshair	crosshair;
}	superscope;

static struct
{
	int16				x[2], y[2];
	uint8				buttons;
	bool8				offscreen[2];
	uint32				ID[2];
	struct crosshair	crosshair[2];
}	justifier;

static struct
{
	int8				pads[4];
}	mp5[2];

static struct
{
	int16				x, y;
	uint8				buttons;
	uint32				ID;
	struct crosshair	crosshair;
}	macsrifle;

static set<struct exemulti *>		exemultis;
static map<uint32, s9xcommand_t>	keymap;
static vector<s9xcommand_t *>		multis;
static bool8						FLAG_LATCH = FALSE;
static int32						curcontrollers[2] = { NONE,    NONE };
static int32						newcontrollers[2] = { JOYPAD0, NONE };
static char							buf[256];

static const char	*color_names[32] =
{
	"Trans",
	"Black",
	"25Grey",
	"50Grey",
	"75Grey",
	"White",
	"Red",
	"Orange",
	"Yellow",
	"Green",
	"Cyan",
	"Sky",
	"Blue",
	"Violet",
	"MagicPink",
	"Purple",
	NULL,
	"tBlack",
	"t25Grey",
	"t50Grey",
	"t75Grey",
	"tWhite",
	"tRed",
	"tOrange",
	"tYellow",
	"tGreen",
	"tCyan",
	"tSky",
	"tBlue",
	"tViolet",
	"tMagicPink",
	"tPurple"
};

static void DoGunLatch (int x, int y)
{
	x += 40;

	if (x > 295)
		x = 295;
	else if (x < 40)
		x = 40;

	if (y > PPU.ScreenHeight - 1)
		y = PPU.ScreenHeight - 1;
	else if (y < 0)
		y = 0;

	PPU.GunVLatch = (uint16) (y + 1);
	PPU.GunHLatch = (uint16) x;
}

static void DoMacsRifleLatch (int x, int y)
{
	PPU.GunVLatch = (uint16) (y + 42);// + (int16) macsrifle.adjust_y;
	PPU.GunHLatch = (uint16) (x + 76);// + (int16) macsrifle.adjust_x;
}

static int maptype (int t)
{
	switch (t)
	{
		case S9xNoMapping:
			return (MAP_NONE);

		case S9xButtonMouse:
		case S9xButtonSuperscope:
		case S9xButtonJustifier:
		case S9xButtonMacsRifle:
		case S9xButtonMulti:
			return (MAP_BUTTON);

		case S9xPointer:
			return (MAP_POINTER);

		default:
			return (MAP_UNKNOWN);
	}
}

void S9xControlsReset (void)
{
	S9xControlsSoftReset();
	mouse[0].buttons  &= ~0x30;
	mouse[1].buttons  &= ~0x30;
	justifier.buttons &= ~JUSTIFIER_SELECT;
	macsrifle.buttons = 0;
}

void S9xControlsSoftReset (void)
{
	for (set<struct exemulti *>::iterator it = exemultis.begin(); it != exemultis.end(); it++)
		delete *it;
	exemultis.clear();

	for (int i = 0; i < 2; i++)
		for (int j = 0; j < 2; j++)
			read_idx[i][j]=0;

	FLAG_LATCH = FALSE;

	curcontrollers[0] = newcontrollers[0];
	curcontrollers[1] = newcontrollers[1];
}

/* Raw absolute pad state, written once per frame by the libretro port.
   No edges, no toggles, no per-button command dispatch: the frontend's
   view of the pad IS the pad. The only filtering is the hardware-
   impossible opposing-direction case, honouring Settings.UpAndDown the
   same way S9xApplyCommand used to. */
void S9xSetJoypadButtons (int pad, uint16 buttons)
{
	if (pad < 0 || pad > 7)
		return;

	if (!Settings.UpAndDown)
	{
		if ((buttons & (SNES_LEFT_MASK | SNES_RIGHT_MASK)) == (SNES_LEFT_MASK | SNES_RIGHT_MASK))
			buttons &= ~(SNES_LEFT_MASK | SNES_RIGHT_MASK);
		if ((buttons & (SNES_UP_MASK | SNES_DOWN_MASK)) == (SNES_UP_MASK | SNES_DOWN_MASK))
			buttons &= ~(SNES_UP_MASK | SNES_DOWN_MASK);
	}

	joypad[pad].buttons = buttons;
}

void S9xUnmapAllControls (void)
{
	S9xControlsReset();

	keymap.clear();

	for (int i = 0; i < (int) multis.size(); i++)
		free(multis[i]);
	multis.clear();

	for (int i = 0; i < 8; i++)
		joypad[i].buttons = 0;

	for (int i = 0; i < 2; i++)
	{
		mouse[i].old_x = mouse[i].old_y = 0;
		mouse[i].cur_x = mouse[i].cur_y = 0;
		mouse[i].buttons = 1;
		mouse[i].ID = InvalidControlID;

		if (!(mouse[i].crosshair.set & 1))
			mouse[i].crosshair.img = 0; // no image for mouse because its only logical position is game-specific, not known by the emulator
		if (!(mouse[i].crosshair.set & 2))
			mouse[i].crosshair.fg  = 5;
		if (!(mouse[i].crosshair.set & 4))
			mouse[i].crosshair.bg  = 1;

		justifier.x[i] = justifier.y[i] = 0;
		justifier.offscreen[i] = 0;
		justifier.ID[i] = InvalidControlID;

		if (!(justifier.crosshair[i].set & 1))
			justifier.crosshair[i].img = 4;
		if (!(justifier.crosshair[i].set & 2))
			justifier.crosshair[i].fg  = i ? 14 : 12;
		if (!(justifier.crosshair[i].set & 4))
			justifier.crosshair[i].bg  = 1;
	}

	justifier.buttons = 0;

	superscope.x = superscope.y = 0;
	superscope.phys_buttons = 0;
	superscope.next_buttons = 0;
	superscope.read_buttons = 0;
	superscope.ID = InvalidControlID;

	if (!(superscope.crosshair.set & 1))
		superscope.crosshair.img = 2;
	if (!(superscope.crosshair.set & 2))
		superscope.crosshair.fg  = 5;
	if (!(superscope.crosshair.set & 4))
		superscope.crosshair.bg  = 1;

	macsrifle.x = macsrifle.y = 0;
	macsrifle.buttons = 0;
	macsrifle.ID = InvalidControlID;

	if (!(macsrifle.crosshair.set & 1))
		macsrifle.crosshair.img = 2;
	if (!(macsrifle.crosshair.set & 2))
		macsrifle.crosshair.fg  = 5;
	if (!(macsrifle.crosshair.set & 4))
		macsrifle.crosshair.bg  = 1;


}

void S9xSetController (int port, enum controllers controller, int8 id1, int8 id2, int8 id3, int8 id4)
{
	if (port < 0 || port > 1)
		return;

	switch (controller)
	{
		case CTL_NONE:
			break;

		case CTL_JOYPAD:
			if (id1 < 0 || id1 > 7)
				break;

			newcontrollers[port] = JOYPAD0 + id1;
			return;

		case CTL_MOUSE:
			if (id1 < 0 || id1 > 1)
				break;
			if (!Settings.MouseMaster)
			{
				S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select SNES Mouse: MouseMaster disabled");
				break;
			}

			newcontrollers[port] = MOUSE0 + id1;
			return;

		case CTL_SUPERSCOPE:
			if (!Settings.SuperScopeMaster)
			{
				S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select SNES Superscope: SuperScopeMaster disabled");
				break;
			}

			newcontrollers[port] = SUPERSCOPE;
			return;

		case CTL_JUSTIFIER:
			if (id1 < 0 || id1 > 1)
				break;
			if (!Settings.JustifierMaster)
			{
				S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select Konami Justifier: JustifierMaster disabled");
				break;
			}

			newcontrollers[port] = ONE_JUSTIFIER + id1;
			return;

		case CTL_MACSRIFLE:
			if (!Settings.MacsRifleMaster)
			{
				S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select SNES M.A.C.S. Rifle: MacsRifleMaster disabled");
				break;
			}

			newcontrollers[port] = MACSRIFLE;
			return;

		case CTL_MP5:
			if (id1 < -1 || id1 > 7)
				break;
			if (id2 < -1 || id2 > 7)
				break;
			if (id3 < -1 || id3 > 7)
				break;
			if (id4 < -1 || id4 > 7)
				break;
			if (!Settings.MultiPlayer5Master)
			{
				S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select MP5: MultiPlayer5Master disabled");
				break;
			}

			newcontrollers[port] = MP5;
			mp5[port].pads[0] = (id1 < 0) ? NONE : JOYPAD0 + id1;
			mp5[port].pads[1] = (id2 < 0) ? NONE : JOYPAD0 + id2;
			mp5[port].pads[2] = (id3 < 0) ? NONE : JOYPAD0 + id3;
			mp5[port].pads[3] = (id4 < 0) ? NONE : JOYPAD0 + id4;
			return;

		default:
			fprintf(stderr, "Unknown controller type %d\n", controller);
			break;
	}

	newcontrollers[port] = NONE;
}

bool S9xVerifyControllers (void)
{
	bool	ret = false;
	int		port, i, used[NUMCTLS];

	for (i = 0; i < NUMCTLS; used[i++] = 0) ;

	for (port = 0; port < 2; port++)
	{
		switch (i = newcontrollers[port])
		{
			case MOUSE0:
			case MOUSE1:
				if (!Settings.MouseMaster)
				{
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select SNES Mouse: MouseMaster disabled");
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				if (used[i]++ > 0)
				{
					snprintf(buf, sizeof(buf), "Mouse%d used more than once! Disabling extra instances", i - MOUSE0 + 1);
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, buf);
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				break;

			case SUPERSCOPE:
				if (!Settings.SuperScopeMaster)
				{
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select SNES Superscope: SuperScopeMaster disabled");
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				if (used[i]++ > 0)
				{
					snprintf(buf, sizeof(buf), "Superscope used more than once! Disabling extra instances");
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, buf);
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				break;

			case ONE_JUSTIFIER:
			case TWO_JUSTIFIERS:
				if (!Settings.JustifierMaster)
				{
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select Konami Justifier: JustifierMaster disabled");
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				if (used[ONE_JUSTIFIER]++ > 0)
				{
					snprintf(buf, sizeof(buf), "Justifier used more than once! Disabling extra instances");
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, buf);
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				break;

			case MACSRIFLE:
				if (!Settings.MacsRifleMaster)
				{
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select SNES M.A.C.S. Rifle: MacsRifleMaster disabled");
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				if (used[i]++ > 0)
				{
					snprintf(buf, sizeof(buf), "M.A.C.S. Rifle used more than once! Disabling extra instances");
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, buf);
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				break;

			case MP5:
				if (!Settings.MultiPlayer5Master)
				{
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, "Cannot select MP5: MultiPlayer5Master disabled");
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				for (i = 0; i < 4; i++)
				{
					if (mp5[port].pads[i] != NONE)
					{
						if (used[mp5[port].pads[i] - JOYPAD0]++ > 0)
						{
							snprintf(buf, sizeof(buf), "Joypad%d used more than once! Disabling extra instances", mp5[port].pads[i] - JOYPAD0 + 1);
							S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, buf);
							mp5[port].pads[i] = NONE;
							ret = true;
							break;
						}
					}
				}

				break;

			case JOYPAD0:
			case JOYPAD1:
			case JOYPAD2:
			case JOYPAD3:
			case JOYPAD4:
			case JOYPAD5:
			case JOYPAD6:
			case JOYPAD7:
				if (used[i - JOYPAD0]++ > 0)
				{
					snprintf(buf, sizeof(buf), "Joypad%d used more than once! Disabling extra instances", i - JOYPAD0 + 1);
					S9xMessage(S9X_CONFIG_INFO, S9X_ERROR, buf);
					newcontrollers[port] = NONE;
					ret = true;
					break;
				}

				break;

			default:
				break;
		}
	}

	return (ret);
}

s9xcommand_t S9xGetCommandT (const char *name)
{
	s9xcommand_t	cmd;
	int				i, j;
	const char		*s;

	memset(&cmd, 0, sizeof(cmd));
	cmd.type         = S9xBadMapping;
	cmd.multi_press  = 0;

	if (!strcmp(name, "None"))
		cmd.type = S9xNoMapping;
	else
	if (!strncmp(name, "Mouse", 5))
	{
		if (name[5] < '1' || name[5] > '2' || name[6] != ' ')
			return (cmd);

		cmd.button.mouse.idx = name[5] - '1';
		s = name + 7;
		i = 0;

		if ((cmd.button.mouse.left  = (*s == 'L')))	s += i = 1;
		if ((cmd.button.mouse.right = (*s == 'R')))	s += i = 1;

		if (i == 0 || *s != 0)
			return (cmd);

		cmd.type = S9xButtonMouse;
	}
	else
	if (!strncmp(name, "Superscope ", 11))
	{
		s = name + 11;
		i = 0;

		if ((cmd.button.scope.aim_offscreen     = strncmp(s, "AimOffscreen", 12) ? 0 : 1))	{ s += i = 12; if (*s == ' ') s++; else if (*s != 0) return (cmd); }
		if ((cmd.button.scope.fire              = strncmp(s, "Fire",          4) ? 0 : 1))	{ s += i =  4; if (*s == '+') s++; }
		if ((cmd.button.scope.cursor            = strncmp(s, "Cursor",        6) ? 0 : 1))	{ s += i =  6; if (*s == '+') s++; }
		if ((cmd.button.scope.turbo             = strncmp(s, "ToggleTurbo",  11) ? 0 : 1))	{ s += i = 11; if (*s == '+') s++; }
		if ((cmd.button.scope.pause             = strncmp(s, "Pause",         5) ? 0 : 1))	{ s += i =  5; }

		if (i == 0 || *s != 0 || *(s - 1) == '+')
			return (cmd);

		cmd.type = S9xButtonSuperscope;
	}
	else
	if (!strncmp(name, "Justifier", 9))
	{
		if (name[9] < '1' || name[9] > '2' || name[10] != ' ')
			return (cmd);

		cmd.button.justifier.idx = name[9] - '1';
		s = name + 11;
		i = 0;

		if ((cmd.button.justifier.aim_offscreen = strncmp(s, "AimOffscreen", 12) ? 0 : 1))	{ s += i = 12; if (*s == ' ') s++; else if (*s != 0) return (cmd); }
		if ((cmd.button.justifier.trigger       = strncmp(s, "Trigger",       7) ? 0 : 1))	{ s += i =  7; if (*s == '+') s++; }
		if ((cmd.button.justifier.start         = strncmp(s, "Start",         5) ? 0 : 1))	{ s += i =  5; }

		if (i == 0 || *s != 0 || *(s - 1) == '+')
			return (cmd);

		cmd.type = S9xButtonJustifier;
	}
	else
	if (!strncmp(name, "MacsRifle ", 10))
	{
		s = name + 10;
		i = 0;

		if ((cmd.button.macsrifle.trigger = strncmp(s, "Trigger", 7) ? 0 : 1))	{ s += i =  7; }

		if (i == 0 || *s != 0 || *(s - 1) == '+')
			return (cmd);

		cmd.type = S9xButtonMacsRifle;
	}
	else
	if (!strncmp(name, "Pointer ", 8))
	{
		s = name + 8;
		i = 0;

		if ((cmd.pointer.aim_mouse0     = strncmp(s, "Mouse1",      6) ? 0 : 1))	{ s += i =  6; if (*s == '+') s++; }
		if ((cmd.pointer.aim_mouse1     = strncmp(s, "Mouse2",      6) ? 0 : 1))	{ s += i =  6; if (*s == '+') s++; }
		if ((cmd.pointer.aim_scope      = strncmp(s, "Superscope", 10) ? 0 : 1))	{ s += i = 10; if (*s == '+') s++; }
		if ((cmd.pointer.aim_justifier0 = strncmp(s, "Justifier1", 10) ? 0 : 1))	{ s += i = 10; if (*s == '+') s++; }
		if ((cmd.pointer.aim_justifier1 = strncmp(s, "Justifier2", 10) ? 0 : 1))	{ s += i = 10; if (*s == '+') s++; }
		if ((cmd.pointer.aim_macsrifle  = strncmp(s, "MacsRifle",   9) ? 0 : 1))	{ s += i =  9; }

		if (i == 0 || *s != 0 || *(s - 1) == '+')
			return (cmd);

		cmd.type = S9xPointer;
	}
	else
	if (!strncmp(name, "MULTI#", 6))
	{
		i = strtol(name + 6, (char **) &s, 10);
		if (s != NULL && *s != '\0')
			return (cmd);
		if (i >= (int) multis.size())
			return (cmd);

		cmd.button.multi_idx = i;
		cmd.type = S9xButtonMulti;
	}
	else
	if (((name[0] == '+' && name[1] == '{') || name[0] == '{') && name[strlen(name) - 1] == '}')
	{
		if (multis.size() > 2147483640)
		{
			fprintf(stderr, "Too many multis!");
			return (cmd);
		}

		string	x;
		int		n;

		j = 2;
		for (i = (name[0] == '+') ? 2 : 1; name[i] != '\0'; i++)
		{
			if (name[i] == ',' || name[i] == ';')
			{
				if (name[i] == ';')
					j++;
				if (++j > 2147483640)
				{
					fprintf(stderr, "Multi too long!");
					return (cmd);
				}
			}

			if (name[i] == '{')
				return (cmd);
		}

		s9xcommand_t	*c = (s9xcommand_t *) calloc(j, sizeof(s9xcommand_t));
		if (c == NULL)
		{
			perror("malloc error while parsing multi");
			return (cmd);
		}

		n = 0;
		i = (name[0] == '+') ? 2 : 1;

		do
		{
			if (name[i] == ';')
			{
				c[n].type         = S9xNoMapping;
				c[n].multi_press  = 0;

				j = i;
			}
			else if (name[i] == ',')
			{
				free(c);
				return (cmd);
			}
			else
			{
				uint8	press = 0;

				if (name[0] == '+')
				{
					if (name[i] == '+')
						press = 1;
					else if (name[i] == '-')
						press = 2;
					else
					{
						free(c);
						return (cmd);
					}

					i++;
				}

				for (j = i; name[j] != ';' && name[j] != ',' && name[j] != '}'; j++) ;

				x.assign(name + i, j - i);
				c[n] = S9xGetCommandT(x.c_str());
				c[n].multi_press = press;

				if (maptype(c[n].type) != MAP_BUTTON)
				{
					free(c);
					return (cmd);
				}

				if (name[j] == ';')
					j--;
			}

			i = j + 1;
			n++;
		}
		while (name[i] != '\0');

		c[n].type        = S9xNoMapping;
		c[n].multi_press = 3;

		multis.push_back(c);

		cmd.button.multi_idx = multis.size() - 1;
		cmd.type = S9xButtonMulti;
	}

	return (cmd);
}

static s9xcommand_t S9xGetMapping (uint32 id)
{
	if (keymap.count(id) == 0)
	{
		s9xcommand_t	cmd;
		cmd.type = S9xNoMapping;
		return (cmd);
	}
	else
		return (keymap[id]);
}

static const char * maptypename (int t)
{
	switch (t)
	{
		case MAP_NONE:		return ("unmapped");
		case MAP_BUTTON:	return ("button");
		case MAP_POINTER:	return ("pointer");
		default:			return ("unknown");
	}
}

void S9xUnmapID (uint32 id)
{
	if (mouse[0].ID     == id)	mouse[0].ID     = InvalidControlID;
	if (mouse[1].ID     == id)	mouse[1].ID     = InvalidControlID;
	if (superscope.ID   == id)	superscope.ID   = InvalidControlID;
	if (justifier.ID[0] == id)	justifier.ID[0] = InvalidControlID;
	if (justifier.ID[1] == id)	justifier.ID[1] = InvalidControlID;
	if (macsrifle.ID    == id)	macsrifle.ID    = InvalidControlID;

	keymap.erase(id);
}

bool S9xMapButton (uint32 id, s9xcommand_t mapping)
{
	int	t;

	if (id == InvalidControlID)
	{
		fprintf(stderr, "Cannot map InvalidControlID\n");
		return (false);
	}

	t = maptype(mapping.type);

	if (t == MAP_NONE)
	{
		S9xUnmapID(id);
		return (true);
	}

	if (t != MAP_BUTTON)
		return (false);

	t = maptype(S9xGetMapping(id).type);

	if (t != MAP_NONE && t != MAP_BUTTON)
		fprintf(stderr, "WARNING: Remapping ID 0x%08x from %s to button\n", id, maptypename(t));

	S9xUnmapID(id);

	keymap[id] = mapping;

	return (true);
}

void S9xReportButton (uint32 id, bool pressed)
{
	if (keymap.count(id) == 0)
		return;

	if (keymap[id].type == S9xNoMapping)
		return;

	if (maptype(keymap[id].type) != MAP_BUTTON)
	{
		fprintf(stderr, "ERROR: S9xReportButton called on %s ID 0x%08x\n", maptypename(maptype(keymap[id].type)), id);
		return;
	}

	S9xApplyCommand(keymap[id], pressed, 0);
}

bool S9xMapPointer (uint32 id, s9xcommand_t mapping)
{
	int	t;

	if (id == InvalidControlID)
	{
		fprintf(stderr, "Cannot map InvalidControlID\n");
		return (false);
	}

	t = maptype(mapping.type);

	if (t == MAP_NONE)
	{
		S9xUnmapID(id);
		return (true);
	}

	if (t != MAP_POINTER)
		return (false);

	t = maptype(S9xGetMapping(id).type);

	if (t != MAP_NONE && t != MAP_POINTER)
		fprintf(stderr, "WARNING: Remapping ID 0x%08x from %s to pointer\n", id, maptypename(t));

	if (mapping.type == S9xPointer)
	{
		if (mapping.pointer.aim_mouse0 && mouse[0].ID != InvalidControlID && mouse[0].ID != id)
		{
			fprintf(stderr, "ERROR: Rejecting attempt to control Mouse1 with two pointers\n");
			return (false);
		}

		if (mapping.pointer.aim_mouse1 && mouse[1].ID != InvalidControlID && mouse[1].ID != id)
		{
			fprintf(stderr, "ERROR: Rejecting attempt to control Mouse2 with two pointers\n");
			return (false);
		}

		if (mapping.pointer.aim_scope && superscope.ID != InvalidControlID && superscope.ID != id)
		{
			fprintf(stderr, "ERROR: Rejecting attempt to control SuperScope with two pointers\n");
			return (false);
		}

		if (mapping.pointer.aim_justifier0 && justifier.ID[0] != InvalidControlID && justifier.ID[0] != id)
		{
			fprintf(stderr, "ERROR: Rejecting attempt to control Justifier1 with two pointers\n");
			return (false);
		}

		if (mapping.pointer.aim_justifier1 && justifier.ID[1] != InvalidControlID && justifier.ID[1] != id)
		{
			fprintf(stderr, "ERROR: Rejecting attempt to control Justifier2 with two pointers\n");
			return (false);
		}

		if (mapping.pointer.aim_macsrifle && macsrifle.ID != InvalidControlID && macsrifle.ID != id)
		{
			fprintf(stderr, "ERROR: Rejecting attempt to control M.A.C.S. Rifle with two pointers\n");
			return (false);
		}
	}

	S9xUnmapID(id);

	keymap[id] = mapping;

	if (mapping.pointer.aim_mouse0    )	mouse[0].ID     = id;
	if (mapping.pointer.aim_mouse1    )	mouse[1].ID     = id;
	if (mapping.pointer.aim_scope     )	superscope.ID   = id;
	if (mapping.pointer.aim_justifier0)	justifier.ID[0] = id;
	if (mapping.pointer.aim_justifier1)	justifier.ID[1] = id;
	if (mapping.pointer.aim_macsrifle )	macsrifle.ID    = id;

	return (true);
}

void S9xReportPointer (uint32 id, int16 x, int16 y)
{
	if (keymap.count(id) == 0)
		return;

	if (keymap[id].type == S9xNoMapping)
		return;

	if (maptype(keymap[id].type) != MAP_POINTER)
	{
		fprintf(stderr, "ERROR: S9xReportPointer called on %s ID 0x%08x\n", maptypename(maptype(keymap[id].type)), id);
		return;
	}

	S9xApplyCommand(keymap[id], x, y);
}

static int32 ApplyMulti (s9xcommand_t *multi, int32 pos, int16 data1)
{
	while (1)
	{
		if (multi[pos].multi_press == 3)
			return (-1);

		if (multi[pos].type == S9xNoMapping)
			break;

		if (multi[pos].multi_press)
			S9xApplyCommand(multi[pos], multi[pos].multi_press == 1, 0);
		else
			S9xApplyCommand(multi[pos], data1, 0);

		pos++;
	}

	return (pos + 1);
}

void S9xApplyCommand (s9xcommand_t cmd, int16 data1, int16 data2)
{
	int	i;

	switch (cmd.type)
	{
		case S9xNoMapping:
			return;

		case S9xButtonMouse:
			i = 0;
			if (cmd.button.mouse.left )	i |= 0x40;
			if (cmd.button.mouse.right)	i |= 0x80;

			if (data1)
				mouse[cmd.button.mouse.idx].buttons |=  i;
			else
				mouse[cmd.button.mouse.idx].buttons &= ~i;

			return;

		case S9xButtonSuperscope:
			i = 0;
			if (cmd.button.scope.fire         )	i |= SUPERSCOPE_FIRE;
			if (cmd.button.scope.cursor       )	i |= SUPERSCOPE_CURSOR;
			if (cmd.button.scope.pause        )	i |= SUPERSCOPE_PAUSE;
			if (cmd.button.scope.aim_offscreen)	i |= SUPERSCOPE_OFFSCREEN;

			if (data1)
			{
				superscope.phys_buttons |= i;

				if (cmd.button.scope.turbo)
				{
					superscope.phys_buttons ^= SUPERSCOPE_TURBO;

					if (superscope.phys_buttons & SUPERSCOPE_TURBO)
						superscope.next_buttons |= superscope.phys_buttons & (SUPERSCOPE_FIRE | SUPERSCOPE_CURSOR);
					else
						superscope.next_buttons &= ~(SUPERSCOPE_FIRE | SUPERSCOPE_CURSOR);
				}

				superscope.next_buttons |= i & (SUPERSCOPE_FIRE | SUPERSCOPE_CURSOR | SUPERSCOPE_PAUSE);

					if ((superscope.next_buttons & (SUPERSCOPE_FIRE | SUPERSCOPE_CURSOR)) && curcontrollers[1] == SUPERSCOPE && !(superscope.phys_buttons & SUPERSCOPE_OFFSCREEN))
						DoGunLatch(superscope.x, superscope.y);
			}
			else
			{
				superscope.phys_buttons &= ~i;
				superscope.next_buttons &= SUPERSCOPE_OFFSCREEN | ~i;
			}

			return;

		case S9xButtonJustifier:
			i = 0;
			if (cmd.button.justifier.trigger)	i |= JUSTIFIER_TRIGGER;
			if (cmd.button.justifier.start  )	i |= JUSTIFIER_START;
			if (cmd.button.justifier.aim_offscreen)	justifier.offscreen[cmd.button.justifier.idx] = data1 ? 1 : 0;
			i >>= cmd.button.justifier.idx;

			if (data1)
				justifier.buttons |=  i;
			else
				justifier.buttons &= ~i;

			return;

		case S9xButtonMacsRifle:
			i = 0;
			if (cmd.button.macsrifle.trigger) i |= MACSRIFLE_TRIGGER;

			if(data1)
				macsrifle.buttons |= i;
			else
				macsrifle.buttons &= ~i;

			return;

		case S9xPointer:
			if (cmd.pointer.aim_mouse0)
			{
				mouse[0].cur_x = data1;
				mouse[0].cur_y = data2;
			}

			if (cmd.pointer.aim_mouse1)
			{
				mouse[1].cur_x = data1;
				mouse[1].cur_y = data2;
			}

			if (cmd.pointer.aim_scope)
			{
				superscope.x   = data1;
				superscope.y   = data2;
			}

			if (cmd.pointer.aim_justifier0)
			{
				justifier.x[0] = data1;
				justifier.y[0] = data2;
			}

			if (cmd.pointer.aim_justifier1)
			{
				justifier.x[1] = data1;
				justifier.y[1] = data2;
			}

			if (cmd.pointer.aim_macsrifle)
			{
				macsrifle.x = data1;
				macsrifle.y = data2;
			}

			return;

		case S9xButtonMulti:
			if (cmd.button.multi_idx >= (int) multis.size())
				return;

			if (multis[cmd.button.multi_idx]->multi_press && !data1)
				return;

			i = ApplyMulti(multis[cmd.button.multi_idx], 0, data1);
			if (i >= 0)
			{
				struct exemulti	*e = new struct exemulti;
				e->pos    = i;
				e->data1  = data1 != 0;
				e->script = multis[cmd.button.multi_idx];
				exemultis.insert(e);
			}

			return;

		default:
			fprintf(stderr, "WARNING: Unknown command type %d\n", cmd.type);
			return;
	}
}

static void UpdateMouseDelta (int i)
{
	int16	j;

	j = mouse[i - MOUSE0].cur_x - mouse[i - MOUSE0].old_x;

	if (j < -127)
	{
		mouse[i - MOUSE0].delta_x = 0xff;
		mouse[i - MOUSE0].old_x -= 127;
	}
	else if (j < 0)
	{
		mouse[i - MOUSE0].delta_x = 0x80 | -j;
		mouse[i - MOUSE0].old_x = mouse[i - MOUSE0].cur_x;
	}
	else if (j > 127)
	{
		mouse[i - MOUSE0].delta_x = 0x7f;
		mouse[i - MOUSE0].old_x += 127;
	}
	else
	{
		mouse[i - MOUSE0].delta_x = (uint8) j;
		mouse[i - MOUSE0].old_x = mouse[i - MOUSE0].cur_x;
	}

	j = mouse[i - MOUSE0].cur_y - mouse[i - MOUSE0].old_y;

	if (j < -127)
	{
		mouse[i - MOUSE0].delta_y = 0xff;
		mouse[i - MOUSE0].old_y -= 127;
	}
	else if (j < 0)
	{
		mouse[i - MOUSE0].delta_y = 0x80 | -j;
		mouse[i - MOUSE0].old_y = mouse[i - MOUSE0].cur_y;
	}
	else if (j > 127)
	{
		mouse[i - MOUSE0].delta_y = 0x7f;
		mouse[i - MOUSE0].old_y += 127;
	}
	else
	{
		mouse[i - MOUSE0].delta_y = (uint8) j;
		mouse[i - MOUSE0].old_y = mouse[i - MOUSE0].cur_y;
	}
}

void S9xSetJoypadLatch (bool latch)
{
	if (!latch && FLAG_LATCH)
	{
		// 1 written, 'plug in' new controllers now
		curcontrollers[0] = newcontrollers[0];
		curcontrollers[1] = newcontrollers[1];
	}

	if (latch && !FLAG_LATCH)
	{
		int	i;

		for (int n = 0; n < 2; n++)
		{
			for (int j = 0; j < 2; j++)
				read_idx[n][j] = 0;

			switch (i = curcontrollers[n])
			{
				case MP5:
					for (int j = 0, k; j < 4; ++j)
					{
						k = mp5[n].pads[j];
						if (k == NONE)
							continue;
					}

					break;

				case JOYPAD0:
				case JOYPAD1:
				case JOYPAD2:
				case JOYPAD3:
				case JOYPAD4:
				case JOYPAD5:
				case JOYPAD6:
				case JOYPAD7:
					break;

				case MOUSE0:
				case MOUSE1:
					UpdateMouseDelta(i);
					break;

				case SUPERSCOPE:
					if (superscope.next_buttons & SUPERSCOPE_FIRE)
					{
						superscope.next_buttons &= ~SUPERSCOPE_TURBO;
						superscope.next_buttons |= superscope.phys_buttons & SUPERSCOPE_TURBO;
					}

					if (superscope.next_buttons & (SUPERSCOPE_FIRE | SUPERSCOPE_CURSOR))
					{
						superscope.next_buttons &= ~SUPERSCOPE_OFFSCREEN;
						superscope.next_buttons |= superscope.phys_buttons & SUPERSCOPE_OFFSCREEN;
					}

					superscope.read_buttons = superscope.next_buttons;

					superscope.next_buttons &= ~SUPERSCOPE_PAUSE;
					if (!(superscope.phys_buttons & SUPERSCOPE_TURBO))
						superscope.next_buttons &= ~(SUPERSCOPE_CURSOR | SUPERSCOPE_FIRE);

					break;

				case TWO_JUSTIFIERS:
					// fall through

				case ONE_JUSTIFIER:
					justifier.buttons ^= JUSTIFIER_SELECT;
					break;

				case MACSRIFLE:
					break;

				default:
					break;
			}
		}
	}

	FLAG_LATCH = latch;
}

// prevent read_idx from overflowing (only latching resets it)
// otherwise $4016/7 reads will start returning input data again
static inline uint8 IncreaseReadIdxPost(uint8 &var)
{
	uint8 oldval = var;
	if (var < 255)
		var++;
	return oldval;
}

uint8 S9xReadJOYSERn (int n)
{
	int	i, j, r;

	if (n > 1)
		n -= 0x4016;
	assert(n == 0 || n == 1);

	uint8	bits = (OpenBus & ~3) | ((n == 1) ? 0x1c : 0);

	if (FLAG_LATCH)
	{
		switch (i = curcontrollers[n])
		{
			case MP5:
				return (bits | 2);

			case JOYPAD0:
			case JOYPAD1:
			case JOYPAD2:
			case JOYPAD3:
			case JOYPAD4:
			case JOYPAD5:
			case JOYPAD6:
			case JOYPAD7:
				return (bits | ((joypad[i - JOYPAD0].buttons & 0x8000) ? 1 : 0));

			case MOUSE0:
			case MOUSE1:
				mouse[i - MOUSE0].buttons += 0x10;
				if ((mouse[i - MOUSE0].buttons & 0x30) == 0x30)
					mouse[i - MOUSE0].buttons &= 0xcf;
				return (bits);

			case SUPERSCOPE:
				return (bits | ((superscope.read_buttons & 0x80) ? 1 : 0));

			case ONE_JUSTIFIER:
			case TWO_JUSTIFIERS:
				return (bits);

			case MACSRIFLE:
				return (bits | ((macsrifle.buttons & 0x01) ? 1 : 0));

			default:
				return (bits);
		}
	}
	else
	{
		switch (i = curcontrollers[n])
		{
			case MP5:
				r = IncreaseReadIdxPost(read_idx[n][FLAG_IOBIT(n) ? 0 : 1]);
				j = FLAG_IOBIT(n) ? 0 : 2;

				for (i = 0; i < 2; i++, j++)
				{
					if (mp5[n].pads[j] == NONE)
						continue;
					if (r >= 16)
						bits |= 1 << i;
					else
						bits |= ((joypad[mp5[n].pads[j] - JOYPAD0].buttons & (0x8000 >> r)) ? 1 : 0) << i;
				}

				return (bits);

			case JOYPAD0:
			case JOYPAD1:
			case JOYPAD2:
			case JOYPAD3:
			case JOYPAD4:
			case JOYPAD5:
			case JOYPAD6:
			case JOYPAD7:
				if (read_idx[n][0] >= 16)
				{
					IncreaseReadIdxPost(read_idx[n][0]);
					return (bits | 1);
				}
				else
					return (bits | ((joypad[i - JOYPAD0].buttons & (0x8000 >> IncreaseReadIdxPost(read_idx[n][0]))) ? 1 : 0));

			case MOUSE0:
			case MOUSE1:
				if (read_idx[n][0] < 8)
				{
					IncreaseReadIdxPost(read_idx[n][0]);
					return (bits);
				}
				else
				if (read_idx[n][0] < 16)
					return (bits | ((mouse[i - MOUSE0].buttons & (0x8000     >> IncreaseReadIdxPost(read_idx[n][0]))) ? 1 : 0));
				else
				if (read_idx[n][0] < 24)
					return (bits | ((mouse[i - MOUSE0].delta_y & (0x800000   >> IncreaseReadIdxPost(read_idx[n][0]))) ? 1 : 0));
				else
				if (read_idx[n][0] < 32)
					return (bits | ((mouse[i - MOUSE0].delta_x & (0x80000000 >> IncreaseReadIdxPost(read_idx[n][0]))) ? 1 : 0));
				else
				{
					IncreaseReadIdxPost(read_idx[n][0]);
					return (bits | 1);
				}

			case SUPERSCOPE:
				if (read_idx[n][0] < 8)
					return (bits | ((superscope.read_buttons & (0x80 >> IncreaseReadIdxPost(read_idx[n][0]))) ? 1 : 0));
				else
				{
					IncreaseReadIdxPost(read_idx[n][0]);
					return (bits | 1);
				}

			case ONE_JUSTIFIER:
				if (read_idx[n][0] < 24)
					return (bits | ((0xaa7000 >> IncreaseReadIdxPost(read_idx[n][0])) & 1));
				else
				if (read_idx[n][0] < 32)
					return (bits | ((justifier.buttons & (JUSTIFIER_TRIGGER | JUSTIFIER_START | JUSTIFIER_SELECT) & (0x80000000 >> IncreaseReadIdxPost(read_idx[n][0]))) ? 1 : 0));
				else
				{
					IncreaseReadIdxPost(read_idx[n][0]);
					return (bits | 1);
				}

			case TWO_JUSTIFIERS:
				if (read_idx[n][0] < 24)
					return (bits | ((0xaa7000 >> IncreaseReadIdxPost(read_idx[n][0])) & 1));
				else
				if (read_idx[n][0] < 32)
					return (bits | ((justifier.buttons & (0x80000000 >> IncreaseReadIdxPost(read_idx[n][0]))) ? 1 : 0));
				else
				{
					IncreaseReadIdxPost(read_idx[n][0]);
					return (bits | 1);
				}

			case MACSRIFLE:
				return (bits | ((macsrifle.buttons & 0x01) ? 1 : 0));

			default:
				IncreaseReadIdxPost(read_idx[n][0]);
				return (bits);
		}
	}
}

void S9xDoAutoJoypad (void)
{
	int	i, j;

	S9xSetJoypadLatch(1);
	S9xSetJoypadLatch(0);

	for (int n = 0; n < 2; n++)
	{
		switch (i = curcontrollers[n])
		{
			case MP5:
				j = FLAG_IOBIT(n) ? 0 : 2;
				for (i = 0; i < 2; i++, j++)
				{
					if (mp5[n].pads[j] == NONE)
						WRITE_WORD(Memory.FillRAM + 0x4218 + n * 2 + i * 4, 0);
					else
						WRITE_WORD(Memory.FillRAM + 0x4218 + n * 2 + i * 4, joypad[mp5[n].pads[j] - JOYPAD0].buttons);
				}

				read_idx[n][FLAG_IOBIT(n) ? 0 : 1] = 16;
				break;

			case JOYPAD0:
			case JOYPAD1:
			case JOYPAD2:
			case JOYPAD3:
			case JOYPAD4:
			case JOYPAD5:
			case JOYPAD6:
			case JOYPAD7:
				read_idx[n][0] = 16;
				WRITE_WORD(Memory.FillRAM + 0x4218 + n * 2, joypad[i - JOYPAD0].buttons);
				WRITE_WORD(Memory.FillRAM + 0x421c + n * 2, 0);
				break;

			case MOUSE0:
			case MOUSE1:
				read_idx[n][0] = 16;
				WRITE_WORD(Memory.FillRAM + 0x4218 + n * 2, mouse[i - MOUSE0].buttons);
				WRITE_WORD(Memory.FillRAM + 0x421c + n * 2, 0);
				break;

			case SUPERSCOPE:
				read_idx[n][0] = 16;
				Memory.FillRAM[0x4218 + n * 2] = 0xff;
				Memory.FillRAM[0x4219 + n * 2] = superscope.read_buttons;
				WRITE_WORD(Memory.FillRAM + 0x421c + n * 2, 0);
				break;

			case ONE_JUSTIFIER:
			case TWO_JUSTIFIERS:
				read_idx[n][0] = 16;
				WRITE_WORD(Memory.FillRAM + 0x4218 + n * 2, 0x000e);
				WRITE_WORD(Memory.FillRAM + 0x421c + n * 2, 0);
				break;

			case MACSRIFLE:
				read_idx[n][0] = 16;
				Memory.FillRAM[0x4218 + n * 2] = 0xff;
				Memory.FillRAM[0x4219 + n * 2] = macsrifle.buttons;
				WRITE_WORD(Memory.FillRAM + 0x421c + n * 2, 0);
				break;

			default:
				WRITE_WORD(Memory.FillRAM + 0x4218 + n * 2, 0);
				WRITE_WORD(Memory.FillRAM + 0x421c + n * 2, 0);
				break;
		}
	}
}

void S9xControlEOF (void)
{
	struct crosshair	*c;
	int					i;

	PPU.GunVLatch = 1000; // i.e., never latch
	PPU.GunHLatch = 0;

	for (int n = 0; n < 2; n++)
	{
		switch (i = curcontrollers[n])
		{
			case MP5:
			case JOYPAD0:
			case JOYPAD1:
			case JOYPAD2:
			case JOYPAD3:
			case JOYPAD4:
			case JOYPAD5:
			case JOYPAD6:
			case JOYPAD7:
				break;

			case MOUSE0:
			case MOUSE1:
				c = &mouse[i - MOUSE0].crosshair;
				if (IPPU.RenderThisFrame)
					S9xDrawCrosshair(S9xGetCrosshair(c->img), c->fg, c->bg, mouse[i - MOUSE0].cur_x, mouse[i - MOUSE0].cur_y);
				break;

			case SUPERSCOPE:
				if (n == 1 && !(superscope.phys_buttons & SUPERSCOPE_OFFSCREEN))
				{
					if (superscope.next_buttons & (SUPERSCOPE_FIRE | SUPERSCOPE_CURSOR))
						DoGunLatch(superscope.x, superscope.y);

					c = &superscope.crosshair;
					if (IPPU.RenderThisFrame)
						S9xDrawCrosshair(S9xGetCrosshair(c->img), c->fg, c->bg, superscope.x, superscope.y);
				}

				break;

			case TWO_JUSTIFIERS:
				if (n == 1 && !justifier.offscreen[1])
				{
					c = &justifier.crosshair[1];
					if (IPPU.RenderThisFrame)
						S9xDrawCrosshair(S9xGetCrosshair(c->img), c->fg, c->bg, justifier.x[1], justifier.y[1]);
				}

				i = (justifier.buttons & JUSTIFIER_SELECT) ?  1 : 0;
				goto do_justifier;

			case ONE_JUSTIFIER:
				i = (justifier.buttons & JUSTIFIER_SELECT) ? -1 : 0;

			do_justifier:
				if (n == 1)
				{
					if (i >= 0 && !justifier.offscreen[i])
						DoGunLatch(justifier.x[i], justifier.y[i]);

					if (!justifier.offscreen[0])
					{
						c = &justifier.crosshair[0];
						if (IPPU.RenderThisFrame)
							S9xDrawCrosshair(S9xGetCrosshair(c->img), c->fg, c->bg, justifier.x[0], justifier.y[0]);
					}
				}

				break;

			case MACSRIFLE:
				if (n == 1)
				{
					DoMacsRifleLatch(macsrifle.x, macsrifle.y);

					c = &macsrifle.crosshair;
					if (IPPU.RenderThisFrame)
						S9xDrawCrosshair(S9xGetCrosshair(c->img), c->fg, c->bg, macsrifle.x, macsrifle.y);
				}

				break;

			default:
				break;
		}
	}

	set<struct exemulti *>::iterator	it, jt;

	for (it = exemultis.begin(); it != exemultis.end(); it++)
	{
		i = ApplyMulti((*it)->script, (*it)->pos, (*it)->data1);

		if (i >= 0)
			(*it)->pos = i;
		else
		{
			jt = it;
			it--;
			delete *jt;
			exemultis.erase(jt);
		}
	}


	pad_read_last = pad_read;
	pad_read      = false;
}

void S9xSetControllerCrosshair (enum crosscontrols ctl, int8 idx, const char *fg, const char *bg)
{
	struct crosshair	*c;
	int8				fgcolor = -1, bgcolor = -1;
	int					i, j;

	if (idx < -1 || idx > 31)
	{
		fprintf(stderr, "S9xSetControllerCrosshair() called with invalid index\n");
		return;
	}

	switch (ctl)
	{
		case X_MOUSE1:		c = &mouse[0].crosshair;		break;
		case X_MOUSE2:		c = &mouse[1].crosshair;		break;
		case X_SUPERSCOPE:	c = &superscope.crosshair;		break;
		case X_JUSTIFIER1:	c = &justifier.crosshair[0];	break;
		case X_JUSTIFIER2:	c = &justifier.crosshair[1];	break;
		case X_MACSRIFLE:	c = &macsrifle.crosshair;		break;
		default:
			fprintf(stderr, "S9xSetControllerCrosshair() called with an invalid controller ID %d\n", ctl);
			return;
	}

	if (fg)
	{
		fgcolor = 0;
		if (*fg == 't')
		{
			fg++;
			fgcolor = 16;
		}

		for (i = 0; i < 16; i++)
		{
			for (j = 0; color_names[i][j] && fg[j] == color_names[i][j]; j++) ;

			if (isalnum(fg[j]))
				continue;

			if (!color_names[i][j])
				break;
		}

		fgcolor |= i;
		if (i > 15 || fgcolor == 16)
		{
			fprintf(stderr, "S9xSetControllerCrosshair() called with invalid fgcolor\n");
			return;
		}
	}

	if (bg)
	{
		bgcolor = 0;
		if (*bg == 't')
		{
			bg++;
			bgcolor = 16;
		}

		for (i = 0; i < 16; i++)
		{
			for (j = 0; color_names[i][j] && bg[j] == color_names[i][j]; j++) ;

			if (isalnum(bg[j]))
				continue;

			if (!color_names[i][j])
				break;
		}

		bgcolor |= i;
		if (i > 15 || bgcolor == 16)
		{
			fprintf(stderr, "S9xSetControllerCrosshair() called with invalid bgcolor\n");
			return;
		}
	}

	if (idx != -1)
	{
		c->set |= 1;
		c->img = idx;
	}

	if (fgcolor != -1)
	{
		c->set |= 2;
		c->fg = fgcolor;
	}

	if (bgcolor != -1)
	{
		c->set |= 4;
		c->bg = bgcolor;
	}
}

void S9xControlPreSaveState (struct SControlSnapshot *s)
{
	memset(s, 0, sizeof(*s));
	s->ver = 4;

	for (int j = 0; j < 2; j++)
	{
		s->port1_read_idx[j] = read_idx[0][j];
		s->port2_read_idx[j] = read_idx[1][j];
	}

	for (int j = 0; j < 2; j++)
		s->mouse_speed[j] = (mouse[j].buttons & 0x30) >> 4;

	s->justifier_select = ((justifier.buttons & JUSTIFIER_SELECT) ? 1 : 0);

#define COPY(x)	{ memcpy((char *) s->internal + i, &(x), sizeof(x)); i += sizeof(x); }

	int	i = 0;

	for (int j = 0; j < 8; j++)
		COPY(joypad[j].buttons);

	for (int j = 0; j < 2; j++)
	{
		COPY(mouse[j].delta_x);
		COPY(mouse[j].delta_y);
		COPY(mouse[j].old_x);
		COPY(mouse[j].old_y);
		COPY(mouse[j].cur_x);
		COPY(mouse[j].cur_y);
		COPY(mouse[j].buttons);
	}

	COPY(superscope.x);
	COPY(superscope.y);
	COPY(superscope.phys_buttons);
	COPY(superscope.next_buttons);
	COPY(superscope.read_buttons);

	for (int j = 0; j < 2; j++)
		COPY(justifier.x[j]);
	for (int j = 0; j < 2; j++)
		COPY(justifier.y[j]);
	COPY(justifier.buttons);
	for (int j = 0; j < 2; j++)
		COPY(justifier.offscreen[j]);

	for (int j = 0; j < 2; j++)
		for (int k = 0; k < 2; k++)
			COPY(mp5[j].pads[k]);

	assert(i == sizeof(s->internal));

	#undef COPY
	#define COPY(x)	{ memcpy((char *) s->internal_macs + i, &(x), sizeof(x)); i += sizeof(x); }
	i = 0;

	COPY(macsrifle.x);
	COPY(macsrifle.y);
	COPY(macsrifle.buttons);

	assert(i == sizeof(s->internal_macs));

#undef COPY

	s->pad_read      = pad_read;
	s->pad_read_last = pad_read_last;
}

void S9xControlPostLoadState (struct SControlSnapshot *s)
{
	if (curcontrollers[0] == MP5 && s->ver < 1)
	{
		// Crap. Old snes9x didn't support this.
		S9xMessage(S9X_WARNING, S9X_FREEZE_FILE_INFO, "Old savestate has no support for MP5 in port 1.");
		newcontrollers[0] = curcontrollers[0];
		curcontrollers[0] = mp5[0].pads[0];
	}

	for (int j = 0; j < 2; j++)
	{
		read_idx[0][j] = s->port1_read_idx[j];
		read_idx[1][j] = s->port2_read_idx[j];
	}

	for (int j = 0; j < 2; j++)
		mouse[j].buttons |= (s->mouse_speed[j] & 3) << 4;

	if (s->justifier_select & 1)
		justifier.buttons |=  JUSTIFIER_SELECT;
	else
		justifier.buttons &= ~JUSTIFIER_SELECT;

	FLAG_LATCH = (Memory.FillRAM[0x4016] & 1) == 1;

	if (s->ver > 1)
	{
	#define COPY(x)	{ memcpy(&(x), (char *) s->internal + i, sizeof(x)); i += sizeof(x); }

		int	i = 0;

		for (int j = 0; j < 8; j++)
			COPY(joypad[j].buttons);

		for (int j = 0; j < 2; j++)
		{
			COPY(mouse[j].delta_x);
			COPY(mouse[j].delta_y);
			COPY(mouse[j].old_x);
			COPY(mouse[j].old_y);
			COPY(mouse[j].cur_x);
			COPY(mouse[j].cur_y);
			COPY(mouse[j].buttons);
		}

		COPY(superscope.x);
		COPY(superscope.y);
		COPY(superscope.phys_buttons);
		COPY(superscope.next_buttons);
		COPY(superscope.read_buttons);

		for (int j = 0; j < 2; j++)
			COPY(justifier.x[j]);
		for (int j = 0; j < 2; j++)
			COPY(justifier.y[j]);
		COPY(justifier.buttons);
		for (int j = 0; j < 2; j++)
			COPY(justifier.offscreen[j]);
		for (int j = 0; j < 2; j++)
			for (int k = 0; k < 2; k++)
				COPY(mp5[j].pads[k]);

		assert(i == sizeof(s->internal));

		if (s->ver > 3)
		{
			#undef COPY
			#define COPY(x)	{ memcpy(&(x), (char *) s->internal_macs + i, sizeof(x)); i += sizeof(x); }
			i = 0;

			COPY(macsrifle.x);
			COPY(macsrifle.y);
			COPY(macsrifle.buttons);

			assert(i == sizeof(s->internal_macs));
		}

	#undef COPY
	}

	if (s->ver > 2)
	{
		pad_read      = s->pad_read;
		pad_read_last = s->pad_read_last;
	}
}
