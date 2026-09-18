#!/usr/bin/env python3
"""Compose a terrain palette and write it as an uncompressed DDS.

A terrain palette is the 64x64 lookup the landscape is coloured by: the slope term runs along x,
flat ground at column 0 and a cliff at column 63, and the height term down y, the summit at row 0
and sea level at row 63 (`Design/SpeciesTerrain.md` section 6; `Client/TerrainChunk.h`,
`TerrainPalette::Lookup`). Species kept eight of them as bottom-up 24-bit BMPs under
its own `GameData/Terrain`, which is the name this tree keeps them under too; this writes one as a top-down `B8G8R8A8_UNORM` DDS with no mip chain, which is
what `Core/TextureFile.h` reads and what the terrain's own lookup wants (`TechnicalDesign.md`
section 8: palettes are read exactly, so they are never block compressed).

Two palettes that colour one landscape must agree at the shore, because their colours blend where
their tiles overlap (`OpenQuestions.md` Q18). `--water` is what that agreement is against: the
bottom `--shore` rows ramp into the water plane's colour, so every palette written by this tool
meets the water the same way and any two of them blend without a seam. Species had no such rule
and did not need one, having one palette per map.

Like every importer under Tools, this never ships and is run by hand; its output is committed.

  python3 Tools/MakeTerrainPalette.py --species ../Species/GameData/Terrain --source Earth \
      --out GameData/Terrain/LandscapeDefault.dds
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SIDE = 64

# Core/TextureFile.cpp's values, which are dxgiformat.h's.
DDS_MAGIC = 0x20534444
DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PIXELFORMAT = 0x1, 0x2, 0x4, 0x1000
DDSD_PITCH = 0x8
DDPF_FOURCC = 0x4
DDSCAPS_TEXTURE = 0x1000
DXGI_FORMAT_B8G8R8A8_UNORM = 87
D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3


def read_bmp(path: Path) -> list[list[tuple[int, int, int]]]:
    """A 24-bit BMP as rows in file order, which for a bottom-up BMP is the summit first."""
    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits != 24:
        raise SystemExit(f"{path}: {bits}-bit; this reads the 24-bit palettes only")
    if (width, height) != (SIDE, SIDE):
        raise SystemExit(f"{path}: {width}x{height}; a palette is {SIDE}x{SIDE}")
    stride = ((width * 3 + 3) // 4) * 4
    rows = []
    for row in range(height):
        base = offset + row * stride
        rows.append([(data[base + x * 3 + 2], data[base + x * 3 + 1], data[base + x * 3]) for x in range(width)])
    return rows


def conform_shore(rows, water: tuple[int, int, int], band: int):
    """Ramp the bottom `band` rows into the water colour, the last row reaching it.

    Row 63 is sea level, so this is the strip the waterline runs through. Ramping rather than
    replacing keeps the palette's own colour a few rows up, which is the beach.
    """
    if band <= 0:
        return rows
    first = SIDE - band
    for row in range(first, SIDE):
        weight = (row - first + 1) / band
        for column in range(SIDE):
            r, g, b = rows[row][column]
            rows[row][column] = (
                round(r + (water[0] - r) * weight),
                round(g + (water[1] - g) * weight),
                round(b + (water[2] - b) * weight),
            )
    return rows


def write_dds(path: Path, rows) -> None:
    """Top-down B8G8R8A8, one level, no mips: row 0 is the summit, as Lookup indexes it."""
    header = struct.pack(
        "<I", DDS_MAGIC
    ) + struct.pack(
        "<7I",
        124,
        DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_PITCH,
        SIDE,
        SIDE,
        SIDE * 4,
        0,
        1,
    ) + b"\0" * 44 + struct.pack(  # reserved1[11]
        "<8I", 32, DDPF_FOURCC, 0x30315844, 0, 0, 0, 0, 0  # "DX10"
    ) + struct.pack(
        "<5I", DDSCAPS_TEXTURE, 0, 0, 0, 0
    ) + struct.pack(
        "<5I", DXGI_FORMAT_B8G8R8A8_UNORM, D3D10_RESOURCE_DIMENSION_TEXTURE2D, 0, 1, 0
    )
    pixels = bytearray()
    for row in rows:
        for r, g, b in row:
            pixels += bytes((b, g, r, 255))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bytes(pixels))


def parse_colour(text: str) -> tuple[int, int, int]:
    parts = [int(p) for p in text.split(",")]
    if len(parts) != 3 or any(p < 0 or p > 255 for p in parts):
        raise SystemExit(f"--water wants three values 0 to 255, not {text!r}")
    return parts[0], parts[1], parts[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--species", type=Path, required=True, help="the Species GameData/Terrain directory")
    parser.add_argument("--source", required=True, help="the palette to compose from: Default, Desert, Earth, Icecaps")
    parser.add_argument("--out", type=Path, required=True, help="the DDS to write")
    parser.add_argument("--water", default="20,56,97", help="the water plane's colour; Client/Shaders/WaterPS.hlsl's")
    parser.add_argument("--shore", type=int, default=6, help="rows ramped into it, of 64")
    arguments = parser.parse_args()

    source = arguments.species / f"Landscape{arguments.source}.bmp"
    if not source.is_file():
        raise SystemExit(f"{source}: no such palette")
    rows = conform_shore(read_bmp(source), parse_colour(arguments.water), arguments.shore)
    write_dds(arguments.out, rows)
    print(f"MakeTerrainPalette: {arguments.out} from {source.name}, {arguments.shore} shore rows to {arguments.water}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
