#!/usr/bin/env python3
"""Convert the Species BMP textures to the uncompressed DDS this game reads.

`TechnicalDesign.md` section 8 asks for exactly this tool: it "converts the Species BMPs to DDS,
expanding the 8-bit ones through their palette and turning a sprite's colour key into alpha, and
writes uncompressed DDS itself -- a header and the pixels". The output is top-down
`B8G8R8A8_UNORM` with no mip chain, the DX10 header form `Core/TextureFile.cpp` validates, and the
same header this tree's `MakeTerrainPalette.py` already writes.

**The alpha channel is the whole problem, and BMP has none.** Every pixel of a Species BMP loads
opaque -- `NeuronClient/Bitmap.cpp:162` for 24-bit and `:126` for the 4- and 8-bit palettes set
`a = 255` unconditionally -- so the alpha a DDS must carry has to be derived. Species derived it
one way and one way only: `Resource::GetTexture` (`NeuronClient/Resource.cpp:99`) calls
`ConvertPinkToTransparent` (`Bitmap.cpp:695`) unless the caller passes `_masked=false`, and that
keys **exactly** magenta (255, 0, 255) to alpha 0, setting the keyed texel's colour to mid-grey so
that filtering cannot bleed magenta into the edges. Nothing else in the tree writes an alpha:
`ConvertColourToAlpha` (`:680`) exists, would have copied a channel into alpha, and **has no
callers at all**.

That leaves most of the take list with no alpha and no key, and they work in Species because they
are drawn **additively** -- `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` or `(GL_ONE, GL_ONE)` at every
effect-sprite, icon and font draw. Under an additive blend a black texel contributes nothing
because its *colour* is zero, whatever its alpha, which is why Species never needed one. So:

* **Black is the transparent end, but not through alpha, and not as a key.** It is a continuous
  falloff, not a cutout: `Glow` and `Starburst` are 128x128 greyscale blobs with 219 and 253
  distinct grey levels. Keying black to alpha 0 and leaving the rest opaque would turn a soft
  blob into a hard-edged disc.
* This tool therefore writes **alpha = luminance** for those, and **leaves RGB untouched**. That
  is strictly more information than Species carried, and it costs nothing: an additive pass can
  ignore alpha and get Species' result exactly, while an alpha-blended or alpha-tested pass now
  has something correct to read.
* **The one way to get it wrong** is to draw a luminance-alpha texture with `SRC_ALPHA, ONE`. That
  multiplies the falloff by itself and the blob comes out far tighter and darker than Species.
  An additive pass over these textures wants `ONE, ONE`, or `SRC_ALPHA` with the shader supplying
  the alpha rather than the texture.

The rules, one per texture, are in MANIFEST below, and `--analyze` checks each one against the
pixels rather than trusting it: it reports what a file actually contains and refuses a row whose
declared rule the content contradicts -- a `key` row with no magenta in it, or a `luminance` row
with no dark pixels to become transparent.

Like every importer under Tools, this never ships (`AGENTS.md` R14), is run by hand, and its
output is committed. It needs no third-party module.

  python3 Tools/ImportTextures.py --species ../Species/GameData --analyze
  python3 Tools/ImportTextures.py --species ../Species/GameData --out GameData
  python3 Tools/ImportTextures.py --species ../Species/GameData --out GameData --only Glow Particle
  python3 Tools/ImportTextures.py --selftest
"""
from __future__ import annotations

import argparse
import struct
import sys
import tempfile
from pathlib import Path

DDS_MAGIC = 0x20534444
DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PIXELFORMAT = 0x1, 0x2, 0x4, 0x1000
DDSD_PITCH = 0x8
DDPF_FOURCC = 0x4
DDSCAPS_TEXTURE = 0x1000
DXGI_FORMAT_B8G8R8A8_UNORM = 87
D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3

# The colour key, and what Species replaces a keyed texel's colour with (Bitmap.cpp:695).
COLOUR_KEY = (255, 0, 255)
KEYED_COLOUR = (128, 128, 128)

