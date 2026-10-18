#include "ps_iso.h"
#include "ps_sfo.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define DIRECTORY_MAX (1024U * 1024U)
#define CONFIG_MAX 32768U
#define SFO_MAX 65536U

static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static int both32(const unsigned char *p, uint32_t *v)
{
	*v = le32(p);
	return *v == ((uint32_t)p[4]<<24 | (uint32_t)p[5]<<16 | (uint32_t)p[6]<<8 | p[7]);
}
static int extent(const unsigned char *r, uint32_t volume, uint32_t *lba, uint32_t *size)
{
	if (r[0] < 34 || r[1] || r[26] || r[27] || r[32] > r[0] - 33 ||
	    (r[25] & 0x80) || !both32(r+2, lba) || !both32(r+10, size)) return 0;
	return *lba < volume && (*size + 2047ULL) / 2048 <= volume - *lba;
}
static int named(const unsigned char *r, const char *name)
{
	unsigned n = r[32];
	size_t want = strlen(name);
	if (n < want || strncasecmp((const char *)r+33,name,want)) return 0;
	if (n == want) return 1;
	if (n < want+2 || r[33+want] != ';') return 0;
	for (unsigned i = 34+(unsigned)want; i < 33+n; i++) if (r[i] < '0' || r[i] > '9') return 0;
	return 1;
}
struct metadata_entry {
	const char *name;
	int directory, found;
	uint32_t lba, size;
};
/* Search only the explicitly requested directories, never recurse through a disc. */
static int directory(const struct ps_disc *d, uint32_t volume, uint32_t dir, uint32_t bytes,
                     struct metadata_entry *targets, unsigned count)
{
	if (!bytes || bytes > DIRECTORY_MAX) return 1;
	unsigned char sec[2048];
	for (uint32_t at = 0; at < bytes; at += 2048) {
		int rc = d->read(d->ctx,dir+at/2048,sec);
		if (rc) return rc;
		unsigned limit = bytes-at < 2048 ? bytes-at : 2048;
		for (unsigned p = 0; p < limit && sec[p]; p += sec[p]) {
			const unsigned char *r = sec+p;
			if (r[0] < 34 || (r[0] & 1) || r[0] > limit-p || r[32] > r[0]-33) return 1;
			for (unsigned i = 0; i < count; i++) {
				struct metadata_entry *e = &targets[i];
				if (!named(r,e->name)) continue;
				if (e->found || !!(r[25]&2) != e->directory ||
				    !extent(r,volume,&e->lba,&e->size) || !e->size) return 1;
				e->found = 1;
			}
		}
	}
	return 0;
}
static int read_file(const struct ps_disc *d, const struct metadata_entry *e, unsigned char *out, unsigned max)
{
	if (!e->size || e->size > max) return 1;
	unsigned char sec[2048];
	for (uint32_t at = 0; at < e->size; at += 2048) {
		int rc = d->read(d->ctx,e->lba+at/2048,sec);
		if (rc) return rc;
		unsigned n = e->size-at < 2048 ? e->size-at : 2048;
		memcpy(out+at,sec,n);
	}
	return 0;
}
static int psp_serial(const struct ps_disc *d, uint32_t volume, const struct metadata_entry *game,
                      char serial[PS_SERIAL_SIZE])
{
	struct metadata_entry sfo = {.name="PARAM.SFO"};
	int rc = directory(d,volume,game->lba,game->size,&sfo,1);
	if (rc) return rc;
	if (!sfo.found) return 1;
	unsigned char data[SFO_MAX];
	rc = read_file(d,&sfo,data,sizeof(data));
	if (rc) return rc;
	return ps_sfo_serial(data,sfo.size,serial) ? 0 : 1;
}
static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *end = s+strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
	return s;
}
static int hex_field(char *s, uint32_t *value)
{
	s = trim(s);
	size_t n = strlen(s);
	if (!n || n > 8) return 0;
	*value = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		unsigned digit = c >= '0' && c <= '9' ? c-'0' :
		                 c >= 'A' && c <= 'F' ? c-'A'+10 :
		                 c >= 'a' && c <= 'f' ? c-'a'+10 : 16;
		if (digit == 16) return 0;
		*value = (*value << 4) | digit;
	}
	return 1;
}
/* IPL entries are filename,bank,hexadecimal load address. Only the PRG loaded
 * at zero contains the boot header; other PRGs can be unrelated overlays. */
