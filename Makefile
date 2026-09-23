# portarelauncher
#
# Two dependencies: libc and libdrm.
#
# PKG_CONFIG rather than a name of our own: that is the variable cross build
# systems export to point at the target's pkg-config. Calling bare
# "pkg-config" finds the host's, or nothing at all, and quietly produces no
# flags - which surfaces a hundred lines later as a missing <xf86drm.h>.
# Hence the error below rather than a silent miscompile.

CC         ?= cc
PKG_CONFIG ?= pkg-config

DRM_CFLAGS := $(shell $(PKG_CONFIG) --cflags libdrm 2>/dev/null)
DRM_LIBS   := $(shell $(PKG_CONFIG) --libs libdrm 2>/dev/null)

ifeq ($(strip $(DRM_LIBS)),)
$(error libdrm not found by '$(PKG_CONFIG)'. Point PKG_CONFIG at the one for \
the target, or install the libdrm development headers. The device ships \
libdrm.so.2 without them, which is why this is cross-built)
endif

# Kept out of CFLAGS deliberately. A build system that passes CFLAGS on the
# make command line overrides everything a makefile appends to it, which
# would silently drop both the language level and the libdrm include path.
# Applied in the recipes instead, where nothing can displace them.
PL_CFLAGS  := -std=c11 -D_GNU_SOURCE $(DRM_CFLAGS)
PL_WARN    := -Wall -Wextra -Wshadow -Wvla -Wno-unused-parameter

CFLAGS     ?= -O2 -g

SRC  := src/net.c src/settings.c src/status.c src/osd.c src/term.c src/kms.c \
        src/input.c src/catalog.c src/main.c
OBJ  := $(SRC:.c=.o)
BIN  := portarelauncher

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(PL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(DRM_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(PL_CFLAGS) $(PL_WARN) -c -o $@ $<

src/term.o: src/font8x16.h

clean:
	rm -f $(OBJ) $(BIN)

# Regenerates the font from the kernel's VGA console font. Needs the source
# file; see tools/mkfont.py for where it comes from.
font: tools/mkfont.py
	python3 tools/mkfont.py $(FONT_SRC)

# Regenerates docs/mockup.txt, and fails if any row overflows the grid.
mockup:
	python3 tools/mockup.py > docs/mockup.txt

.PHONY: all clean font mockup
