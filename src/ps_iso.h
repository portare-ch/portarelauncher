#ifndef PL_PS_ISO_H
#define PL_PS_ISO_H
#include <stdint.h>
#include "ps_serial.h"
/* A reader returns 0 on success, 1 for malformed/unsupported data, 2 for I/O failure. */
struct ps_disc {
	void *ctx;
	uint64_t sectors;
	int (*read)(void *ctx, uint32_t lba, unsigned char sector[2048]);
};
int ps_iso_serial(const struct ps_disc *disc, char serial[PS_SERIAL_SIZE]);
#endif
