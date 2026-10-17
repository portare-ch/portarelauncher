#include "ps_sfo.h"
#include <ctype.h>
#include <stdint.h>
#include <string.h>

static uint16_t le16(const unsigned char *p)
{ return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }

int ps_sfo_serial(const unsigned char *sfo, size_t size, char serial[PS_SERIAL_SIZE])
{
	serial[0] = 0;
	if (size < 20 || size > 65536 || memcmp(sfo,"\0PSF",4) || le32(sfo+4) != 0x101) return 0;
	uint32_t keys = le32(sfo+8), data = le32(sfo+12), count = le32(sfo+16);
	if (!count || count > 256 || keys < 20+count*16 || keys >= data || data > size) return 0;
	int found = 0;
	for (uint32_t i = 0; i < count; i++) {
		const unsigned char *entry = sfo+20+i*16;
		uint32_t key = le16(entry), len = le32(entry+4), cap = le32(entry+8), at = le32(entry+12);
		/* Subtraction keeps all offset/length checks safe on 32-bit hosts too. */
		if (key >= data-keys || !memchr(sfo+keys+key,0,data-keys-key) ||
		    at > size-data || cap > size-data-at || len > cap) return 0;
		if (strcmp((const char *)sfo+keys+key,"DISC_ID")) continue;
		if (found || le16(entry+2) != 0x0204 || (len != 10 && len != 11)) return 0;
		const unsigned char *id = sfo+data+at;
		if (id[len-1] || memchr(id,0,len-1)) return 0;
		/* SFO normally stores ULUS12345; accept ULUS-12345 as well. */
		if (len == 11 && id[4] != '-') return 0;
		for (unsigned k = 0; k < 4; k++) serial[k] = (char)toupper(id[k]);
		serial[4] = '-';
		memcpy(serial+5,id+(len == 11 ? 5 : 4),5);
		serial[10] = 0;
		if (!ps_serial_valid(serial)) return 0;
		found = 1;
	}
	return found;
}
