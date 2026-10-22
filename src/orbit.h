/* Where the menu's grid sits: a walk of a few pixels around its centred
 * place, so static text does not wear the same OLED pixels for hours. */
#ifndef PL_ORBIT_H
#define PL_ORBIT_H

/* The offset for a wall-clock minute: a triangle wave from 0 to +amp, down
 * to -amp and back, one pixel a minute, each axis on its own period of
 * 4 * amp minutes. The clock, not uptime, so each session starts elsewhere. */
void orbit_offset(long long minute, int amp_x, int amp_y, int *dx, int *dy);

#endif
