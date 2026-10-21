/* KMS with one connector and one dumb buffer, written directly without
 * page flips. Static menus avoid a second buffer and vblank waits at the
 * cost of possible tearing during cell updates. VRR lets the panel settle
 * at its minimum refresh rate while idle. */
#ifndef PL_KMS_H
#define PL_KMS_H

#include <stddef.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct kms {
	int fd;
	uint32_t conn_id;
	uint32_t crtc_id;
	uint32_t fb_id;
	uint32_t handle;
	drmModeModeInfo mode;  /* see modes.h */
	int internal;          /* the built-in panel rather than a display */
	int vrr;               /* variable refresh asked for */
	drmModeCrtc *saved;   /* whatever was on the CRTC when we arrived */
	uint32_t *map;
	size_t size;
	unsigned pitch_px;
};

/* Opens the card, picks the connected connector and its mode, allocates
 * a dumb buffer and scans it out. Every other CRTC is switched off. */
int  kms_open(struct kms *k, const char *card);
void kms_close(struct kms *k);

/* After a hotplug, moves to the connector that is now connected, with a
 * new buffer the size of its mode. Returns 1 when it moved, 0 when the
 * output stays, -1 when nothing is driven any more. Needs DRM master. */
int  kms_follow(struct kms *k);

/* Restores our buffer after another DRM master changed the scanout. */
int  kms_present(struct kms *k);

/* Requests VRR at the selected mode. Reapplied by kms_present and reset by
 * kms_close. Failure is reported once and leaves fixed refresh. */
int  kms_vrr(struct kms *k, int on);

/* Disables the CRTC to power down the panel. */
int  kms_blank(struct kms *k);

int  kms_drop_master(struct kms *k);
int  kms_set_master(struct kms *k);

/* Applies a color profile, or clears it with NULL. Requires DRM master.
 * CRTC properties persist across modesets and master handoffs. A display
 * is always cleared: the profiles are the panel's calibration. */
struct color_profile;
int  kms_color_apply(struct kms *k, const struct color_profile *p);

/* Returns whether the CRTC exposes a 256-entry DEGAMMA_LUT stage. */
int  kms_has_degamma(struct kms *k);

#endif
