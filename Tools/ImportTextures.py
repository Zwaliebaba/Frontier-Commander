#!/usr/bin/env python3
"""Convert a Species BMP into the uncompressed DDS the client reads.

Textures are DDS and nothing else (owner, 2026-09-17), so a BMP has to become one before Core's
TextureFile will look at it. The header and the pixels are written here rather than by a library,
because the format wanted is the simplest DDS there is - B8G8R8A8_UNORM, no mip chain, no
compression - and a dependency for ninety bytes of header would be a dependency to carry for ever.

WHAT IT READS. A 24-bit BMP, or an 8-bit one through its palette. Both ship in Species: the
landscape ramps and the icons are 24-bit, the fonts and the sprite sheets are 8-bit. Bottom-up rows
(a positive height, which is every Species BMP) are flipped, because a DDS is top-down.

THE COLOUR KEY. A sprite with a transparent background carries it as a colour rather than as an
alpha channel, since a BMP has no alpha. --key <rrggbb> says which colour becomes alpha zero;
without it every pixel is opaque. The key is matched exactly, not by distance: these are paletted
or hand-drawn images, not photographs, so the background is one value.

WHICH KEY IS THE RIGHT ONE IS PER IMAGE, and the answer is not the one SpeciesLineage.md 4's
opening line gives. Magenta (255, 0, 255) is Species's key for its SPRITES; the font is white
glyphs on black, so its key is 000000, and the order icons are glyphs on an opaque dark-blue field
that is part of the icon, so they take no key at all. Measured before converting, not assumed.

    python3 Tools/ImportTextures.py Species/Textures/SpeccyFontNormal.bmp GameData/Textures/SpectrumFont.dds --key 000000
    python3 Tools/ImportTextures.py --atlas 4 GameData/Textures/Icons.dds a.bmp b.bmp ...
"""

import argparse
import struct
import sys

#: DDS_HEADER flags: caps, height, width, pitch, pixel format.
DDSD_REQUIRED = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000
DDPF_ALPHAPIXELS = 0x1
DDPF_RGB = 0x40
DDSCAPS_TEXTURE = 0x1000


class Fault(Exception):
    """Something the file says that cannot be turned into a texture."""


