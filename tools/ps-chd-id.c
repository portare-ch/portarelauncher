/* Read-only, bounded PS1/PS2/PSP identification. Compression is entirely libchdr's. */
#include "ps_iso.h"
#include <libchdr/chd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

struct image {
	chd_file *chd;
	FILE *fp;
	const chd_header *h;
	unsigned char *hunk;
	uint32_t loaded;
	uint64_t start, sectors;
	unsigned stride, offset;
	int raw, mode2;
};
static int failure(struct image *im, chd_error err)
{
	fprintf(stderr, "ps-chd-id: %s\n", chd_error_string(err));
	return ferror(im->fp) || err == CHDERR_OUT_OF_MEMORY ? 2 : 1;
}
static int bytes(struct image *im, uint64_t pos, void *out, size_t n)
{
	if (pos > im->h->logicalbytes || n > im->h->logicalbytes - pos) return 1;
	while (n) {
		uint32_t hunk = (uint32_t)(pos / im->h->hunkbytes);
		unsigned at = (unsigned)(pos % im->h->hunkbytes);
		if (im->loaded != hunk) {
			chd_error err = chd_read(im->chd, hunk, im->hunk);
			if (err) return failure(im, err);
			im->loaded = hunk;
		}
		size_t take = im->h->hunkbytes - at;
		if (take > n) take = n;
		memcpy(out, im->hunk+at, take);
		out = (unsigned char *)out + take;
		pos += take; n -= take;
	}
	return 0;
}
static int sector(void *ctx, uint32_t lba, unsigned char out[2048])
{
	struct image *im = ctx;
	if (lba >= im->sectors) return 1;
	uint64_t pos = (im->start + lba) * im->stride;
	unsigned char frame[2352];
	unsigned n = im->raw ? 2352 : im->mode2 ? 2336 : 2048;
	int rc = bytes(im, pos, frame, n);
	if (rc) return rc;
	if (im->raw) {
		static const unsigned char sync[12] = {0,255,255,255,255,255,255,255,255,255,255,0};
		if (memcmp(frame, sync, 12) || frame[15] != (im->mode2 ? 2 : 1)) return 1;
	}
	if (im->mode2) {
		unsigned sub = im->raw ? 16 : 0;
		if (memcmp(frame+sub, frame+sub+4, 4) || (frame[sub+2] & 0x20)) return 1;
	}
	memcpy(out, frame+im->offset, 2048);
	return 0;
}
static int track_type(struct image *im, const char *s)
{
	im->offset = 0; im->raw = im->mode2 = 0;
	if (!strcmp(s,"MODE1") || !strcmp(s,"MODE1/2048") ||
	    !strcmp(s,"MODE2_FORM1") || !strcmp(s,"MODE2/2048")) return 1;
	if (!strcmp(s,"MODE1_RAW") || !strcmp(s,"MODE1/2352")) {
		im->raw = 1; im->offset = 16; return 1;
	}
	if (!strcmp(s,"MODE2_RAW") || !strcmp(s,"MODE2/2352")) {
		im->raw = im->mode2 = 1; im->offset = 24; return 1;
	}
	if (!strcmp(s,"MODE2") || !strcmp(s,"MODE2/2336") || !strcmp(s,"MODE2_FORM_MIX")) {
		im->mode2 = 1; im->offset = 8; return 1;
	}
	return 0;
}
static int identify(struct image *im, char serial[PS_SERIAL_SIZE])
{
	im->stride = im->h->unitbytes;
	if (im->stride == 2048) {
		im->sectors = im->h->logicalbytes / 2048;
		struct ps_disc d = {im, im->sectors, sector};
		return ps_iso_serial(&d, serial);
	}
	if (im->stride != 2448) return 1;
	uint64_t start = 0;
	int found = 0;
	char identified[PS_SERIAL_SIZE] = "";
	for (unsigned i = 0; i <= 99; i++) {
		char meta[256] = {0}, type[32], sub[32], pgtype[32] = "", pgsub[32];
		unsigned len = 0, num = 0, frames = 0, pregap = 0, postgap = 0;
		chd_error err = chd_get_metadata(im->chd, CDROM_TRACK_METADATA2_TAG, i,
		                               meta, sizeof(meta)-1, &len, NULL, NULL);
		int modern = err == CHDERR_NONE;
		if (err == CHDERR_METADATA_NOT_FOUND)
			err = chd_get_metadata(im->chd, CDROM_TRACK_METADATA_TAG, i,
			                       meta, sizeof(meta)-1, &len, NULL, NULL);
		if (err == CHDERR_METADATA_NOT_FOUND) {
			if (found) memcpy(serial, identified, PS_SERIAL_SIZE);
			return found ? 0 : 1;
		}
		if (err) return failure(im, err);
		if (i == 99 || len >= sizeof(meta)) return 1;
		int n = modern ? sscanf(meta, "TRACK:%u TYPE:%31s SUBTYPE:%31s FRAMES:%u PREGAP:%u PGTYPE:%31s PGSUB:%31s POSTGAP:%u",
		                        &num,type,sub,&frames,&pregap,pgtype,pgsub,&postgap)
		               : sscanf(meta, "TRACK:%u TYPE:%31s SUBTYPE:%31s FRAMES:%u", &num,type,sub,&frames);
		if (n != (modern ? 8 : 4) || num != i+1 || !frames ||
		    (pgtype[0] == 'V' && pregap > frames) ||
		    start > im->h->unitcount || frames > im->h->unitcount-start) return 1;
		unsigned skip = pgtype[0] == 'V' ? pregap : 0;
		im->start = start + skip;
		im->sectors = frames - skip;
		if (track_type(im, type)) {
			/* PS1 ISO volume space may include later audio tracks. Actual reads
			 * remain bounded to this data track by sector(), including extents. */
			struct ps_disc d = {im, im->h->unitcount, sector};
			char s[PS_SERIAL_SIZE];
			int rc = ps_iso_serial(&d, s);
			if (rc == 2) return rc;
			if (!rc) {
				if (found && strcmp(identified, s)) return 1;
				memcpy(identified, s, sizeof(s)); found = 1;
			}
		}
		/* CHD stores every CD frame at 2448 bytes and pads EACH track to four frames.
		 * Only V-prefixed PGTYPE pregaps occupy frames in this track's data. */
		start += ((uint64_t)frames + 3) & ~3ULL;
	}
	return 1;
}
int main(int argc, char **argv)
{
	if (argc != 2 || argv[1][0] != '/') {
		fprintf(stderr, "usage: ps-chd-id /absolute/path/game.chd\n"); return 2;
	}
#if defined(__linux__) && !defined(PS_ID_SANITIZED)
	struct rlimit mem = {256U*1024U*1024U,256U*1024U*1024U};
	struct rlimit cpu = {15,15};
	if (setrlimit(RLIMIT_AS, &mem) || setrlimit(RLIMIT_CPU, &cpu)) return 2;
#endif
	struct image im = {0};
	im.loaded = UINT32_MAX;
	int fd = open(argv[1], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) { perror("ps-chd-id"); return 2; }
	im.fp = fdopen(fd, "rb");
	if (!im.fp) close(fd);
	if (!im.fp) { perror("ps-chd-id"); return 2; }
	struct stat st;
	int rc = 1;
	chd_header header;
	if (fstat(fileno(im.fp), &st)) { rc = 2; goto done; }
	if (!S_ISREG(st.st_mode)) goto done;
	chd_error err = chd_read_header_file(im.fp, &header);
	if (err) { rc = failure(&im,err); goto done; }
	/* Bounds apply BEFORE libchdr allocates the map or codec buffers. */
	if (!header.hunkbytes || header.hunkbytes > 1024U*1024U ||
	    !header.totalhunks || header.totalhunks > 4U*1024U*1024U ||
	    !header.logicalbytes || header.logicalbytes > 16ULL*1024*1024*1024 ||
	    (header.unitbytes != 2048 && header.unitbytes != 2448)) goto done;
	err = chd_open_file(im.fp, CHD_OPEN_READ, NULL, &im.chd);
	if (err) { rc = failure(&im,err); goto done; }
	im.h = chd_get_header(im.chd);
	im.hunk = malloc(im.h->hunkbytes);
	if (!im.hunk) { rc = 2; goto done; }
	char serial[PS_SERIAL_SIZE];
	rc = identify(&im, serial);
	if (!rc && printf("%s\n",serial) < 0) rc = 2;
done:
	free(im.hunk);
	if (im.chd) chd_close(im.chd);
	fclose(im.fp);
	return rc;
}
