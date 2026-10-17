/* Game catalog from EmulationStation's es_systems.cfg and per-system
 * overrides in system.cfg. The tag scanner handles generated XML and
 * explicitly decodes entity escapes. */
#ifndef PL_CATALOG_H
#define PL_CATALOG_H

struct game {
	char name[192];   /* display title; defaults to filename without extension */
	char path[512];
	int folder;       /* a folder that plays as a whole, "name/" */
};

struct psystem {
	char name[32];      /* "psx" */
	char fullname[72];  /* "Sony PlayStation" */
	char path[256];     /* /storage/roms/psx */
	char exts[256];     /* ".cue .chd .pbp"; a lone "/" says
	                     * a folder of those plays too */
	char launcher[256]; /* the binary out of <command> */
	char core[64];
	char emulator[64];
	struct game *games;
	int ngames;
};

struct catalog {
	struct psystem *sys;
	int n;
};

struct disc_titles;
int catalog_load_titles(struct catalog *c, const char *es_systems,
                        const char *settings, const struct disc_titles *titles);
/* Sort without changing the selected path. Equal titles remain separate rows. */
int catalog_sort_games(struct psystem *s, const char *selected_path);

/* Drops empty systems. An empty catalog succeeds; returns -1 only when
 * es_systems.cfg cannot be read. */
int  catalog_load(struct catalog *c, const char *es_systems, const char *settings);
void catalog_free(struct catalog *c);

#endif
