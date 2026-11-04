"""Rasterize the fixed warning SVG subset, not a general SVG implementation.
GPL-3.0-or-later; BroTracker contributors. Standard-library Python 3.6+ only.
"""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET


def rasterize(path):
    root = ET.parse(str(path)).getroot()
    assert root.attrib == {"width": "64", "height": "64", "viewBox": "0 0 64 64"}
    pixels = [0] * (64 * 64)
    for element in root:
        tag = element.tag.split("}")[-1]
        attr = dict(element.attrib)
        color = {"#ff4b4b": 1, "#101010": 2}[attr.pop("fill")]
        if tag == "polygon":
            points = [tuple(map(float, p.split(","))) for p in attr.pop("points").split()]
            def covers(x, y):
                inside = False
                for i, (ax, ay) in enumerate(points):
                    bx, by = points[i - 1]
                    if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
                        inside = not inside
                return inside
        elif tag == "circle":
            cx, cy, radius = (float(attr.pop(k)) for k in ("cx", "cy", "r"))
            def covers(x, y):
                return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2
        elif tag == "rect":
            left, top, width, height, radius = (float(attr.pop(k)) for k in ("x", "y", "width", "height", "rx"))
            def covers(x, y):
                dx = max(left + radius - x, 0, x - (left + width - radius))
                dy = max(top + radius - y, 0, y - (top + height - radius))
                return left <= x < left + width and top <= y < top + height and dx * dx + dy * dy <= radius ** 2
        else:
            raise ValueError("Unsupported SVG element: " + tag)
        assert not attr, attr
        for row in range(64):
            for col in range(64):
                if covers(col + 0.5, row + 0.5):
                    pixels[row * 64 + col] = color
    assert set(pixels) == {0, 1, 2}
    assert pixels[49 * 64 + 32] == 1 and pixels[18 * 64 + 32] == 2
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("header", type=Path)
    args = parser.parse_args()
    values = rasterize(args.source)
    header = "// Generated from font-preview-warning.svg; GPL-3.0-or-later.\n#pragma once\n"
    header += "constexpr unsigned char warning_icon[4096] = {\n"
    header += "\n".join(",".join(str(v) for v in values[y:y + 64]) + "," for y in range(0, 4096, 64))
    header += "\n};\n"
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header, encoding="utf-8")


if __name__ == "__main__":
    main()