# Rec. 601 luminance, in integer hundredths-of-a-thousandth, so the result is exact and
# platform-independent. Species' own dead ConvertColourToAlpha copied the green channel; for the
# greyscale masks that is the same number, and for a coloured one -- MuzzleFlash is the only take
# with real colour in it -- luminance is what the eye reads as coverage.
def luminance(r: int, g: int, b: int) -> int:
    return (r * 299 + g * 587 + b * 114) // 1000


# ---------------------------------------------------------------------------------------------
# The manifest: what comes across, where it lands, and how its alpha is made.
#
# The take list is SpeciesLineage.md section 4; the destinations are TechnicalDesign.md section 8
# (Terrain\ for what the landscape reads, Textures\ for sprites, icons and the font). Every rule
# below was chosen by measuring the file, and --analyze re-measures it.
#
#   opaque     alpha 255 everywhere. Lookups and gradients: nothing is meant to show through.
#   key        alpha 0 where the texel is exactly magenta, colour replaced as Species does it;
#              opaque elsewhere. A cutout, for art drawn against a keyed field.
#   luminance  alpha = luminance of the source, RGB untouched. Coverage for art drawn additively
#              against black.
# ---------------------------------------------------------------------------------------------
MANIFEST: list[tuple[str, str, str, str]] = [
    # (source, destination, rule, why)
    # -- the landscape's own textures -------------------------------------------------------
    ("Terrain/WaterDefault.bmp", "Terrain/WaterDefault.dds", "opaque", "caustic lookup; no key, no black"),
    ("Terrain/WaterIcecaps.bmp", "Terrain/WaterIcecaps.dds", "opaque", "caustic lookup; no key, no black"),
    ("Terrain/WaterLaunchpad.bmp", "Terrain/WaterLaunchpad.dds", "opaque", "caustic lookup; no key, no black"),
    ("Terrain/WavesDefault.bmp", "Terrain/WavesDefault.dds", "opaque", "shoreline colour ramp"),
    ("Terrain/WavesDesert.bmp", "Terrain/WavesDesert.dds", "opaque", "shoreline colour ramp"),
    ("Terrain/WavesEarth.bmp", "Terrain/WavesEarth.dds", "opaque", "shoreline colour ramp"),
    ("Terrain/WavesIcecaps.bmp", "Terrain/WavesIcecaps.dds", "opaque", "shoreline colour ramp"),
    ("Terrain/WavesLaunchpad.bmp", "Terrain/WavesLaunchpad.dds", "opaque", "shoreline colour ramp"),
    # Landscape*.bmp are deliberately absent: a palette must have its bottom rows ramped into the
    # water colour before it is written (OpenQuestions.md Q18, ADR-006), which is
    # MakeTerrainPalette.py's job, not a straight conversion's.
    # -- effect sprites, all drawn additively ------------------------------------------------
    ("Textures/Glow.bmp", "Textures/Glow.dds", "luminance", "soft blob, 8-bit greyscale"),
    ("Textures/CloudyGlow.bmp", "Textures/CloudyGlow.dds", "luminance", "soft blob, 8-bit greyscale"),
    ("Textures/Starburst.bmp", "Textures/Starburst.dds", "luminance", "star flare, 8-bit greyscale"),
    ("Textures/MuzzleFlash.bmp", "Textures/MuzzleFlash.dds", "luminance", "the one coloured effect sprite; RGB kept"),
    ("Textures/Particle.bmp", "Textures/Particle.dds", "luminance", "the 16x16 grey square of SpeciesLook.md section 8"),
    ("Textures/Laser.bmp", "Textures/Laser.dds", "luminance", "beam, 8-bit greyscale"),
    ("Textures/LaserFence.bmp", "Textures/LaserFence.dds", "luminance", "fence panel, 8-bit greyscale"),
    ("Textures/LaserFence2.bmp", "Textures/LaserFence2.dds", "luminance", "fence panel, 8-bit greyscale"),
    ("Textures/GodRay.bmp", "Textures/GodRay.dds", "luminance", "light shaft, 24-bit greyscale"),
    ("Textures/Fuel.bmp", "Textures/Fuel.dds", "luminance", "plume, 24-bit greyscale"),
    ("Textures/RadarSignal.bmp", "Textures/RadarSignal.dds", "luminance", "sweep, 8-bit greyscale"),
    # -- the sky and the overlays ------------------------------------------------------------
    ("Textures/Clouds.bmp", "Textures/Clouds.dds", "key", "16x16 noise mask; 57% magenta is the gap between clouds"),
    ("Textures/ShapeWireframe.bmp", "Textures/ShapeWireframe.dds", "key", "diagonal-line tile; 98% magenta is the gap"),
    ("Textures/SkyWireframe.bmp", "Textures/SkyWireframe.dds", "luminance", "bordered square on black, drawn additively"),
    ("Textures/TriangleOutline.bmp", "Textures/TriangleOutline.dds", "luminance", "cell outline on black, drawn additively"),
    # -- the interface ------------------------------------------------------------------------
    ("Textures/InterfaceGrey.bmp", "Textures/InterfaceGrey.dds", "opaque", "window gradient; drawn as a solid fill"),
    ("Textures/InterfaceRed.bmp", "Textures/InterfaceRed.dds", "opaque", "window gradient, red variant"),
    ("Textures/InterfaceDivider.bmp", "Textures/InterfaceDivider.dds", "opaque", "divider gradient"),
    # -- distortion maps: RGB is a vector, not a colour, so alpha means nothing here -----------
    ("Textures/Deform1c.bmp", "Textures/Deform1c.dds", "opaque", "shockwave offsets in RGB; not coverage"),
    ("Textures/Deform36c.bmp", "Textures/Deform36c.dds", "opaque", "shockwave offsets in RGB; not coverage"),
    # -- the font (owner, 2026-09-17: the Spectrum font is the game's) -------------------------
    ("Textures/SpeccyFontNormal.bmp", "Textures/SpeccyFontNormal.dds", "luminance", "two colours; luminance is an exact cutout"),
    ("Textures/SpeccyFontFrench.bmp", "Textures/SpeccyFontFrench.dds", "luminance", "accented variant"),
    ("Textures/SpeccyFontItalian.bmp", "Textures/SpeccyFontItalian.dds", "luminance", "accented variant"),
    ("Textures/SpeccyFontRussian.bmp", "Textures/SpeccyFontRussian.dds", "luminance", "accented variant"),
    # -- the population ------------------------------------------------------------------------
    ("Sprites/Citizen.bmp", "Textures/Citizen.dds", "key", "stick figure on a magenta field"),
    ("Sprites/LaserTrooper.bmp", "Textures/LaserTrooper.dds", "key", "stick figure on a magenta field"),
    # -- order icons and cursors, drawn additively ---------------------------------------------
    ("Icons/BannerNone.bmp", "Textures/BannerNone.dds", "luminance", "order glyph, additive"),
    ("Icons/BannerGoto.bmp", "Textures/BannerGoto.dds", "luminance", "order glyph, additive"),
    ("Icons/BannerFollow.bmp", "Textures/BannerFollow.dds", "luminance", "order glyph, additive"),
    ("Icons/BannerAbsorb.bmp", "Textures/BannerAbsorb.dds", "luminance", "order glyph, additive"),
    ("Icons/BannerDeploy.bmp", "Textures/BannerDeploy.dds", "luminance", "order glyph, additive"),
    ("Icons/BannerUnload.bmp", "Textures/BannerUnload.dds", "luminance", "order glyph, additive"),
    ("Icons/IconDelete.bmp", "Textures/IconDelete.dds", "luminance", "generic glyph kept by SpeciesLineage.md section 4"),
    ("Icons/IconNoTask.bmp", "Textures/IconNoTask.dds", "luminance", "generic glyph kept by SpeciesLineage.md section 4"),
    ("Icons/Compass.bmp", "Textures/Compass.dds", "luminance", "overlay glyph, additive"),
    ("Icons/Background.bmp", "Textures/Background.dds", "luminance", "overlay highlight, additive"),
    # Icons/Icon{Rocket,Grenade,Laser,Shadow} are Darwinia's programs. SpeciesLineage.md section 4
    # takes "the style and the generic glyphs", and Interface.md section 11 item 7 wants one
    # Icons.dds laid out as 32x32 cells for this game's own orders and stances. Composing that
    # atlas is m1-vertical-slice/C4's, not a straight conversion's; this tool converts the
    # individual files a person wants to look at, and the atlas is built from the new art.
]

