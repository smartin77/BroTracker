# Offline native font-preview generator

This small host-only tool exports the existing three static 640x480 BMP fixtures.
It retains native Terminus 6x12 Regular and 8x14 Regular/Bold without choosing a
production font. Original BT font loading and pattern-grid geometry are unchanged.

The generator reads the three checked-in BDFs, decodes UTF-8, wraps continuous
paragraphs at 1.5 native line height and keeps explicit BR at native spacing.
Both Regular images use the same comparison text and available width; bitmap
pixels are not rescaled. Missing-glyph fallback and bounds guards stay local to
this tool. This is not a production font subsystem or a complete BDF implementation.
The restricted reader supports the retained fixed-width horizontal BDF 2.1 faces.

The warning fixture calls the production RenderMainScreen path through
`tools/ui_preview/pattern_fixture.h`, shared with the existing UI preview.
Its panel geometry, icon, text, red colors, background dimming and padding are
unchanged. The composition is provisionally accepted; a future task will reduce
vertical padding by roughly one third to one half. No padding adjustment is made
here, and no device detection/blocking behavior is implemented.

The original SVG icon and provenance remain in assets/icons. A restricted,
standard-library Python converter produces its native 64x64 palette header at
build time. There is no runtime SVG loader, SDL viewer, TTF rasterizer or ARM64
application in this tool.

## Generation reference

The existing OFF-by-default BROTRACKER_BUILD_FONT_PREVIEW option retains the
BroTrackerFontPreview target as an offline generator only. Its build-time Python
dependency is confined to the SVG conversion. No font-preview tests are registered.
For a future authorized regeneration, from the repository root:

```sh
cmake -S . -B build -DBROTRACKER_BUILD_FONT_PREVIEW=ON
cmake --build build --target BroTrackerFontPreview
./build/BroTrackerFontPreview . assets/font-preview --static-three
```

On Windows use the existing configured MinGW toolchain and the `.exe` suffix.
The optional `--static-three` spelling is retained; supplying only the asset root
and output directory also exports the same three files. No build, test or image
regeneration was performed during cleanup. The generator was reduced by source
editing only; a future build will establish its post-cleanup compilation status.

## Static handheld delivery

The existing committed launcher at
`deploy/font-preview-static/BroTracker Font Images.sh` displays existing images
using an available mpv or ffplay. It imports only the existing installed
`brotracker/midi-connect.py` button-reading function; it does not run that helper's
main routine, start MIDI routing or change normal BTX configuration. Python 3 and
the existing R36H gamepad access are required. No utility installation or physical
handheld execution is claimed by this cleanup.

After transferring the repository through the owner's normal Git workflow, copy
from the checkout on the handheld:

```sh
cp -r deploy/font-preview-static /roms/ports/
cp 'deploy/font-preview-static/BroTracker Font Images.sh' /roms/ports/
chmod +x '/roms/ports/BroTracker Font Images.sh'
```

Select **BroTracker Font Images** from Ports, or run:

```sh
bash '/roms/ports/BroTracker Font Images.sh'
```

A new gamepad button press advances to the next image; a press on the third image
exits. Existing normal BTX files are not replaced. There is no on-device font
rendering, image regeneration, custom viewer or preview build/package framework.

## Format finding retained for future integration

Production BTF loads numeric BMP keys but crops BFM rows 5..11 to seven byte rows;
text rendering reads five columns with a fixed 6px pattern-grid advance. BTF width,
height and spacing fields do not currently resize rendering. The production UTF-8
path handles only one/two-byte sequences. Native 6x12 and 8x14 therefore cannot be
losslessly substituted through BTF metadata alone. This experiment leaves that
format and all production text behavior unchanged.

Font licenses and pinned provenance are in assets/fonts/terminus/SOURCES.md and
OFL-bitmap.txt. TTF assets, stb dependency/notices, interactive viewing modes,
comparison-only test/package scripts, old screenshots and generated experiment
reports/caches were removed. Neither native bitmap size is selected yet.
