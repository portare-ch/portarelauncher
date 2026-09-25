/* Mode setting, one connector, one dumb buffer, no GPU.
 *
 * A text screen on a black field does not need GBM, EGL, Vulkan or Mesa. It
 * needs a buffer the display controller can scan out and somewhere to write
 * pixels, which is what a dumb buffer is.
 *
 * There is deliberately one buffer and no page flipping. The screen is static
 * almost all the time, so there is nothing to flip; changed cells are written
 * straight into the buffer being scanned out. The cost of that is a torn cell
 * if the scanout passes over it mid-write, which for a menu is invisible and
 * much cheaper than holding a second buffer and waiting on vblank.
 */
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
	drmModeModeInfo mode;
	drmModeCrtc *saved;   /* whatever was on the CRTC when we arrived */
	uint32_t *map;
	size_t size;
	unsigned pitch_px;
};

/* Opens the card, picks the connected connector and its preferred mode,
 * allocates a dumb buffer and scans it out. */
int  kms_open(struct kms *k, const char *card);
void kms_close(struct kms *k);

/* Points the CRTC back at our buffer. Needed after anything else has held
 * DRM master, because it will have set its own. */
int  kms_present(struct kms *k);

/* Blanks by disabling the CRTC, which powers the panel down rather than
 * drawing black on it - on an OLED the difference is the backlight-equivalent
 * rather than a dark picture. */
int  kms_blank(struct kms *k);

int  kms_drop_master(struct kms *k);
int  kms_set_master(struct kms *k);

/* Writes a color profile into the CRTC's CTM and GAMMA_LUT properties, or
 * clears both with NULL. The display controller keeps them across every
 * later modeset, so what is set here stays in force for whatever runs after
 * the launcher has dropped master. Needs master. */
struct color_profile;
int  kms_color_apply(struct kms *k, const struct color_profile *p);

/* Whether the CRTC has a de-gamma stage (the DEGAMMA_LUT property with
 * entries): a kernel that drives the display controller's IGC. */
int  kms_has_degamma(struct kms *k);

#endif
