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

# Only when something is actually being compiled: `make mockup` regenerates a
# text file and has no business demanding a graphics library, least of all on
# the laptop this is written on.
GOALS := $(or $(MAKECMDGOALS),all)
ifneq ($(filter-out clean font mockup test tests/%,$(GOALS)),)
ifeq ($(strip $(DRM_LIBS)),)
$(error libdrm not found by '$(PKG_CONFIG)'. Point PKG_CONFIG at the one for \
the target, or install the libdrm development headers. The device ships \
libdrm.so.2 without them, which is why this is cross-built)
endif
endif

# Kept out of CFLAGS deliberately. A build system that passes CFLAGS on the
# make command line overrides everything a makefile appends to it, which
# would silently drop both the language level and the libdrm include path.
# Applied in the recipes instead, where nothing can displace them.
PL_CFLAGS  := -std=c11 -D_GNU_SOURCE $(DRM_CFLAGS)
PL_WARN    := -Wall -Wextra -Wshadow -Wvla -Wno-unused-parameter

CFLAGS     ?= -O2 -g

# The version shown under Settings > About. A release tarball has it in
# VERSION, which git archive fills in from the tag (export-subst, see
# .gitattributes). A checkout has the placeholder instead, so it asks git,
# and a copy with neither says "dev".
PL_VERSION := $(strip $(shell cat VERSION 2>/dev/null))
ifneq ($(findstring $$Format,$(PL_VERSION)),)
PL_VERSION := $(strip $(shell git describe --tags --always --dirty 2>/dev/null))
endif
ifeq ($(PL_VERSION),)
PL_VERSION := dev
endif

SRC  := src/text.c src/proc.c src/update.c src/osinfo.c src/tz.c src/quit.c src/idle.c src/sheets.c src/color.c src/net.c src/bt.c src/osk.c src/tools.c src/settings.c src/status.c src/notify.c src/term.c src/pix.c src/glyph.c src/unifont.c src/ja26.c src/lang.c src/kms.c \
        src/input.c src/catalog.c src/lists.c src/main.c
OBJ  := $(SRC:.c=.o)
BIN  := portarelauncher

# PortScope, the input diagnostics the launcher opens from Settings: the
# launcher's panel, grid and palette, and its own model of the pad.
SCOPE_SRC := src/portscope.c src/scope.c src/pix.c src/term.c src/kms.c \
             src/color.c src/quit.c src/settings.c src/glyph.c src/unifont.c src/ja26.c
SCOPE_OBJ := $(SCOPE_SRC:.c=.o)
SCOPE_BIN := portscope

all: $(BIN) $(SCOPE_BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(PL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(DRM_LIBS)

$(SCOPE_BIN): $(SCOPE_OBJ)
	$(CC) $(CFLAGS) $(PL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(DRM_LIBS) -lm

%.o: %.c
	$(CC) $(CFLAGS) $(PL_CFLAGS) $(PL_WARN) -c -o $@ $<

src/term.o: src/font8x16.h

# Only main.c prints it, so only main.o carries it.
src/main.o: PL_CFLAGS += -DPL_VERSION='"$(PL_VERSION)"'

clean:
	rm -f $(OBJ) $(SCOPE_OBJ) $(BIN) $(SCOPE_BIN) $(TESTS)

# Regenerates the font from the kernel's VGA console font. Needs the source
# file; see tools/mkfont.py for where it comes from.
font: tools/mkfont.py
	python3 tools/mkfont.py $(FONT_SRC)

# Regenerates docs/mockup.txt, and fails if any row overflows the grid.
mockup:
	python3 tools/mockup.py > docs/mockup.txt
	python3 tools/mockup_png.py

.PHONY: all clean font mockup test

# ---- tests ------------------------------------------------------------------
#
# Host programs, one per module, linked against only what they test - never
# kms.c or input.c, so they need no libdrm and run on the laptop as well as in
# CI. bt.c and net.c are linked with tests/fake_proc.c instead of proc.c, which
# replays recorded nmcli and bluetoothctl output rather than running them.
#
#   make test          build and run them all
#   make test SAN=1    the same under AddressSanitizer and UBSan

TEST_CFLAGS := -std=c11 -D_GNU_SOURCE -Isrc -Itests -O1 -g -Werror
ifeq ($(SAN),1)
TEST_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer \
               -fno-sanitize-recover=all
endif

TESTS := tests/test_text tests/test_settings tests/test_proc \
         tests/test_catalog tests/test_tools tests/test_net tests/test_bt \
         tests/test_osk tests/test_update tests/test_osinfo tests/test_tz tests/test_quit tests/test_sheets tests/test_color tests/test_lists \
         tests/test_scope tests/test_pix tests/test_term tests/test_glyph tests/test_lang tests/test_ja26 \
         tests/test_idle

tests/test_text:     src/text.c
tests/test_settings: src/settings.c
tests/test_proc:     src/proc.c src/text.c
tests/test_catalog:  src/catalog.c src/settings.c src/sheets.c src/text.c src/glyph.c src/unifont.c
tests/test_tools:    src/tools.c src/text.c
tests/test_net:      src/net.c src/text.c tests/fake_proc.c
tests/test_bt:       src/bt.c src/text.c src/settings.c tests/fake_proc.c
tests/test_osk:      src/osk.c src/term.c src/pix.c src/glyph.c src/unifont.c src/ja26.c
tests/test_update:   src/update.c src/text.c src/settings.c tests/fake_proc.c
tests/test_osinfo:   src/osinfo.c src/text.c
tests/test_tz:       src/tz.c src/text.c src/settings.c tests/fake_proc.c
tests/test_quit:     src/quit.c
tests/test_idle:     src/idle.c
tests/test_sheets:   src/sheets.c
tests/test_color:   src/color.c
tests/test_lists:    src/lists.c src/text.c
tests/test_scope:    src/scope.c
tests/test_pix:      src/pix.c
tests/test_term:     src/term.c src/pix.c src/glyph.c src/unifont.c src/ja26.c
tests/test_glyph:    src/glyph.c src/unifont.c
tests/test_lang:     src/lang.c src/glyph.c src/unifont.c
tests/test_ja26:     src/ja26.c

$(TESTS): %: %.c tests/check.h tests/ja26_fixture.h
	$(CC) $(TEST_CFLAGS) $(PL_WARN) -o $@ $(filter %.c,$^)

# Always rebuilt: SAN=1 and a plain run must not reuse each other's binaries.
test:
	@rm -f $(TESTS)
	@$(MAKE) --no-print-directory $(TESTS)
	@fail=0; for t in $(TESTS); do ./$$t || fail=1; done; exit $$fail
