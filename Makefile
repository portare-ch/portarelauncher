# portarelauncher
#
# Two dependencies: libc and libdrm. If pkg-config cannot find libdrm you are
# missing the headers, not the library - the device ships libdrm.so.2 without
# them, which is why this is cross-built rather than built on the device.

CC       ?= cc
PKGCONFIG ?= pkg-config

CFLAGS   ?= -O2 -g
CFLAGS   += -std=c11 -Wall -Wextra -Wshadow -Wvla -Wno-unused-parameter
CFLAGS   += -D_GNU_SOURCE
CFLAGS   += $(shell $(PKGCONFIG) --cflags libdrm)
LDLIBS   += $(shell $(PKGCONFIG) --libs libdrm)

SRC  := src/settings.c src/status.c src/osd.c src/term.c src/kms.c src/input.c src/catalog.c src/main.c
OBJ  := $(SRC:.c=.o)
BIN  := portarelauncher

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

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
