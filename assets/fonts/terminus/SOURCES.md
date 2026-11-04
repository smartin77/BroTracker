# Native Terminus bitmap assets

Runtime warnings default to native 8x14 Regular/genuine Bold. Native 6x12 Regular
remains selectable for both heading and body through `kWarningFont`.
These are UI host resources, not firmware assets.

| File | Native cell/style | Bytes | SHA-256 |
|---|---|---:|---|
| ter-u12n.bdf | 6x12 Regular | 164741 | 9e942cad8173fddf34fc8a784e9ec6d2d4cfb8b82363faf65f43b236fe2f5a99 |
| ter-u14n.bdf | 8x14 Regular | 172877 | fb6aaad8bebe5ee824e914635b3f4e17a82f1673dea37f083e9adde285d7de32 |
| ter-u14b.bdf | 8x14 Bold | 172873 | 00dd0c08c3fae274aa66161d89ffa900cf0b0efec0f114255188c1fb68a8b0da |

Each unchanged ISO10646 BDF contains 1,356 encoded characters, including the
requested Slovak letters. Original Terminus has no native 6x12 Bold; none is
synthesized. BDF files are text, not binary assets.

## Source and license

Original Terminus Font 4.49.1 by Dimitar Toshkov Zhekov. Retrieved 2026-11-04.
Authoritative project and release:
https://terminus-font.sourceforge.net/
https://sourceforge.net/projects/terminus-font/files/terminus-font-4.49/

SourceForge's download endpoints returned HTML in the development environment.
The three BDFs and original OFL.TXT were downloaded unchanged from this pinned
release mirror:
https://github.com/mikebeaton/terminus-font-4.49.1/tree/6905a7bc9a12571a83917979a16c3218b836ce08

Raw source URL example:
https://raw.githubusercontent.com/mikebeaton/terminus-font-4.49.1/6905a7bc9a12571a83917979a16c3218b836ce08/ter-u12n.bdf

The original OFL.TXT is retained as OFL-bitmap.txt: SIL Open Font License 1.1,
Copyright (C) 2020 Dimitar Toshkov Zhekov, reserved name "Terminus Font".
BDF properties also carry the copyright and license notice. No independent byte
verification against the unavailable SourceForge archive is claimed.

The larger bitmap family, TTF conversion, italic fonts and upstream build tools
are not retained. Their licenses must not be inferred from this bitmap notice.
The three retained fonts and their provenance are independent of the static
preview screenshots or a future production font choice.
