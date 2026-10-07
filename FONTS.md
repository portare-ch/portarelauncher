# Fonts

Three fonts, all drawn from data generated into this repository.

## VGA 8x16

`src/font8x16.h`, the whole UI today, is the VGA 8x16 text-mode font from
Linux, `lib/fonts/font_8x16.c`, under the GPL 2.0 like the launcher.
`tools/mkfont.py` generates it and records where it comes from.

## Kana and kanji, from Noto Sans CJK JP

`data/ja26.bin` holds the kana and kanji as the launcher draws them: Noto
Sans CJK JP Medium, version 2.004, rasterised at 26 x 26 pixels with 16
grey levels by `tools/mkja26.py`. That is a modified version of the font
under the SIL Open Font License 1.1, so `data/ja26.NOTICE`, with the
font's copyright, and `data/OFL-1.1.txt`, the licence, go wherever the
glyphs go: the launcher's package installs all three to
`/usr/share/portarelauncher`.

> © 2014-2021 Adobe (http://www.adobe.com/).

The font is reached by the name Noto; Adobe's reserved name for it,
Source, is not used here. `tools/ja26-mockup.hex` carries the glyphs the
mockups use, under the same notice.

## Japanese, from Unifont

`src/unifont.c` carries the full set the launcher draws with, generated
by `tools/mkunifont.py`; `tools/unifont-mockup.hex` carries the glyphs the
mockups use, cut by `tools/mkjafont.py`. Both come from the Japanese build
of GNU Unifont, `unifont_jp` 18.0.01:
https://unifoundry.com/pub/unifont/unifont-18.0.01/

Unifont's notice, from the font's own COPYRIGHT property:

> Copyright (C) 1998-2026 Roman Czyborra, Paul Hardy, Qianqian Fang,
> Andrew Miller, Johnnie Weaver, David Corbett, Ælla Chiana Moskopp,
> Rebecca Bettencourt, Minseo Lee, Ho-Seok Ee, et al. License: SIL Open
> Font License version 1.1 and GPLv2+: GNU GPL version 2 or later
> <http://gnu.org/licenses/gpl.html> with the GNU Font Embedding Exception.

- **Izumi16 glyphs** are public domain. Most of the kana and kanji are
  these, from the Izumi BDF font that `unifont_jp` is built with
  (`font/plane00/izmg16-plane00.hex` in Unifont's source).
- **Unifont's own glyphs** are used under the GPL 2.0 or later with the GNU
  Font Embedding Exception, the one of Unifont's two licences that matches
  this program's. `src/unifont.c` and `tools/unifont-mockup.hex` list them
  in their headers.

The exception, as Unifont states it:

> As a special exception, if you create a document which uses this font,
> and embed this font or unaltered portions of this font into the
> document, this font does not by itself cause the resulting document to
> be covered by the GNU General Public License. This exception does not
> however invalidate any other reasons why the document might be covered
> by the GNU General Public License. If you modify this font, you may
> extend this exception to your version of the font, but you are not
> obligated to do so. If you do not wish to do so, delete this exception
> statement from your version.
