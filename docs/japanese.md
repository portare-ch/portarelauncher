# Japanese

The launcher in English or Japanese, and in nothing else. The language is
`system.language` in system.cfg, `en_US` or `ja_JP`: the key the scraper
already reads, so there is no second place that says which language the
device speaks.

![Systems](images/ja-systems.png)

![Games, a long title scrolled to its end](images/ja-games.png)

![Recently played](images/ja-recently-played.png)

![Settings](images/ja-settings.png)

![Settings > Language & region](images/ja-language-region.png)

![The same in English](images/language-region.png)

The pictures are `tools/mockup.py`'s `ja_*` screens, rendered by
`tools/mockup_png.py` with the launcher's font and the glyphs below.

## The font

Kana and kanji are 16x16 glyphs from the Japanese build of GNU Unifont,
`unifont_jp` 18.0.01, and each takes two cells of the 8x16 grid. ASCII
stays the VGA font. The result is a PC-98 text screen, which is where the
launcher's DOS look leads in Japanese anyway.

Most of Unifont's Japanese glyphs come from the public domain Izumi16 font,
9,880 of them, JIS X 0213. A few, the ideographic full stop 。 and the
repetition mark 々 among them, are Unifont's own, under the SIL OFL 1.1 or
the GPL 2.0 or later with the font embedding exception. Either suits the
launcher's GPL 2.0. The Izumi16 set and those few are generated into a
header the way `font8x16.h` is, about 320 KB in the binary.

The mockups carry only the 138 glyphs they use, in `tools/ja16-mockup.hex`,
cut from the full font by `tools/mkjafont.py`.

## Text

Strings and titles are UTF-8 and are decoded. Today every byte is drawn as
a CP437 glyph, so a title such as "Pokémon" comes out as "Pok├⌐mon";
decoding fixes the accented Latin letters CP437 has as well. A character
neither font has is drawn as `?`.

A wide glyph fills two cells, and nothing ever draws half of one. Writing
over either half clears the other. A title cut to its column loses whole
characters, and a scrolling title moves one character a step; a wide
character takes two steps' time, so the text moves at an even speed.

Titles render in either language, since a gamelist can hold Japanese names
whatever the menus say. The language changes the menus, the hints and the
console names.

## Order

Lists sort by code point: Latin first, then kana in their own order, then
kanji in no order a reader would expect, as in the games mockup. Readings
are not in the data, so a gamelist `<sortname>` decides where it has one.

## Words

Every string the launcher shows lives in one table per language, compiled
in. A test checks that each fits where it is drawn, in both languages, by
width, not by length.

Console names are the ones the machines were sold under in Japan:
スーパーファミコン, メガドライブ, ニンテンドウ64. The short names of the
cross-system lists follow, SFC and FC rather than SNES and NES.

The hints keep the key letters and translate the verb: `A 起動`, `B 戻る`,
`Y お気に入り`, `Y 解除`.

## Settings

Settings stays at thirteen rows. Time zone becomes Language & region,
言語と地域, a submenu with the language and the time zone. Its value is
the language, named in its own script.

The language row names itself in both languages, `言語 / Language` and
`Language / 言語`, the one place that does. Whoever switched by mistake has
to find the way back without reading the language they switched to. The
values are each language in its own script, English and 日本語. A, left or
right switches, as Button style does, and the screen redraws in the new
language at once.

## Not part of it

- The on-screen keyboard stays Latin: it types Wi-Fi passwords and network
  names. Its hints are translated.
- PortScope keeps the kernel's event names and the labels printed on the
  device: L1, SELECT, HOME, START.
- Japanese input, other languages, and RetroArch's own menu language.
- Scraping names in Japanese. The scraper reads `system.language` and
  lists en, fr, es, de and pt today; whether `ja_JP` should fetch
  Japanese names is its own question.

## Open

Izumi16 draws with one-pixel strokes and the VGA font mostly with two, so
a row that mixes them looks uneven; `MOTHER2 ギーグの逆襲` in the games
mockup shows it. Drawing kana and kanji one pixel bolder matches the
weight but fills dense kanji such as 魔 and 襲. That is for the panel to
decide, not a mockup.
