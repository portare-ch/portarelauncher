#ifndef PL_PS_SFO_H
#define PL_PS_SFO_H
#include <stddef.h>
#include "ps_serial.h"
/* Read DISC_ID from a bounded PSP PARAM.SFO. No title text leaves the helper. */
int ps_sfo_serial(const unsigned char *sfo, size_t size, char serial[PS_SERIAL_SIZE]);
#endif
