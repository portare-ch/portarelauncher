# Fonts

Two bitmap fonts, both drawn from data generated into this repository.

## VGA 8x16

`src/font8x16.h`, the whole UI today, is the VGA 8x16 text-mode font from
Linux, `lib/fonts/font_8x16.c`, under the GPL 2.0 like the launcher.
`tools/mkfont.py` generates it and records where it comes from.

## Japanese, from Unifont

`tools/unifont-mockup.hex` carries the Unifont glyphs the mockups use. The
launcher's own header, once Japanese is built (docs/japanese.md), carries
the full set the same way. Both come from the Japanese build of GNU
Unifont, `unifont_jp` 18.0.01, cut by `tools/mkjafont.py`:
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
  this program's. `tools/unifont-mockup.hex` lists them in its header, and the
  generated header will too.

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
