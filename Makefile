# Requires libc and libdrm. Honor the cross-build system's PKG_CONFIG
# to avoid silently using host headers and libraries.

CC         ?= cc
PKG_CONFIG ?= pkg-config

DRM_CFLAGS := $(shell $(PKG_CONFIG) --cflags libdrm 2>/dev/null)
DRM_LIBS   := $(shell $(PKG_CONFIG) --libs libdrm 2>/dev/null)

# Require libdrm for device builds; host tests and mockups do not need it.
GOALS := $(or $(MAKECMDGOALS),all)
ifneq ($(filter-out clean font mockup test test-chd test-chd-san install-disc-titles ps-chd-id disc-title-tables regen-disc-title-tables tests/%,$(GOALS)),)
ifeq ($(strip $(DRM_LIBS)),)
$(error libdrm not found by '$(PKG_CONFIG)'. Point PKG_CONFIG at the one for \
the target, or install the libdrm development headers. The device ships \
libdrm.so.2 without them, which is why this is cross-built)
endif
endif

# Keep required flags separate: command-line CFLAGS overrides makefile
# assignments. Apply PL_CFLAGS in recipes so those flags are retained.
PL_CFLAGS  := -std=c11 -D_GNU_SOURCE $(DRM_CFLAGS)
PL_WARN    := -Wall -Wextra -Wshadow -Wvla -Wno-unused-parameter

CFLAGS     ?= -O2 -g

# Release archives get VERSION via export-subst; checkouts use git describe.
# Fall back to "dev" when neither is available.
PL_VERSION := $(strip $(shell cat VERSION 2>/dev/null))
ifneq ($(findstring $$Format,$(PL_VERSION)),)
PL_VERSION := $(strip $(shell git describe --tags --always --dirty 2>/dev/null))
endif
ifeq ($(PL_VERSION),)
PL_VERSION := dev
endif

SRC  := src/disc_titles.c src/ps_serial.c src/text.c src/proc.c src/update.c src/osinfo.c src/tz.c src/quit.c src/idle.c src/orbit.c src/sheets.c src/color.c src/net.c src/bt.c src/osk.c src/tools.c src/settings.c src/status.c src/notify.c src/term.c src/pix.c src/glyph.c src/unifont.c src/ja26.c src/lang.c src/kms.c \
        src/fit.c src/modes.c src/uevent.c src/input.c src/catalog.c src/lists.c src/main.c
OBJ  := $(SRC:.c=.o)
BIN  := portarelauncher

# PortScope shares the launcher's display code and has its own input model.
SCOPE_SRC := src/portscope.c src/scope.c src/pix.c src/term.c src/kms.c src/fit.c src/modes.c \
             src/color.c src/quit.c src/settings.c src/glyph.c src/unifont.c src/ja26.c
SCOPE_OBJ := $(SCOPE_SRC:.c=.o)
SCOPE_BIN := portscope

