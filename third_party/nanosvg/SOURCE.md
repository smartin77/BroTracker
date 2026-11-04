# NanoSVG

Upstream: https://github.com/memononen/nanosvg

Pinned revision: `239e102ec2c691f2902e20ace2ed36ee4a35cfe6`

Vendored unmodified `src/nanosvg.h`, `src/nanosvgrast.h` and `LICENSE.txt`.
Copyright Mikko Mononen; zlib license (see LICENSE.txt and header notices).

CPU-only portable C implementation, compiled exclusively into host UI targets.
No runtime DLL, GPU backend, or firmware dependency. SVG paths remain runtime
assets; parsed cubic geometry is retained and rasterized at the requested size.
NanoSVG supports the basic paths, fills, strokes, opacity and round joins/caps
used here, not arbitrary SVG/CSS/filter/text features. Upstream is not actively
maintained; this integration uses bundled application artwork, not remote SVGs.
