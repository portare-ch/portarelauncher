#include "kms.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

static drmModeConnector *pick_connector(int fd, drmModeRes *res)
{
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
		if (!c)
			continue;
		if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
			return c;
		drmModeFreeConnector(c);
	}
	return NULL;
}

static const drmModeModeInfo *pick_mode(const drmModeConnector *c)
{
	/* The preferred mode is the panel's native one. On this device that is
	 * 1280x960 at 119.88011988, and the second mode exists only so
	 * SwanStation can ask for exactly twice its frame rate - which is not
	 * this program's business. */
	for (int i = 0; i < c->count_modes; i++)
		if (c->modes[i].type & DRM_MODE_TYPE_PREFERRED)
			return &c->modes[i];
	return &c->modes[0];
}

static uint32_t pick_crtc(int fd, drmModeRes *res, drmModeConnector *conn)
{
	if (conn->encoder_id) {
		drmModeEncoder *e = drmModeGetEncoder(fd, conn->encoder_id);
		if (e) {
			uint32_t id = e->crtc_id;
			drmModeFreeEncoder(e);
			if (id)
				return id;
		}
	}
	for (int i = 0; i < conn->count_encoders; i++) {
		drmModeEncoder *e = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!e)
			continue;
		for (int j = 0; j < res->count_crtcs; j++) {
			if (e->possible_crtcs & (1u << j)) {
				uint32_t id = res->crtcs[j];
				drmModeFreeEncoder(e);
				return id;
			}
		}
		drmModeFreeEncoder(e);
	}
	return 0;
}

int kms_open(struct kms *k, const char *card)
{
	memset(k, 0, sizeof(*k));

	k->fd = open(card, O_RDWR | O_CLOEXEC);
	if (k->fd < 0) {
		fprintf(stderr, "open %s: %s\n", card, strerror(errno));
		return -1;
	}

	drmModeRes *res = drmModeGetResources(k->fd);
	if (!res) {
		fprintf(stderr, "drmModeGetResources: %s\n", strerror(errno));
		goto fail_fd;
	}

	drmModeConnector *conn = pick_connector(k->fd, res);
	if (!conn) {
		fprintf(stderr, "no connected connector with modes\n");
		goto fail_res;
	}

	k->conn_id = conn->connector_id;
	k->mode = *pick_mode(conn);
	k->crtc_id = pick_crtc(k->fd, res, conn);
	if (!k->crtc_id) {
		fprintf(stderr, "no usable crtc for connector %u\n", k->conn_id);
		goto fail_conn;
	}

	struct drm_mode_create_dumb creq = {
		.width = k->mode.hdisplay,
		.height = k->mode.vdisplay,
		.bpp = 32,
	};
	if (drmIoctl(k->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		fprintf(stderr, "create dumb: %s\n", strerror(errno));
		goto fail_conn;
	}
	k->handle = creq.handle;
	k->size = creq.size;
	k->pitch_px = creq.pitch / 4;

	if (drmModeAddFB(k->fd, k->mode.hdisplay, k->mode.vdisplay,
	                 24, 32, creq.pitch, k->handle, &k->fb_id) < 0) {
		fprintf(stderr, "addfb: %s\n", strerror(errno));
		goto fail_dumb;
	}

	struct drm_mode_map_dumb mreq = { .handle = k->handle };
	if (drmIoctl(k->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		fprintf(stderr, "map dumb: %s\n", strerror(errno));
		goto fail_fb;
	}

	k->map = mmap(NULL, k->size, PROT_READ | PROT_WRITE, MAP_SHARED,
	              k->fd, (off_t)mreq.offset);
	if (k->map == MAP_FAILED) {
		k->map = NULL;
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		goto fail_fb;
	}
	memset(k->map, 0, k->size);

	k->saved = drmModeGetCrtc(k->fd, k->crtc_id);

	drmModeFreeConnector(conn);
	drmModeFreeResources(res);

	if (kms_present(k) < 0)
		return -1;
	return 0;

fail_fb:
	drmModeRmFB(k->fd, k->fb_id);
	k->fb_id = 0;
fail_dumb:
	{
		struct drm_mode_destroy_dumb d = { .handle = k->handle };
		drmIoctl(k->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
		k->handle = 0;
	}
fail_conn:
	drmModeFreeConnector(conn);
fail_res:
	drmModeFreeResources(res);
fail_fd:
	close(k->fd);
	k->fd = -1;
	return -1;
}

int kms_present(struct kms *k)
{
	if (drmModeSetCrtc(k->fd, k->crtc_id, k->fb_id, 0, 0,
	                   &k->conn_id, 1, &k->mode) < 0) {
		fprintf(stderr, "setcrtc: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int kms_blank(struct kms *k)
{
	if (drmModeSetCrtc(k->fd, k->crtc_id, 0, 0, 0, NULL, 0, NULL) < 0) {
		fprintf(stderr, "blank: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int kms_drop_master(struct kms *k)
{
	if (drmDropMaster(k->fd) < 0) {
		fprintf(stderr, "drop master: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

/* Quiet, because it is retried: the caller says so when it gives up. */
int kms_set_master(struct kms *k)
{
	return drmSetMaster(k->fd) < 0 ? -1 : 0;
}

void kms_close(struct kms *k)
{
	if (k->fd < 0)
		return;

	if (k->saved) {
		drmModeSetCrtc(k->fd, k->saved->crtc_id, k->saved->buffer_id,
		               k->saved->x, k->saved->y, &k->conn_id, 1,
		               &k->saved->mode);
		drmModeFreeCrtc(k->saved);
		k->saved = NULL;
	}
	if (k->map)
		munmap(k->map, k->size);
	if (k->fb_id)
		drmModeRmFB(k->fd, k->fb_id);
	if (k->handle) {
		struct drm_mode_destroy_dumb d = { .handle = k->handle };
		drmIoctl(k->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
	}
	close(k->fd);
	k->fd = -1;
}
