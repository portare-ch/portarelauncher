/* Scripts in the modules directory, named and described by its gamelist.xml. */
#ifndef PL_TOOLS_H
#define PL_TOOLS_H

/* post-update copies modules to storage; use the image copy until then. */
#define TOOLS_DIR          "/storage/.config/modules"
#define TOOLS_DIR_FALLBACK "/usr/config/modules"

#define TOOLS_MAX 32

struct tool {
	char file[128];      /* "commander.sh" */
	char name[64];       /* "File Manager", or the file's name */
	char desc[320];      /* may be empty */
};

struct tools {
	char dir[64];
	struct tool t[TOOLS_MAX];
	int n;
};

/* Lists tools by display name. Returns the count, or 0 for a missing folder. */
int tools_load(struct tools *ts);

/* Loads an explicit directory, also used by fixture tests. */
int tools_load_from(struct tools *ts, const char *dir);

#endif