def read_bmp(path):
    """(width, height, rows) with rows top-down, each row a list of (b, g, r) triples."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 54 or data[0:2] != b"BM":
        raise Fault("%s is not a BMP" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if compression != 0:
        raise Fault("%s is compressed (%d); only uncompressed BMPs are read" % (path, compression))
    if bits not in (8, 24, 32):
        raise Fault("%s is %d bits a pixel; 8, 24 and 32 are read" % (path, bits))
    bottom_up = height > 0
    height = abs(height)

    palette = []
    if bits == 8:
        # The palette sits between the info header and the pixels, four bytes an entry (b, g, r, x).
        start = 14 + header_size
        for entry in range(start, offset, 4):
            if entry + 3 <= len(data):
                palette.append((data[entry], data[entry + 1], data[entry + 2]))

    stride = ((width * bits + 31) // 32) * 4
    rows = []
    for y in range(height):
        source = offset + (height - 1 - y if bottom_up else y) * stride
        row = []
        for x in range(width):
            if bits == 8:
                index = data[source + x]
                row.append(palette[index] if index < len(palette) else (0, 0, 0))
            else:
                at = source + x * (bits // 8)
                row.append((data[at], data[at + 1], data[at + 2]))
        rows.append(row)
    return width, height, rows


def dds_bytes(width, height, pixels):
    """A DDS of B8G8R8A8_UNORM with no mip chain. `pixels` is rows of (b, g, r, a)."""
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<I", header, 4, 124)  # dwSize of DDS_HEADER
    struct.pack_into("<I", header, 8, DDSD_REQUIRED)
    struct.pack_into("<I", header, 12, height)
    struct.pack_into("<I", header, 16, width)
    struct.pack_into("<I", header, 20, width * 4)  # dwPitchOrLinearSize
    struct.pack_into("<I", header, 28, 1)  # dwMipMapCount
    # DDS_PIXELFORMAT at offset 76: size, flags, fourCC, bit count, and the four masks. The masks
    # are what say B8G8R8A8 rather than R8G8B8A8, and getting them the wrong way round swaps red
    # and blue in every texture in the game.
    struct.pack_into("<I", header, 76, 32)
    struct.pack_into("<I", header, 80, DDPF_RGB | DDPF_ALPHAPIXELS)
    struct.pack_into("<I", header, 88, 32)
    struct.pack_into("<I", header, 92, 0x00FF0000)  # red
    struct.pack_into("<I", header, 96, 0x0000FF00)  # green
    struct.pack_into("<I", header, 100, 0x000000FF)  # blue
    struct.pack_into("<I", header, 104, 0xFF000000)  # alpha
    struct.pack_into("<I", header, 108, DDSCAPS_TEXTURE)
    body = bytearray()
    for row in pixels:
        for blue, green, red, alpha in row:
            body += bytes((blue, green, red, alpha))
    return bytes(header) + bytes(body)


def keyed(rows, key):
    """Rows of (b, g, r) as rows of (b, g, r, a), with the key colour at alpha zero."""
    out = []
    for row in rows:
        out.append([(b, g, r, 0 if key is not None and (r, g, b) == key else 255) for (b, g, r) in row])
    return out


def atlas(sources, columns, key):
    """Several BMPs packed into a grid, each cell the size of the largest source."""
    read = [read_bmp(path) for path in sources]
    cell_width = max(entry[0] for entry in read)
    cell_height = max(entry[1] for entry in read)
    rows_of_cells = (len(read) + columns - 1) // columns
    width = cell_width * columns
    height = cell_height * rows_of_cells
    pixels = [[(0, 0, 0, 0)] * width for _ in range(height)]
    for number, (source_width, source_height, source_rows) in enumerate(read):
        originX = (number % columns) * cell_width
        originY = (number // columns) * cell_height
        for y in range(source_height):
            for x in range(source_width):
                blue, green, red = source_rows[y][x]
                alpha = 0 if key is not None and (red, green, blue) == key else 255
                pixels[originY + y][originX + x] = (blue, green, red, alpha)
    return width, height, pixels, cell_width, cell_height


def main(argv):
    parser = argparse.ArgumentParser(description="Convert a Species BMP into an uncompressed DDS.")
    parser.add_argument("output", help="the DDS to write")
    parser.add_argument("inputs", nargs="+", help="the BMP or BMPs to read")
    parser.add_argument("--key", help="a colour as rrggbb that becomes alpha zero")
    parser.add_argument("--atlas", type=int, default=0, help="pack the inputs into a grid this many cells wide")
    arguments = parser.parse_args(argv[1:])

    key = None
    if arguments.key:
        try:
            value = int(arguments.key, 16)
        except ValueError:
            print("ImportTextures: --key wants six hexadecimal digits, as rrggbb", file=sys.stderr)
            return 2
        key = ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)

    try:
        if arguments.atlas > 0:
            width, height, pixels, cell_width, cell_height = atlas(arguments.inputs, arguments.atlas, key)
            note = ", %d cells of %dx%d" % (len(arguments.inputs), cell_width, cell_height)
        else:
            if len(arguments.inputs) != 1:
                print("ImportTextures: several inputs need --atlas", file=sys.stderr)
                return 2
            width, height, rows = read_bmp(arguments.inputs[0])
            pixels = keyed(rows, key)
            note = ""
        with open(arguments.output, "wb") as handle:
            handle.write(dds_bytes(width, height, pixels))
    except (Fault, OSError, IndexError) as fault:
        print("ImportTextures: %s" % fault, file=sys.stderr)
        return 2
    print("ImportTextures: %s, %dx%d B8G8R8A8_UNORM, no mip chain%s" % (arguments.output, width, height, note))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