RULES = {"opaque", "key", "luminance"}


# ---------------------------------------------------------------------------------------------
# BMP
# ---------------------------------------------------------------------------------------------
class Bitmap:
    """A BMP as rows of (r, g, b), top row first, whichever way round the file stored them."""

    def __init__(self, width: int, height: int, bits: int, rows: list[list[tuple[int, int, int]]]):
        self.width = width
        self.height = height
        self.bits = bits
        self.rows = rows


def read_bmp(path: Path) -> Bitmap:
    """4-, 8- and 24-bit uncompressed BMP, which is every file Species ships.

    The 4- and 8-bit ones are expanded through their palette here, as TechnicalDesign.md section 8
    asks, so that everything downstream sees one pixel format.
    """
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise SystemExit(f"{path}: not a BMP (no 'BM' magic)")
    offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    if header_size >= 40:
        width, height = struct.unpack_from("<ii", data, 18)
        bits = struct.unpack_from("<H", data, 28)[0]
        compression = struct.unpack_from("<I", data, 30)[0]
        used = struct.unpack_from("<I", data, 46)[0]
        palette_at, entry = 14 + header_size, 4
    else:  # the OS/2 header, which Species does not use but which costs three lines to admit
        width, height = struct.unpack_from("<hh", data, 18)
        bits = struct.unpack_from("<H", data, 24)[0]
        compression, used = 0, 0
        palette_at, entry = 14 + header_size, 3
    if compression != 0:
        raise SystemExit(f"{path}: BI_RGB only; this file is compression {compression}")
    if bits not in (4, 8, 24):
        raise SystemExit(f"{path}: {bits}-bit; this reads 4, 8 and 24")
    top_down = height < 0
    height = abs(height)

    palette: list[tuple[int, int, int]] = []
    if bits <= 8:
        for index in range(used or (1 << bits)):
            at = palette_at + index * entry
            palette.append((data[at + 2], data[at + 1], data[at]))

    stride = ((width * bits + 31) // 32) * 4
    rows = []
    for y in range(height):
        base = offset + (y if top_down else (height - 1 - y)) * stride
        line = data[base : base + stride]
        if len(line) < (width * bits + 7) // 8:
            raise SystemExit(f"{path}: the pixel data ends inside row {y}")
        row = []
        if bits == 24:
            for x in range(width):
                row.append((line[x * 3 + 2], line[x * 3 + 1], line[x * 3]))
        elif bits == 8:
            for x in range(width):
                row.append(palette[line[x]])
        else:
            for x in range(width):
                byte = line[x >> 1]
                row.append(palette[(byte >> 4) if (x & 1) == 0 else (byte & 0xF)])
        rows.append(row)
    return Bitmap(width, height, bits, rows)


# ---------------------------------------------------------------------------------------------
# Alpha
# ---------------------------------------------------------------------------------------------
def apply_rule(bitmap: Bitmap, rule: str) -> bytes:
    """The BGRA payload, top row first, with the alpha this rule derives."""
    out = bytearray()
    for row in bitmap.rows:
        for r, g, b in row:
            if rule == "opaque":
                out += bytes((b, g, r, 255))
            elif rule == "key":
                if (r, g, b) == COLOUR_KEY:
                    kr, kg, kb = KEYED_COLOUR
                    out += bytes((kb, kg, kr, 0))
                else:
                    out += bytes((b, g, r, 255))
            else:  # luminance
                out += bytes((b, g, r, luminance(r, g, b)))
    return bytes(out)


# ---------------------------------------------------------------------------------------------
# DDS
# ---------------------------------------------------------------------------------------------
def write_dds(path: Path, width: int, height: int, payload: bytes) -> None:
    """Top-down B8G8R8A8_UNORM, one level, no mips, in the DX10 header form Core admits."""
    header = (
        struct.pack("<I", DDS_MAGIC)
        + struct.pack(
            "<7I",
            124,
            DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_PITCH,
            height,
            width,
            width * 4,
            0,
            1,
        )
        + b"\0" * 44  # reserved1[11]
        + struct.pack("<8I", 32, DDPF_FOURCC, 0x30315844, 0, 0, 0, 0, 0)  # "DX10"
        + struct.pack("<5I", DDSCAPS_TEXTURE, 0, 0, 0, 0)
        + struct.pack("<5I", DXGI_FORMAT_B8G8R8A8_UNORM, D3D10_RESOURCE_DIMENSION_TEXTURE2D, 0, 1, 0)
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + payload)


def read_dds(path: Path) -> tuple[int, int, bytes]:
    """Enough of a reader to check our own output; it admits only what write_dds emits."""
    data = path.read_bytes()
    if struct.unpack_from("<I", data, 0)[0] != DDS_MAGIC:
        raise SystemExit(f"{path}: the magic is not 'DDS '")
    size, _flags, height, width, _pitch, _depth, mips = struct.unpack_from("<7I", data, 4)
    if size != 124:
        raise SystemExit(f"{path}: header size {size}, not 124")
    pf_size, pf_flags, four_cc = struct.unpack_from("<3I", data, 4 + 28 + 44)
    if pf_size != 32 or pf_flags != DDPF_FOURCC or four_cc != 0x30315844:
        raise SystemExit(f"{path}: not the DX10 pixel format this tool writes")
    dxgi, dimension, _misc, array_size, _misc2 = struct.unpack_from("<5I", data, 4 + 124)
    if dxgi != DXGI_FORMAT_B8G8R8A8_UNORM or dimension != D3D10_RESOURCE_DIMENSION_TEXTURE2D or array_size != 1:
        raise SystemExit(f"{path}: DXGI {dxgi}, dimension {dimension}, array {array_size}")
    if mips not in (0, 1):
        raise SystemExit(f"{path}: {mips} mip levels, and this tool writes one")
    return width, height, data[4 + 124 + 20 :]


# ---------------------------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------------------------
class Measurement:
    """What a source actually holds, so a rule can be checked instead of trusted."""

    def __init__(self, bitmap: Bitmap):
        total = bitmap.width * bitmap.height
        black = key = grey = 0
        lowest, highest = 255, 0
        distinct = set()
        for row in bitmap.rows:
            for pixel in row:
                distinct.add(pixel)
                r, g, b = pixel
                if pixel == COLOUR_KEY:
                    key += 1
                    continue  # the key is not art; it must not set the luminance range
                if r == g == b:
                    grey += 1
                    if r == 0:
                        black += 1
                level = luminance(r, g, b)
                lowest = min(lowest, level)
                highest = max(highest, level)
        self.total = total
        self.black_percent = 100.0 * black / total
        self.key_percent = 100.0 * key / total
        self.grey_percent = 100.0 * grey / total
        self.distinct = len(distinct)
        # An all-key image has no art to measure; report a flat range rather than 255..0.
        self.lowest = lowest if highest >= lowest else 0
        self.highest = highest if highest >= lowest else 0

    @property
    def spread(self) -> int:
        return self.highest - self.lowest


# A luminance alpha earns its place by varying. Below this the channel is a constant and the row
# should say `opaque`, which is the same picture and says what it means.
FLAT_SPREAD = 16


def check(rule: str, measured: Measurement) -> str | None:
    """The complaint about this rule against these pixels, or None when they agree.

    The test for a luminance row is deliberately NOT "does it contain black". Art drawn additively
    sits on whatever dark field it was authored against -- the order banners are glyphs on a dark
    blue one and hold no pure black at all -- and what matters is that the derived alpha varies
    across the image. A row is only wrong when the channel would come out flat.
    """
    if rule == "key" and measured.key_percent == 0.0:
        return "declared 'key' but holds no magenta; every texel would come out opaque"
    if rule == "luminance" and measured.spread < FLAT_SPREAD:
        return (
            f"declared 'luminance' but its luminance runs {measured.lowest}..{measured.highest}; "
            f"the alpha would be flat, so 'opaque' says the same thing more plainly"
        )
    if rule == "opaque" and measured.key_percent > 0.0:
        return f"declared 'opaque' but {measured.key_percent:.1f}% of it is the colour key"
    return None


def selftest() -> int:
    """Every rule, and a BMP-shaped round trip, without touching a Species tree."""
    rows = [[(255, 0, 255), (0, 0, 0)], [(255, 255, 255), (10, 200, 30)]]
    bitmap = Bitmap(2, 2, 24, rows)

    opaque = apply_rule(bitmap, "opaque")
    assert opaque[3::4] == bytes((255, 255, 255, 255)), "opaque must leave every alpha at 255"

    keyed = apply_rule(bitmap, "key")
    assert keyed[3::4] == bytes((0, 255, 255, 255)), "key must clear alpha on magenta alone"
    assert keyed[0:3] == bytes(KEYED_COLOUR), "a keyed texel takes the neutral colour"

    lit = apply_rule(bitmap, "luminance")
    assert lit[3] == luminance(255, 0, 255), "luminance alpha comes from the source colour"
    assert lit[7] == 0, "black becomes alpha 0"
    assert lit[11] == 255, "white becomes alpha 255"
    assert lit[8:11] == bytes((255, 255, 255)), "luminance leaves RGB alone"

    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory) / "selftest.dds"
        write_dds(out, 2, 2, lit)
        width, height, payload = read_dds(out)
        assert (width, height) == (2, 2), "the extent survives the round trip"
        assert payload == lit, "the payload survives the round trip"

    print("selftest: the three rules and the DDS round trip pass")
    return 0


