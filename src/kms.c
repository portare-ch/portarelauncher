#include "kms.h"
#include "color.h"
#include "modes.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

static int set_vrr(struct kms *k, int on);

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

static int is_internal(uint32_t type)
{
	return type == DRM_MODE_CONNECTOR_DSI || type == DRM_MODE_CONNECTOR_eDP ||
	       type == DRM_MODE_CONNECTOR_LVDS;
}

static const drmModeModeInfo *pick_mode(const drmModeConnector *c)
{
	struct mode_info *m = calloc((size_t)c->count_modes, sizeof(*m));
	if (!m)
		return &c->modes[0];
	for (int i = 0; i < c->count_modes; i++) {
		const drmModeModeInfo *d = &c->modes[i];
		m[i] = (struct mode_info){
			.w = d->hdisplay, .h = d->vdisplay,
			.htotal = d->htotal, .vtotal = d->vtotal,
			.clock_khz = d->clock,
			.preferred = !!(d->type & DRM_MODE_TYPE_PREFERRED),
			.interlaced = !!(d->flags & DRM_MODE_FLAG_INTERLACE),
		};
	}
	int i = modes_pick(m, c->count_modes, is_internal(c->connector_type));
	free(m);
	return &c->modes[i < 0 ? 0 : i];
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

static int fb_create(struct kms *k)
{
	struct drm_mode_create_dumb creq = {
		.width = k->mode.hdisplay,
		.height = k->mode.vdisplay,
		.bpp = 32,
	};
	if (drmIoctl(k->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		fprintf(stderr, "create dumb: %s\n", strerror(errno));
		return -1;
	}
	k->handle = creq.handle;
	k->size = creq.size;
	k->pitch_px = creq.pitch / 4;

	if (drmModeAddFB(k->fd, k->mode.hdisplay, k->mode.vdisplay,
	                 24, 32, creq.pitch, k->handle, &k->fb_id) < 0) {
		fprintf(stderr, "addfb: %s\n", strerror(errno));
		k->fb_id = 0;
		return -1;
	}

	struct drm_mode_map_dumb mreq = { .handle = k->handle };
	if (drmIoctl(k->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		fprintf(stderr, "map dumb: %s\n", strerror(errno));
		return -1;
	}

	k->map = mmap(NULL, k->size, PROT_READ | PROT_WRITE, MAP_SHARED,
	              k->fd, (off_t)mreq.offset);
	if (k->map == MAP_FAILED) {
		k->map = NULL;
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		return -1;
	}
	memset(k->map, 0, k->size);
	return 0;
}

/* Removing a framebuffer that is on screen also switches its CRTC off. */
static void fb_destroy(struct kms *k)
{
	if (k->map)
		munmap(k->map, k->size);
	k->map = NULL;
	if (k->fb_id)
		drmModeRmFB(k->fd, k->fb_id);
	k->fb_id = 0;
	if (k->handle) {
		struct drm_mode_destroy_dumb d = { .handle = k->handle };
		drmIoctl(k->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
	}
	k->handle = 0;
}

/* Makes conn the output: its mode, a CRTC for it, a buffer of its size. */
static int use(struct kms *k, drmModeRes *res, drmModeConnector *conn)
{
	k->conn_id = conn->connector_id;
	k->internal = is_internal(conn->connector_type);
	k->mode = *pick_mode(conn);
	k->crtc_id = pick_crtc(k->fd, res, conn);
	if (!k->crtc_id) {
		fprintf(stderr, "no usable crtc for connector %u\n", k->conn_id);
		return -1;
	}
	if (fb_create(k) < 0) {
		fb_destroy(k);
		return -1;
	}
	return 0;
}

/* Only one output is lit: the panel goes dark when a display takes over,
 * and a console left on the other output goes with it. */
static void disable_others(struct kms *k, drmModeRes *res)
{
	for (int i = 0; i < res->count_crtcs; i++) {
		if (res->crtcs[i] == k->crtc_id)
			continue;
		drmModeCrtc *c = drmModeGetCrtc(k->fd, res->crtcs[i]);
		if (!c)
			continue;
		if (c->mode_valid)
			drmModeSetCrtc(k->fd, c->crtc_id, 0, 0, 0, NULL, 0, NULL);
		drmModeFreeCrtc(c);
	}
}

/* The colour profiles are the panel's calibration. A display gets none,
 * including the LUTs the panel left on a CRTC the display now uses. */
static void lit(struct kms *k, drmModeRes *res)
{
	disable_others(k, res);
	if (!k->internal)
		kms_color_apply(k, NULL);
}

int kms_open(struct kms *k, const char *card)
{
	memset(k, 0, sizeof(*k));

	k->fd = open(card, O_RDWR | O_CLOEXEC);
	if (k->fd < 0) {
		fprintf(stderr, "open %s: %s\n", card, strerror(errno));
		return -1;
	}
	/* Expose all planes, including cursors, for cleanup in kms_present. */
	drmSetClientCap(k->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

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
	if (use(k, res, conn) < 0)
		goto fail_conn;
	drmModeFreeConnector(conn);

	k->saved = drmModeGetCrtc(k->fd, k->crtc_id);

	if (kms_present(k) < 0) {
		drmModeFreeResources(res);
		return -1;
	}
	lit(k, res);
	drmModeFreeResources(res);
	return 0;

fail_conn:
	drmModeFreeConnector(conn);
fail_res:
	drmModeFreeResources(res);
fail_fd:
	close(k->fd);
	k->fd = -1;
	return -1;
}

int kms_follow(struct kms *k)
{
	drmModeRes *res = drmModeGetResources(k->fd);
	if (!res)
		return -1;
	int ret = 0;
	/* Between a display leaving and the panel coming back nothing is
	 * connected; the panel's own event follows. */
	drmModeConnector *conn = pick_connector(k->fd, res);
	if (!conn)
		goto out;
	if (conn->connector_id == k->conn_id &&
	    !memcmp(pick_mode(conn), &k->mode, sizeof(k->mode)))
		goto out;

	ret = -1;
	fb_destroy(k);
	if (use(k, res, conn) < 0)
		goto out;
	/* What was on the CRTC when we arrived belongs to the old output. */
	if (k->saved) {
		drmModeFreeCrtc(k->saved);
		k->saved = NULL;
	}
	if (kms_present(k) < 0)
		goto out;
	lit(k, res);
	ret = 1;
out:
	if (conn)
		drmModeFreeConnector(conn);
	drmModeFreeResources(res);
	return ret;
}

/* Disable other planes on our CRTC. Legacy cursor framebuffers can survive
 * a client's exit and otherwise remain visible over later programs. */
static void clear_other_planes(struct kms *k)
{
	drmModeSetCursor(k->fd, k->crtc_id, 0, 0, 0);

	drmModePlaneRes *pr = drmModeGetPlaneResources(k->fd);
	if (!pr)
		return;
	for (uint32_t i = 0; i < pr->count_planes; i++) {
		drmModePlane *pl = drmModeGetPlane(k->fd, pr->planes[i]);
		if (!pl)
			continue;
		if (pl->crtc_id == k->crtc_id && pl->fb_id && pl->fb_id != k->fb_id)
			drmModeSetPlane(k->fd, pl->plane_id, k->crtc_id, 0, 0,
			                0, 0, 0, 0, 0, 0, 0, 0);
		drmModeFreePlane(pl);
	}
	drmModeFreePlaneResources(pr);
}

int kms_present(struct kms *k)
{
	if (drmModeSetCrtc(k->fd, k->crtc_id, k->fb_id, 0, 0,
	                   &k->conn_id, 1, &k->mode) < 0) {
		fprintf(stderr, "setcrtc: %s\n", strerror(errno));
		return -1;
	}
	clear_other_planes(k);
	/* Whoever had the panel in between may have cleared the property. */
	if (k->vrr)
		set_vrr(k, 1);
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

int kms_vrr(struct kms *k, int on)
{
	k->vrr = on;
	return set_vrr(k, on);
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

	if (k->vrr)
		set_vrr(k, 0);
	if (k->saved) {
		drmModeSetCrtc(k->fd, k->saved->crtc_id, k->saved->buffer_id,
		               k->saved->x, k->saved->y, &k->conn_id, 1,
		               &k->saved->mode);
		drmModeFreeCrtc(k->saved);
		k->saved = NULL;
	}
	fb_destroy(k);
	close(k->fd);
	k->fd = -1;
}

/* Look up properties by name; IDs vary by object and driver. */
static uint32_t crtc_prop(struct kms *k, const char *name)
{
	uint32_t id = 0;
	drmModeObjectProperties *props =
		drmModeObjectGetProperties(k->fd, k->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!props)
		return 0;
	for (uint32_t i = 0; i < props->count_props && !id; i++) {
		drmModePropertyRes *pr = drmModeGetProperty(k->fd, props->props[i]);
		if (!pr)
			continue;
		if (strcmp(pr->name, name) == 0)
			id = pr->prop_id;
		drmModeFreeProperty(pr);
	}
	drmModeFreeObjectProperties(props);
	return id;
}

static int set_vrr(struct kms *k, int on)
{
	uint32_t prop = crtc_prop(k, "VRR_ENABLED");
	if (!prop) {
		fprintf(stderr, "kms: the crtc has no VRR_ENABLED property\n");
		return -1;
	}
	if (drmModeObjectSetProperty(k->fd, k->crtc_id, DRM_MODE_OBJECT_CRTC,
	                             prop, on ? 1 : 0) < 0) {
		fprintf(stderr, "kms: VRR_ENABLED: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int set_blob(struct kms *k, const char *name, const void *data, size_t len)
{
	uint32_t prop = crtc_prop(k, name);
	if (!prop) {
		fprintf(stderr, "kms: the crtc has no %s property\n", name);
		return -1;
	}
	uint32_t blob = 0;
	if (data && drmModeCreatePropertyBlob(k->fd, data, len, &blob) < 0) {
		fprintf(stderr, "kms: %s blob: %s\n", name, strerror(errno));
		return -1;
	}
	int ret = drmModeObjectSetProperty(k->fd, k->crtc_id, DRM_MODE_OBJECT_CRTC,
	                                   prop, blob);
	if (ret < 0)
		fprintf(stderr, "kms: set %s: %s\n", name, strerror(errno));
	/* The kernel retains its reference while the property uses this blob. */
	if (blob)
		drmModeDestroyPropertyBlob(k->fd, blob);
	return ret < 0 ? -1 : 0;
}

int kms_has_degamma(struct kms *k)
{
	uint32_t prop = crtc_prop(k, "DEGAMMA_LUT_SIZE");
	if (!prop)
		return 0;
	drmModeObjectProperties *props =
		drmModeObjectGetProperties(k->fd, k->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!props)
		return 0;
	uint64_t size = 0;
	for (uint32_t i = 0; i < props->count_props; i++)
		if (props->props[i] == prop)
			size = props->prop_values[i];
	drmModeFreeObjectProperties(props);
	return size == COLOR_DEGAMMA_LEN;
}

int kms_color_apply(struct kms *k, const struct color_profile *p)
{
	int ret = 0;

	if (!k->internal)
		p = NULL;

	/* Apply degamma first; only clear it if the CRTC exposes the property. */
	if (p && p->has_degamma) {
		static struct drm_color_lut dg[COLOR_DEGAMMA_LEN];
		for (int i = 0; i < COLOR_DEGAMMA_LEN; i++) {
			dg[i].red = p->degamma[i][0];
			dg[i].green = p->degamma[i][1];
			dg[i].blue = p->degamma[i][2];
			dg[i].reserved = 0;
		}
		ret |= set_blob(k, "DEGAMMA_LUT", dg, sizeof(dg));
	} else if (crtc_prop(k, "DEGAMMA_LUT")) {
		ret |= set_blob(k, "DEGAMMA_LUT", NULL, 0);
	}

	if (p && p->has_ctm) {
		struct drm_color_ctm ctm;
		for (int i = 0; i < 9; i++)
			ctm.matrix[i] = color_ctm_fixed(p->ctm[i]);
		ret |= set_blob(k, "CTM", &ctm, sizeof(ctm));
	} else {
		ret |= set_blob(k, "CTM", NULL, 0);
	}

	if (p && p->has_lut) {
		static struct drm_color_lut lut[COLOR_LUT_LEN];
		for (int i = 0; i < COLOR_LUT_LEN; i++) {
			lut[i].red = p->lut[i][0];
			lut[i].green = p->lut[i][1];
			lut[i].blue = p->lut[i][2];
			lut[i].reserved = 0;
		}
		ret |= set_blob(k, "GAMMA_LUT", lut, sizeof(lut));
	} else {
		ret |= set_blob(k, "GAMMA_LUT", NULL, 0);
	}
	return ret;
}
