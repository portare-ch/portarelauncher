/* Disc sheets and playlists. Files referenced by a sheet in the same
 * system belong to that game and are hidden from the catalog. */
#ifndef PL_SHEETS_H
#define PL_SHEETS_H

#include <stddef.h>

#define SHEET_MAX_REFS 64

/* Is this a file that names others: .cue .gdi .toc .m3u .ccd? */
int sheet_is(const char *file);

/* Resolves references against the sheet's directory and normalizes
 * backslashes. CloneCD .ccd returns same-stem .img and .sub files.
 * Returns the number of paths, up to max. */
int sheet_refs(const char *path, char out[][512], int max);

#endif
