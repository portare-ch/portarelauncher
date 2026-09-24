/* Files that name other files: the reason one game can be several entries.
 *
 * A PlayStation or Saturn rip is a .cue and one .bin per track; a Dreamcast
 * one a .gdi and its tracks; a multi-disc game an .m3u naming each disc's
 * sheet. Every one of those files has an extension the system accepts, so
 * a folder scan listed Tekken 3 four times - the .cue and three .bin - and
 * picking a track launched nothing useful.
 *
 * The rule: a file that a sheet in the same system names is part of that
 * sheet's game, and only the sheet is listed.
 */
#ifndef PL_SHEETS_H
#define PL_SHEETS_H

#include <stddef.h>

#define SHEET_MAX_REFS 64

/* Is this a file that names others: .cue .gdi .toc .m3u .ccd? */
int sheet_is(const char *file);

/* The files the sheet at path names, as full paths resolved against the
 * sheet's own directory, backslashes turned round. A .ccd names nothing
 * inside itself; CloneCD's .img and .sub share its name, so those are what
 * it returns. Returns how many, up to max. */
int sheet_refs(const char *path, char out[][512], int max);

#endif