static int neocd_serial(const struct ps_disc *d, uint32_t volume, uint32_t dir, uint32_t bytes,
                        const struct metadata_entry *ipl, char serial[PS_SERIAL_SIZE])
{
	unsigned char data[CONFIG_MAX+1];
	int rc = read_file(d,ipl,data,CONFIG_MAX);
	if (rc) return rc;
	if (memchr(data,0,ipl->size)) return 1;
	data[ipl->size] = 0;
	char program[32] = "";
	unsigned count = 0;
	for (char *p = (char *)data; *p;) {
		if (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') { p++; continue; }
		if ((unsigned char)*p == 0x1a) {
			for (p++; *p; p++) if (*p != '\r' && *p != '\n' && *p != ' ' && *p != '\t') return 1;
			break;
		}
		char *line = p;
		while (*p && *p != '\r' && *p != '\n' && (unsigned char)*p != 0x1a) p++;
		/* The DOS EOF marker may follow the last entry without a newline. */
		int eof = (unsigned char)*p == 0x1a;
		if (*p) *p++ = 0;
		if (++count > 32) return 1;
		char *bank = strchr(line,','), *address;
		if (!bank) return 1;
		*bank++ = 0; address = strchr(bank,',');
		if (!address) return 1;
		*address++ = 0;
		uint32_t b, a;
		if (!hex_field(bank,&b) || !hex_field(address,&a)) return 1;
		char *name = trim(line);
		size_t n = strlen(name);
		if (!n || n >= sizeof(program)) return 1;
		for (size_t i = 0; i < n; i++) {
			unsigned char c = (unsigned char)name[i];
			if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			      (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 1;
		}
		if (n > 4 && !strcasecmp(name+n-4,".PRG") && !b && !a) {
			if (program[0]) return 1;
			memcpy(program,name,n+1);
		}
		if (eof) {
			for (; *p; p++) if (*p != '\r' && *p != '\n' && *p != ' ' && *p != '\t') return 1;
			break;
		}
	}
	if (!program[0]) return 1;
	struct metadata_entry prg = {.name=program};
	rc = directory(d,volume,dir,bytes,&prg,1);
	if (rc) return rc;
	if (!prg.found || prg.size < 0x10a) return 1;
	unsigned char sec[2048];
	rc = d->read(d->ctx,prg.lba,sec);
	if (rc) return rc;
	if (memcmp(sec+0x100,"NEO-GEO",7)) return 1;
	unsigned id = (unsigned)sec[0x108]<<8 | sec[0x109];
	if (!id || id == 0xffff) return 1;
	/* The five hexadecimal digits keep the existing fixed-width cache protocol. */
	snprintf(serial,PS_SERIAL_SIZE,"NGCD-%05X",id);
	return 0;
}
int ps_iso_serial(const struct ps_disc *d, char serial[PS_SERIAL_SIZE])
{
	unsigned char sec[2048], root[34];
	uint32_t volume = 0, dir = 0, bytes = 0;
	serial[0] = 0;
	for (uint32_t i = 16; i < 48 && i < d->sectors; i++) {
		int rc = d->read(d->ctx,i,sec);
		if (rc) return rc;
		if (memcmp(sec+1,"CD001",5) || sec[6] != 1) return 1;
		if (sec[0] == 255) break;
		if (sec[0] != 1) continue;
		if (!both32(sec+80,&volume) || volume > d->sectors ||
		    sec[128] != 0 || sec[129] != 8 || sec[130] != 8 || sec[131] != 0) return 1;
		memcpy(root,sec+156,sizeof(root));
		if (root[0] != 34 || !(root[25]&2) || !extent(root,volume,&dir,&bytes)) return 1;
		break;
	}
	if (!bytes) return 1;
	struct metadata_entry targets[] = {{.name="SYSTEM.CNF"},{.name="PSP_GAME",.directory=1},{.name="IPL.TXT"}};
	int rc = directory(d,volume,dir,bytes,targets,3);
	if (rc) return rc;
	/* A mixed console layout is ambiguous. Invalid metadata cannot fall through. */
	if (targets[0].found + targets[1].found + targets[2].found > 1) return 1;
	if (targets[2].found) return neocd_serial(d,volume,dir,bytes,&targets[2],serial);
	if (targets[1].found) return psp_serial(d,volume,&targets[1],serial);
	if (!targets[0].found) return 1;
	unsigned char config[CONFIG_MAX];
	rc = read_file(d,&targets[0],config,sizeof(config));
	if (rc) return rc;
	return ps_boot_serial((const char *)config,targets[0].size,serial) ? 0 : 1;
}
