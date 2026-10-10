/* Settings > Consoles: what a player sets for one console.
 *
 * Each switch is a system.cfg key that setsettings.sh reads when a game
 * starts, keyed by the system's name in es_systems.cfg like every other
 * per-system setting:
 *
 *   <system>.preempt       PRMPT, the pre-emptive frame
 *   <system>.integerscale  whole multiples of the console's pixels; off,
 *                          the picture is as large as the screen allows,
 *                          at the console's own shape
 *
 * 1 is on. Absent, empty or anything else is off, which is also what
 * setsettings.sh makes of it. A setting has changed when it differs
 * from the image's own copy of system.cfg.
 */
#ifndef PL_CONSOLES_H
#define PL_CONSOLES_H

#define CONSOLES_SHIPPED "/usr/config/system/configs/system.cfg"

enum console_opt { CON_PRMPT, CON_INTEGER, N_CONSOLE_OPTS };

struct console { const char *key, *label; };
extern const struct console consoles[];
extern const int n_consoles;

int console_get(const char *cfg, int i, enum console_opt o);
int console_set(const char *cfg, int i, enum console_opt o, int on);

/* How many of console i's settings in cfg differ from shipped's. */
int console_changed(const char *cfg, const char *shipped, int i);

#endif
