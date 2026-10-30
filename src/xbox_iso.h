#ifndef PL_XBOX_ISO_H
#define PL_XBOX_ISO_H
#include "ps_iso.h"
/* Xbox XDVDFS: default.xbe's certificate as X, A plus its region flags, the
 * title ID's publisher letters and number: XBMS-00004 for Halo in North America. */
int xbox_iso_serial(const struct ps_disc *disc, char serial[PS_SERIAL_SIZE]);
#endif
