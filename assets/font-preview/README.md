# Static native font previews

The three canonical 640x480 BMPs and static handheld launcher are already
committed. Their pixels and launcher behavior are unchanged by experiment cleanup:

- static-01-terminus-6x12.bmp: native Regular.
- static-02-terminus-8x14.bmp: identical comparison text, plus a separate real Bold sample.
- static-03-pattern-warning.bmp: red rounded-rectangle warning and icon over the
  actual production Pattern screen.

Both font sizes remain candidates; neither has been selected for production.
Paragraph spacing is 18px for 6x12 and 21px for 8x14; explicit BR uses 12/14px.
The warning composition is provisionally accepted. Its existing padding is
preserved; reducing top/bottom padding is a separate future task.

The exact three deploy copies are retained in deploy/font-preview-static because
the existing launcher needs image files beside it or in that Ports subdirectory.
The old multi-page screenshots, TTF comparisons and generated manifests are removed.
See tools/font_preview/README.md for the focused offline generator and static
handheld copy/run instructions. No production font integration is present.
