#include "xbox_iso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define DIRECTORY_MAX (1024U * 1024U)
#define XBE_HEADER_MAX 65536U
#define XBE_IMAGE_HEADER 0x178U
#define XBE_CERTIFICATE 0x1d0U
#define XBE_REGIONS 0xa0U

static const char media[] = "MICROSOFT*XBOX*MEDIA";
/* Game partition starts: a rebuilt XISO, then full XGD1, XGD2 and XGD3 dumps. */
static const uint32_t partitions[] = {0, 0x18300000/2048, 0xfd90000/2048, 0x2080000/2048};

static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static unsigned le16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1]<<8; }
static int read_bytes(const struct ps_disc *d, uint64_t lba, uint32_t at, unsigned char *out, uint32_t n)
{
	unsigned char sec[2048];
	while (n) {
		uint64_t s = lba + at/2048;
		if (s >= d->sectors) return 1;
		int rc = d->read(d->ctx,(uint32_t)s,sec);
		if (rc) return rc;
		uint32_t off = at%2048, take = 2048-off < n ? 2048-off : n;
		memcpy(out,sec+off,take);
		out += take; at += take; n -= take;
	}
	return 0;
}
/* Visit every node once: the lookup must not depend on the tree's sort order. */
static int find_xbe(const unsigned char *dir, uint32_t bytes, uint32_t *lba, uint32_t *size)
{
	uint32_t slots = bytes/4, count = 0, stack_max = bytes/16+1;
	unsigned char *seen = calloc(slots/8+1,1);
	uint32_t *stack = malloc(stack_max*sizeof(*stack));
	int rc = 1, found = 0;
	if (!seen || !stack) { rc = 2; goto done; }
	stack[count++] = 0;
	while (count) {
		uint32_t slot = stack[--count], off = slot*4;
		if (off+14 > bytes || (seen[slot/8] & (1U << slot%8))) goto done;
		seen[slot/8] |= (unsigned char)(1U << slot%8);
		const unsigned char *e = dir+off;
		unsigned left = le16(e), right = le16(e+2), n = e[13];
		/* Entries never cross a sector; 0xff padding is not an entry. */
		if (!n || off%2048+14+n > 2048 || off+14+n > bytes) goto done;
		if (n == 11 && !strncasecmp((const char *)e+14,"default.xbe",11)) {
			if (found || (e[12] & 0x10)) goto done;
			*lba = le32(e+4); *size = le32(e+8); found = 1;
		}
		if (count+2 > stack_max) goto done;
		if (left) stack[count++] = left;
		if (right) stack[count++] = right;
	}
	rc = !found;
done:
	free(seen); free(stack);
	return rc;
}
int xbox_iso_serial(const struct ps_disc *d, char serial[PS_SERIAL_SIZE])
{
	unsigned char sec[2048];
	uint32_t base = 0, dir = 0, bytes = 0;
	int found = 0;
	serial[0] = 0;
	for (unsigned i = 0; i < sizeof(partitions)/sizeof(*partitions); i++) {
		if (partitions[i]+33ULL > d->sectors) continue;
		int rc = d->read(d->ctx,partitions[i]+32,sec);
		if (rc) return rc;
		if (memcmp(sec,media,20) || memcmp(sec+0x7ec,media,20)) continue;
		if (found) return 1; /* Two game partitions are ambiguous. */
		base = partitions[i]; dir = le32(sec+20); bytes = le32(sec+24); found = 1;
	}
	uint64_t volume = d->sectors-base;
	if (!found || !bytes || bytes > DIRECTORY_MAX || dir >= volume ||
	    (bytes+2047ULL)/2048 > volume-dir) return 1;
	unsigned char *data = malloc(bytes);
	if (!data) return 2;
	uint32_t lba = 0, size = 0;
	int rc = read_bytes(d,base+dir,0,data,bytes);
	if (!rc) rc = find_xbe(data,bytes,&lba,&size);
	free(data);
	if (rc) return rc;
	if (size < XBE_IMAGE_HEADER || lba >= volume || (size+2047ULL)/2048 > volume-lba) return 1;
	unsigned char head[XBE_IMAGE_HEADER];
	rc = read_bytes(d,base+lba,0,head,sizeof(head));
	if (rc) return rc;
	uint32_t image = le32(head+0x104), headers = le32(head+0x108), cert = le32(head+0x118)-image;
	if (memcmp(head,"XBEH",4) || headers > size || headers > XBE_HEADER_MAX ||
	    headers < XBE_IMAGE_HEADER+XBE_CERTIFICATE ||
	    le32(head+0x118) < image || cert < XBE_IMAGE_HEADER) return 1;
	unsigned char c[XBE_REGIONS+4];
	if (cert > headers-sizeof(c)) return 1;
	rc = read_bytes(d,base+lba,cert,c,sizeof(c));
	if (rc) return rc;
	uint32_t length = le32(c), id = le32(c+8);
	/* North America, Japan, rest of world. Releases sharing a title ID differ by region. */
	unsigned regions = le32(c+XBE_REGIONS) & 7;
	if (length < XBE_CERTIFICATE || length > headers-cert || !regions) return 1;
	/* The title ID's high half is the publisher's two letters, as in Redump's MS-004. */
	unsigned char a = (unsigned char)(id>>24), b = (unsigned char)(id>>16);
	if (a < 'A' || a > 'Z' || b < 'A' || b > 'Z') return 1;
	snprintf(serial,PS_SERIAL_SIZE,"X%c%c%c-%05u",'A'+regions,a,b,(unsigned)(id & 0xffff));
	return 0;
}
