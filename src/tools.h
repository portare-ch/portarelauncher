/* The Tools folder: the scripts packages drop into the modules directory,
 * which is what EmulationStation listed as its Tools system.
 *
 * Whatever is in the folder is what gets offered - a package that installs
 * a tool is all it takes to appear here. Names and descriptions come from
 * the folder's own gamelist.xml where it has them, so a tool is shown as
 * "File Manager" rather than "commander.sh", and the description can carry
 * what has to be read before launching: how to get back out.
 */
#ifndef PL_TOOLS_H
#define PL_TOOLS_H

/* post-update copies /usr/config/modules here on every update; the image's
 * copy is the fallback for a device where it has not happened yet. */
#define TOOLS_DIR          "/storage/.config/modules"
#define TOOLS_DIR_FALLBACK "/usr/config/modules"

#define TOOLS_MAX 32

struct tool {
	char file[128];      /* "commander.sh"                     */
	char name[64];       /* "File Manager", or the file's name */
	char desc[320];      /* may be empty                       */
};

struct tools {
	char dir[64];
	struct tool t[TOOLS_MAX];
	int n;
};

/* Lists the folder, sorted by the name shown. Returns the count; 0 when
 * there is no folder, which is how the Tools entry stays off a machine
 * that has none. */
int tools_load(struct tools *ts);

#endif
