#include "consoles.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

/* The PlayStation keeps its Vulkan renderer at 4x with the frame on. Its
 * old latency mode also dropped to software at 1x; that renderer was
 * never what cost the frame rate, so the switch is the same as here. */
const struct console consoles[] = {
	{ "snes",    "SNES"             },
	{ "nes",     "NES"              },
	{ "psx",     "PlayStation"      },
	{ "gb",      "Game Boy"         },
	{ "gbc",     "Game Boy Color"   },
	{ "gba",     "Game Boy Advance" },
	{ "genesis", "Genesis"          },
};
const int n_consoles = (int)(sizeof(consoles) / sizeof(consoles[0]));

static const char *const suffix[N_CONSOLE_OPTS] = {
	[CON_PRMPT]   = "preempt",
	[CON_INTEGER] = "integerscale",
};

static void key_of(char *key, size_t n, int i, enum console_opt o)
{
	snprintf(key, n, "%s.%s", consoles[i].key, suffix[o]);
}

int console_get(const char *cfg, int i, enum console_opt o)
{
	char key[64], val[16];
	key_of(key, sizeof(key), i, o);
	return settings_get(cfg, key, val, sizeof(val)) && strcmp(val, "1") == 0;
}

int console_set(const char *cfg, int i, enum console_opt o, int on)
{
	char key[64];
	key_of(key, sizeof(key), i, o);
	return settings_set(cfg, key, on ? "1" : "0");
}

int console_changed(const char *cfg, const char *shipped, int i)
{
	int n = 0;
	for (int o = 0; o < N_CONSOLE_OPTS; o++)
		n += console_get(cfg, i, (enum console_opt)o) !=
		     console_get(shipped, i, (enum console_opt)o);
	return n;
}