all: $(BIN) $(SCOPE_BIN) ps-chd-id disc-title-tables

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(PL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(DRM_LIBS)

$(SCOPE_BIN): $(SCOPE_OBJ)
	$(CC) $(CFLAGS) $(PL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(DRM_LIBS) -lm

%.o: %.c
	$(CC) $(CFLAGS) $(PL_CFLAGS) $(PL_WARN) -c -o $@ $<

src/term.o: src/font8x16.h

# Only main.o needs the version string.
src/main.o: PL_CFLAGS += -DPL_VERSION='"$(PL_VERSION)"'

clean:
	rm -f $(OBJ) $(SCOPE_OBJ) $(BIN) $(SCOPE_BIN) $(TESTS) ps-chd-id

# Regenerate the VGA font; FONT_SRC points to Linux's font_8x16.c.
font: tools/mkfont.py
	python3 tools/mkfont.py $(FONT_SRC)

# Regenerates docs/mockup.txt, and fails if any row overflows the grid.
mockup:
	python3 tools/mockup.py > docs/mockup.txt
	python3 tools/mockup_png.py

.PHONY: all clean font mockup test

# Host tests need no libdrm or devices. Radio tests replay portnet and
# bluetoothctl output through fake_proc.c.
#
#   make test          build and run all tests
#   make test SAN=1    enable AddressSanitizer and UBSan

TEST_CFLAGS := -std=c11 -D_GNU_SOURCE -Isrc -Itests -O1 -g -Werror
ifeq ($(SAN),1)
TEST_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer \
               -fno-sanitize-recover=all
endif

TESTS := tests/test_text tests/test_settings tests/test_proc \
         tests/test_catalog tests/test_metadata_input tests/test_disc_titles tests/test_tools tests/test_net tests/test_bt \
         tests/test_osk tests/test_update tests/test_osinfo tests/test_tz tests/test_quit tests/test_sheets tests/test_color tests/test_lists \
         tests/test_scope tests/test_pix tests/test_term tests/test_glyph tests/test_lang tests/test_ja26 \
         tests/test_idle tests/test_orbit tests/test_fit tests/test_uevent tests/test_modes

tests/test_text:     src/text.c
tests/test_settings: src/settings.c
tests/test_proc:     src/proc.c src/text.c
tests/test_catalog:  src/disc_titles.c src/ps_serial.c src/proc.c src/catalog.c src/settings.c src/sheets.c src/text.c src/glyph.c src/unifont.c
ifeq ($(shell uname -s),Linux)
tests/test_metadata_input: src/input.c src/proc.c src/quit.c
endif
tests/test_disc_titles: src/ps_sfo.c src/disc_titles.c src/ps_serial.c src/ps_iso.c src/proc.c src/catalog.c src/settings.c src/sheets.c src/text.c src/glyph.c src/unifont.c
tests/test_tools:    src/tools.c src/text.c
tests/test_net:      src/net.c src/text.c tests/fake_proc.c
tests/test_bt:       src/bt.c src/text.c src/settings.c tests/fake_proc.c
tests/test_osk:      src/osk.c src/term.c src/pix.c src/glyph.c src/unifont.c src/ja26.c
tests/test_update:   src/update.c src/text.c src/settings.c tests/fake_proc.c
tests/test_osinfo:   src/osinfo.c src/text.c
tests/test_tz:       src/tz.c src/text.c src/settings.c tests/fake_proc.c
tests/test_quit:     src/quit.c
tests/test_idle:     src/idle.c
tests/test_orbit:    src/orbit.c
tests/test_fit:      src/fit.c
tests/test_uevent:   src/uevent.c
tests/test_modes:    src/modes.c
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

# Decoder and codecs stay in this helper; the launcher links only libc/libdrm.
CMAKE ?= cmake
PS_BUILD ?= build/ps-helper
PS_CMAKE_ARGS ?=
ps-chd-id: src/ps_sfo.c src/ps_sfo.h tools/ps-chd-id.c src/ps_iso.c src/ps_iso.h src/ps_serial.c src/ps_serial.h src/xbox_iso.c src/xbox_iso.h tools/ps-helper/CMakeLists.txt
	$(CMAKE) -S tools/ps-helper -B $(PS_BUILD) -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_C_COMPILER="$(CC)" $(PS_CMAKE_ARGS)
	+$(CMAKE) --build $(PS_BUILD) --target ps-chd-id
	cp $(PS_BUILD)/ps-chd-id $@

disc-title-tables: data/ps2-titles.tsv data/psx-titles.tsv data/psp-titles.tsv data/neocd-titles.tsv data/xbox-titles.tsv

# Normal builds use the checked-in tables. Only explicit regeneration needs
# Python/PyYAML and the losslessly compressed upstream catalogues.
regen-disc-title-tables:
	python3 tools/gen_ps2_titles.py third_party/pcsx2/GameIndex.yaml.xz data/ps2-titles.tsv
	python3 tools/gen_psx_titles.py third_party/libretro/PlayStation.dat.xz data/psx-titles.tsv --collisions data/psx-title-collisions.json
	python3 tools/gen_psp_titles.py third_party/libretro/PlayStationPortable.dat.xz data/psp-titles.tsv --collisions data/psp-title-collisions.json
	python3 tools/gen_neocd_titles.py third_party/libretro/NeoGeoCD.dat.xz data/neocd-titles.tsv --collisions data/neocd-title-collisions.json
	python3 tools/gen_xbox_titles.py third_party/libretro/Xbox.dat.xz data/xbox-titles.tsv --collisions data/xbox-title-collisions.json

test-chd: ps-chd-id disc-title-tables
	python3 tests/test_ps_chd.py ./ps-chd-id
	python3 tests/test_title_generators.py

.PHONY: disc-title-tables regen-disc-title-tables test-chd

# ASan needs its large virtual address mapping; production memory bounds stay on.
test-chd-san: disc-title-tables
	$(CMAKE) -S tools/ps-helper -B build/ps-helper-san -DCMAKE_BUILD_TYPE=Debug -DPS_ID_SANITIZED=ON -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
	$(CMAKE) --build build/ps-helper-san --target ps-chd-id
	python3 tests/test_ps_chd.py build/ps-helper-san/ps-chd-id

# Device packaging can call this alongside its existing launcher/font install.
PREFIX ?= /usr
install-disc-titles: ps-chd-id disc-title-tables
	install -Dm755 ps-chd-id $(DESTDIR)$(PREFIX)/bin/ps-chd-id
	install -Dm644 data/ps2-titles.tsv $(DESTDIR)$(PREFIX)/share/portarelauncher/ps2-titles.tsv
	install -Dm644 data/psx-titles.tsv $(DESTDIR)$(PREFIX)/share/portarelauncher/psx-titles.tsv
	install -Dm644 data/psp-titles.tsv $(DESTDIR)$(PREFIX)/share/portarelauncher/psp-titles.tsv
	install -Dm644 data/neocd-titles.tsv $(DESTDIR)$(PREFIX)/share/portarelauncher/neocd-titles.tsv
	install -Dm644 data/xbox-titles.tsv $(DESTDIR)$(PREFIX)/share/portarelauncher/xbox-titles.tsv
	install -Dm644 third_party/libretro/NOTICE $(DESTDIR)$(PREFIX)/share/portarelauncher/notices/LIBRETRO-NOTICE.txt
	install -Dm644 third_party/libretro/LICENSE $(DESTDIR)$(PREFIX)/share/portarelauncher/notices/LIBRETRO-LICENSE.txt
	install -Dm644 third_party/CHD-NOTICES.txt $(DESTDIR)$(PREFIX)/share/portarelauncher/notices/CHD-NOTICES.txt
	install -Dm644 third_party/pcsx2/NOTICE $(DESTDIR)$(PREFIX)/share/portarelauncher/notices/PCSX2-NOTICE.txt
	install -Dm644 third_party/pcsx2/COPYING.GPLv3 $(DESTDIR)$(PREFIX)/share/portarelauncher/notices/PCSX2-COPYING.GPLv3

.PHONY: ps-chd-id test-chd-san install-disc-titles
