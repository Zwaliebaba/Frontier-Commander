#!/usr/bin/env python3
"""Describe the BMPs a capture wrote, in text an agent can read off a job log (TechnicalDesign.md §10).

    python3 Tools/DescribeCapture.py Captures            # every frame-*.bmp in the directory
    python3 Tools/DescribeCapture.py frame-0.bmp ...     # the files named

CI uploads the captures as an artefact, and the session that has to judge them cannot fetch an
artefact through its proxy; the job log it can read. So for each frame this prints what a glance
would tell: the size, the mean colour, how much of the frame is near black, and two thumbnails,
one of luminance and one of hue class, plus the same statistics per horizontal band, because
the fog question (SpeciesLook.md §5, the fog-and-lighting ADR) is a question about how the far
field, which is the upper bands, differs from the near field.

Reads the 24-bit bottom-up BMP Core's BitmapWriter writes and nothing else; the standard library
alone, so that the CI step needs no package.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

LUMINANCE_RAMP = " .:-=+*#%@"
THUMBNAIL_COLUMNS = 96
THUMBNAIL_ROWS = 27
BANDS = 6


def read_bitmap(path: Path) -> tuple[int, int, bytes, int]:
    """(width, height, pixel bytes bottom-up BGR, row stride) of a 24-bit uncompressed BMP."""
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size, width, height, planes, bits, compression = struct.unpack_from("<IiiHHI", data, 14)
    if header_size < 40 or planes != 1 or bits != 24 or compression != 0:
        raise ValueError(f"{path}: only an uncompressed 24-bit BMP is read (header {header_size}, {bits} bits, compression {compression})")
    if height < 0:
        raise ValueError(f"{path}: a top-down BMP is not what the capture writes")
    stride = (width * 3 + 3) & ~3
    if len(data) < pixel_offset + stride * height:
        raise ValueError(f"{path}: truncated")
    return width, height, data[pixel_offset:], stride


def pixel(pixels: bytes, stride: int, height: int, x: int, y: int) -> tuple[int, int, int]:
    """(r, g, b) at column x, row y counted from the top."""
    offset = (height - 1 - y) * stride + x * 3
    return pixels[offset + 2], pixels[offset + 1], pixels[offset]


def hue_class(r: int, g: int, b: int) -> str:
    """One letter: k black, w white, x grey, then the dominant hue r y g c b m."""
    high, low = max(r, g, b), min(r, g, b)
    if high < 24:
        return "k"
    if low > 200:
        return "w"
    if high - low < 24:
        return "x"
    if r >= g and r >= b:
        return "y" if g > (r + b) // 2 else ("m" if b > g + 24 else "r")
    if g >= r and g >= b:
        return "c" if b > (g + r) // 2 else ("y" if r > (g + b) // 2 else "g")
    return "m" if r > (g + b) // 2 else ("c" if g > (b + r) // 2 else "b")


def block_mean(pixels: bytes, stride: int, height: int, x0: int, y0: int, x1: int, y1: int) -> tuple[float, float, float]:
    total = [0, 0, 0]
    count = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = pixel(pixels, stride, height, x, y)
            total[0] += r
            total[1] += g
            total[2] += b
            count += 1
    if count == 0:
        return 0.0, 0.0, 0.0
    return total[0] / count, total[1] / count, total[2] / count


def saturation(r: float, g: float, b: float) -> float:
    high = max(r, g, b)
    return 0.0 if high == 0 else (high - min(r, g, b)) / high


def describe(path: Path) -> None:
    width, height, pixels, stride = read_bitmap(path)
    print(f"{path.name}: {width}x{height}")
    # Sampled on a grid rather than every pixel: a 1080p frame is two million pixels, and Python is
    # slow, while a 4-pixel step loses nothing a glance would see.
    step = 4
    total = [0, 0, 0]
    count = 0
    black = 0
    classes: dict[str, int] = {}
    for y in range(0, height, step):
        for x in range(0, width, step):
            r, g, b = pixel(pixels, stride, height, x, y)
            total[0] += r
            total[1] += g
            total[2] += b
            count += 1
            if max(r, g, b) < 24:
                black += 1
            letter = hue_class(r, g, b)
            classes[letter] = classes.get(letter, 0) + 1
    mean = tuple(value / count for value in total)
    print(f"  mean colour ({mean[0]:.0f}, {mean[1]:.0f}, {mean[2]:.0f}); near-black {100 * black / count:.1f}%; "
          "hue classes " + ", ".join(f"{letter} {100 * n / count:.1f}%" for letter, n in sorted(classes.items(), key=lambda item: -item[1])))
    print("  bands, top to bottom: mean colour, saturation, near-black")
    band_height = height // BANDS
    for band in range(BANDS):
        y0, y1 = band * band_height, (band + 1) * band_height if band < BANDS - 1 else height
        r, g, b = block_mean(pixels, stride, height, 0, y0, width, y1) if width * (y1 - y0) < 200000 else band_mean_sampled(pixels, stride, height, width, y0, y1, step)
        dark = band_black_sampled(pixels, stride, height, width, y0, y1, step)
        print(f"    band {band}: ({r:.0f}, {g:.0f}, {b:.0f}), saturation {saturation(r, g, b):.2f}, near-black {100 * dark:.1f}%")
    print("  luminance:")
    print_thumbnail(pixels, stride, width, height, luminance=True)
    print("  hue class (k black, w white, x grey, r y g c b m):")
    print_thumbnail(pixels, stride, width, height, luminance=False)


def band_mean_sampled(pixels: bytes, stride: int, height: int, width: int, y0: int, y1: int, step: int) -> tuple[float, float, float]:
    total = [0, 0, 0]
    count = 0
    for y in range(y0, y1, step):
        for x in range(0, width, step):
            r, g, b = pixel(pixels, stride, height, x, y)
            total[0] += r
            total[1] += g
            total[2] += b
            count += 1
    return (total[0] / count, total[1] / count, total[2] / count) if count else (0.0, 0.0, 0.0)


def band_black_sampled(pixels: bytes, stride: int, height: int, width: int, y0: int, y1: int, step: int) -> float:
    black = 0
    count = 0
    for y in range(y0, y1, step):
        for x in range(0, width, step):
            if max(pixel(pixels, stride, height, x, y)) < 24:
                black += 1
            count += 1
    return black / count if count else 0.0


def print_thumbnail(pixels: bytes, stride: int, width: int, height: int, luminance: bool) -> None:
    cell_width = max(1, width // THUMBNAIL_COLUMNS)
    cell_height = max(1, height // THUMBNAIL_ROWS)
    columns = width // cell_width
    rows = height // cell_height
    for row in range(rows):
        line = []
        for column in range(columns):
            x0, y0 = column * cell_width, row * cell_height
            # The centre of each cell and its four neighbours at half a cell: five samples a cell.
            samples = [(x0 + cell_width // 2, y0 + cell_height // 2), (x0 + cell_width // 4, y0 + cell_height // 4),
                       (x0 + 3 * cell_width // 4, y0 + cell_height // 4), (x0 + cell_width // 4, y0 + 3 * cell_height // 4),
                       (x0 + 3 * cell_width // 4, y0 + 3 * cell_height // 4)]
            r = g = b = 0
            for x, y in samples:
                sr, sg, sb = pixel(pixels, stride, height, min(x, width - 1), min(y, height - 1))
                r += sr
                g += sg
                b += sb
            r, g, b = r // len(samples), g // len(samples), b // len(samples)
            if luminance:
                value = (299 * r + 587 * g + 114 * b) // 1000
                line.append(LUMINANCE_RAMP[min(len(LUMINANCE_RAMP) - 1, value * len(LUMINANCE_RAMP) // 256)])
            else:
                line.append(hue_class(r, g, b))
        print("    |" + "".join(line) + "|")


def main() -> int:
    arguments = sys.argv[1:]
    if not arguments:
        print(__doc__)
        return 2
    paths: list[Path] = []
    for argument in arguments:
        path = Path(argument)
        if path.is_dir():
            paths.extend(sorted(path.glob("frame-*.bmp"), key=lambda p: int(p.stem.split("-")[1]) if p.stem.split("-")[1].isdigit() else 0))
        else:
            paths.append(path)
    if not paths:
        print("DescribeCapture: no frame-*.bmp found")
        return 1
    for path in paths:
        try:
            describe(path)
        except (OSError, ValueError) as error:
            print(f"DescribeCapture: {error}")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