# ---------------------------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--species", type=Path, help="a Species GameData directory to read from")
    parser.add_argument("--out", type=Path, help="the GameData directory to write into")
    parser.add_argument("--analyze", action="store_true", help="measure and check the rules; write nothing")
    parser.add_argument("--only", nargs="+", metavar="NAME", help="limit to sources whose stem matches")
    parser.add_argument("--selftest", action="store_true", help="check the rules and the DDS round trip")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if not args.species:
        parser.error("--species is required unless --selftest")
    if not args.analyze and not args.out:
        parser.error("--out is required unless --analyze")
    for _source, _destination, rule, _why in MANIFEST:
        assert rule in RULES, f"unknown rule {rule}"

    wanted = {name.lower() for name in args.only} if args.only else None
    complaints = 0
    written = 0
    missing = 0

    if args.analyze:
        print(f"{'source':<34} {'rule':<10} {'size':>11} {'uniq':>6} {'lum':>9} {'black':>7} {'key':>7}")

    for source, destination, rule, why in MANIFEST:
        path = args.species / source
        if wanted and Path(source).stem.lower() not in wanted:
            continue
        if not path.is_file():
            print(f"missing: {path}", file=sys.stderr)
            missing += 1
            continue
        bitmap = read_bmp(path)
        measured = Measurement(bitmap)
        complaint = check(rule, measured)

        if args.analyze:
            print(
                f"{source:<34} {rule:<10} {bitmap.width:>4}x{bitmap.height:<4}{bitmap.bits:>2}b "
                f"{measured.distinct:>6} {measured.lowest:>3}..{measured.highest:<4} "
                f"{measured.black_percent:>6.1f}% {measured.key_percent:>6.1f}%   {why}"
            )
        if complaint:
            print(f"  ! {source}: {complaint}", file=sys.stderr)
            complaints += 1
            continue
        if not args.analyze:
            write_dds(args.out / destination, bitmap.width, bitmap.height, apply_rule(bitmap, rule))
            written += 1

    if args.analyze:
        print(f"\n{len(MANIFEST)} rows, {complaints} disagreeing with their pixels, {missing} missing")
    else:
        print(f"{written} written, {complaints} refused, {missing} missing")
    return 1 if (complaints or missing) else 0


if __name__ == "__main__":
    raise SystemExit(main())
