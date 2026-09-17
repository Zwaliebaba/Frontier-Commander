#!/usr/bin/env python3
"""The landscape generator in Python: integer diamond-square tiles, the size-class recipes, and the
guarantee measurements of GameDesign.md §3 (TechnicalDesign.md §4.4; m0-foundation/T16).

    python3 Tools/LandscapeTool.py --report Small --seeds 100        # pass or fail per guarantee per seed
    python3 Tools/LandscapeTool.py --render Small 1 out.png          # a picture of the heightfield
    python3 Tools/LandscapeTool.py --define Small 1 out.json         # the landscape definition Content adopts
    python3 Tools/LandscapeTool.py --golden Small 1 out.heights      # the int16 field for the C++ test, and its hash
    python3 Tools/LandscapeTool.py --hashes Small 1 20 out.txt       # the hash of every seed in a range
    python3 Tools/LandscapeTool.py --fractal-table                   # the C++ header of the fixed-point tables

THIS FILE IS THE SPECIFICATION OF THE GENERATOR. The C++ port in Sim (m0-foundation/T17) must
produce the same int16 heights for the same definition, sample for sample, and is tested against
the goldens this tool writes. Every operation below is integer: shifts floor (Python >> on a
negative number is arithmetic, as C++20 defines it), divisions are on non-negative operands,
products that could exceed 32 bits are noted where the C++ port must widen to int64.

THE ALGORITHM, from SpeciesTerrain.md §4 and §5 with the two float exponentials replaced by the
tables of Tools/FractalTable.py:

  A landscape of a size class has cells_per_side cells and samples_per_side = cells * 4 + 1
  samples at a spacing of 16 world units, every sample OUTSIDE_HEIGHT to begin with (in 24.8
  fixed point: whole units * 256). The recipe of the size class turns the match seed into a
  list of tiles; a tile is generated on its own grid of extent + 1 samples a side (extent a
  power of two) and merged into the landscape by maximum.

  Tile generation, on the tile's generator seeded by derive_seed(match seed, tile index):
    every grid sample starts at OUTSIDE_HEIGHT; amplitude = tile.amplitude * 256; step = extent
    while step >= 2:
      half = step / 2
      for every square (x, y) of the current step, rows then columns:
        the square midpoint (x+half, y+half) = base + noise(base), base from the four corners
        if y > 0: the top edge midpoint (x+half, y) = base + noise(base), base from the four
          samples at distance half (the two corners left and right, the centres above and below)
        if x > 0: the left edge midpoint (x, y+half), likewise (corners above and below, centres
          left and right)
      amplitude = (amplitude * FALLOFF_16_16[fd]) >> 16
      step = half
    then every sample += tile.height_shift * 256
    then, if tile.edge_falloff > 0, every sample within edge_falloff samples of the grid's border
      is pulled toward OUTSIDE_HEIGHT in proportion to its distance d from the nearest border:
      raw = OUTSIDE + (max(raw, OUTSIDE) - OUTSIDE) * d / edge_falloff (a 64-bit product)
  The grid's border samples are never generated and stay at OUTSIDE_HEIGHT, which is what makes a
  tile an island. The shift lifts a tile's plain; the falloff, applied after it, brings the tile
  back down to the sea at its border, so a lifted island still has a coast. The edge falloff is
  this port's addition to Species, because method 1 drops to the border in a random staircase
  and a hill tile placed on land otherwise ends in a straight cliff along its edge. Method 0 takes the mean of the four (sum >> 2); method 1 draws one bit and takes
  the mean of one diagonal (square) or of the horizontal or vertical pair (diamond), (a + b) >> 1;
  method 2 draws below(4) and takes that one sample. The draw for the base comes first, then the
  draw for the noise.
  noise(base): lowland = 6554 + ((9830 * LOWLAND_POWER_16_16[s][min(|base| >> 8, 255)]) >> 16), the
  Species 0.1 + 0.15 * |base|^s in 16.16; n = below(2 * amplitude + 1) - amplitude; result
  (n * lowland) >> 16. (9830 * table entry fits in 64 bits; the C++ port widens.)

  Merge: raw = max(sample, OUTSIDE); if the tile's maximum is above OUTSIDE,
  h = OUTSIDE + (raw - OUTSIDE) * (desired * 256 - OUTSIDE) / (max - OUTSIDE) with a 64-bit
  product and a non-negative numerator, else h = raw; landscape[y][x] = max(landscape, h) for the
  tile samples that fall inside the landscape.

  The int16 height of a sample is fixed >> 8. Sea level is 0: a sample below 0 is under water.

CELLS AND MEASUREMENTS (also the spec for Sim's derived grids and the tool's guarantees):
  cell (cx, cy) holds samples 4cx..4cx+4 by 4cy..4cy+4. water = the lowest of its 25 samples is
  below 0. slope percent = the largest |difference| between two horizontally or vertically
  adjacent samples in the cell * 100 / 16. A drive passes a land cell whose slope is at most its
  limit (Wheels 25, HalfTrack 35, Tracks 40, Hover 20); Hover also passes water. A 3x3 footprint
  is buildable when its nine cells are land and (highest sample - lowest sample) * 100 / 192 < 25.

Runs on the standard library alone; --render writes its own PNG.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from collections import deque
from dataclasses import dataclass, asdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from FractalTable import FALLOFF_16_16, FD_MIN, FD_MAX, LOWLAND_EXPONENTS, LOWLAND_POWER_16_16, HEIGHT_ENTRIES  # noqa: E402
from Xoshiro import Xoshiro128StarStar, derive_seed  # noqa: E402

SAMPLE_SPACING = 16
SAMPLES_PER_CELL = 4
OUTSIDE_HEIGHT = -26
FIXED_ONE = 256
LOWLAND_MIN_16 = 6554  # 0.1
LOWLAND_GAIN_16 = 9830  # 0.15

SIZE_CLASSES = {"Small": 128, "Medium": 256, "Large": 512, "Frontier": 1024}
COMMANDERS = {"Small": 2, "Medium": 4, "Large": 8, "Frontier": 8}
DEPOSITS = {"Small": 12, "Medium": 24, "Large": 60, "Frontier": 160}
DRIVES = {"Wheels": (25, False), "HalfTrack": (35, False), "Tracks": (40, False), "Hover": (20, True)}
FOOTPRINT_SLOPE_LIMIT = 25
START_SEARCH_RADIUS = 24
START_MIN_WINDOWS = 100
START_EDGE_MARGIN = 10
DEPOSIT_SPACING = 5
LAND_PASSABLE_FRACTION = 0.70


@dataclass
class Tile:
    x: int
    y: int
    extent: int
    fractal_dimension: int  # hundredths
    amplitude: int  # whole units, the first level's noise
    desired_height: int  # whole units
    height_shift: int  # whole units
    lowland: int  # hundredths, one of LOWLAND_EXPONENTS
    method: int
    edge_falloff: int = 0  # samples from the border over which the tile is pulled to the plain


# ---------------------------------------------------------------------------------------------
# Recipes: how a seed becomes a tile list. Tuned against --report; the numbers are the output of
# that tuning and the notes of m0-foundation/T16 record the pass rates they reached.
# ---------------------------------------------------------------------------------------------


def recipe(size_class: str, seed: int) -> list[Tile]:
    """The tile list of a size class for a seed. Tuned against --report on 2026-09-17: variant H of the
    tuning notes in tasks/m0-foundation.yaml (island 80-100 shifted 32 with a 64-sample edge falloff,
    hills by classic diamond-square with a 32-sample falloff, ridges by the Species random-pair method)
    passed 100 of 100 Small seeds where the first draft passed 1 of 6."""
    cells = SIZE_CLASSES[size_class]
    samples = cells * SAMPLES_PER_CELL  # the extent of a tile that covers the map
    rng = Xoshiro128StarStar(derive_seed(seed, 1_000_003))
    tiles: list[Tile] = []

    def pick(*values: int) -> int:
        return values[rng.below(len(values))]

    def jitter(spread: int) -> int:
        return rng.below(2 * spread + 1) - spread

    # The island: one tile covering the whole map, its border at the plain, gentle and broad, lifted
    # so that most of it stands above the sea; the falloff makes the coast a coast and not a cliff.
    tiles.append(Tile(0, 0, samples, pick(150, 160, 170), 90, pick(80, 90, 100), 48, 70, 1, samples // 16))

    # Hills: a grid of tiles a quarter of the map across, one per region, jittered, higher than the
    # island; classic diamond-square (method 0) so that their flanks are slopes rather than staircases.
    hill = samples // 2
    regions = {"Small": 2, "Medium": 4, "Large": 8, "Frontier": 16}[size_class]
    pitch = samples // regions
    for ry in range(regions):
        for rx in range(regions):
            cx = rx * pitch + pitch // 2 - hill // 2 + jitter(pitch // 4)
            cy = ry * pitch + pitch // 2 - hill // 2 + jitter(pitch // 4)
            if rng.below(8) == 0:
                continue  # a missing hill now and then, so regions differ
            tiles.append(Tile(cx, cy, hill, pick(190, 210, 230), 90, pick(60, 75, 90, 105), 0, 80, 0, hill // 16))

    # Ridges: small, rough and tall, scattered, by the Species random-pair method for their crags.
    ridge = samples // 4
    for _ in range(regions * regions // 2 + 1):
        rx = rng.below(samples - ridge // 2) - ridge // 4
        ry = rng.below(samples - ridge // 2) - ridge // 4
        tiles.append(Tile(rx, ry, ridge, pick(250, 270, 290), 90, pick(110, 130, 150), 0, 87, 1, ridge // 8))
    return tiles


# ---------------------------------------------------------------------------------------------
# Generation
# ---------------------------------------------------------------------------------------------


def generate_tile(tile: Tile, tile_seed: int) -> list[int]:
    """The tile's grid, (extent + 1)^2 samples in 24.8 fixed point, row-major."""
    if tile.extent & (tile.extent - 1) or tile.extent < 2:
        raise ValueError("a tile's extent is a power of two of at least 2")
    if not FD_MIN <= tile.fractal_dimension <= FD_MAX:
        raise ValueError(f"fractal dimension {tile.fractal_dimension} outside {FD_MIN}..{FD_MAX}")
    if tile.lowland not in LOWLAND_EXPONENTS:
        raise ValueError(f"lowland exponent {tile.lowland} not in {LOWLAND_EXPONENTS}")
    n = tile.extent + 1
    outside = OUTSIDE_HEIGHT * FIXED_ONE
    grid = [outside] * (n * n)
    rng = Xoshiro128StarStar(tile_seed)
    falloff = FALLOFF_16_16[tile.fractal_dimension - FD_MIN]
    power = LOWLAND_POWER_16_16[tile.lowland]
    top_entry = HEIGHT_ENTRIES - 1
    amplitude = tile.amplitude * FIXED_ONE
    method = tile.method
    below = rng.below
    next_u32 = rng.next_u32

    def noise(base: int) -> int:
        whole = abs(base) >> 8
        if whole > top_entry:
            whole = top_entry
        lowland = LOWLAND_MIN_16 + ((LOWLAND_GAIN_16 * power[whole]) >> 16)
        return ((below(2 * amplitude + 1) - amplitude) * lowland) >> 16

    def base_of(a: int, b: int, c: int, d: int) -> int:
        # a, b: the first pair (diagonal or horizontal); c, d: the second (other diagonal or vertical)
        if method == 0:
            return (a + b + c + d) >> 2
        if method == 1:
            return (a + b) >> 1 if (next_u32() & 1) == 0 else (c + d) >> 1
        pick = below(4)
        return (a, b, c, d)[pick]

    step = tile.extent
    while step >= 2:
        half = step >> 1
        for y in range(0, tile.extent, step):
            row = y * n
            row_mid = (y + half) * n
            row_end = (y + step) * n
            for x in range(0, tile.extent, step):
                # the square midpoint from the corners: first pair one diagonal, second pair the other
                base = base_of(grid[row + x], grid[row_end + x + step], grid[row + x + step], grid[row_end + x])
                grid[row_mid + x + half] = base + noise(base)
                if y > 0:
                    # top edge midpoint: horizontal pair (the corners), vertical pair (centre above, centre below)
                    base = base_of(grid[row + x], grid[row + x + step], grid[(y - half) * n + x + half], grid[row_mid + x + half])
                    grid[row + x + half] = base + noise(base)
                if x > 0:
                    # left edge midpoint: horizontal pair (centre left, centre right), vertical pair (the corners)
                    base = base_of(grid[row_mid + x - half], grid[row_mid + x + half], grid[row + x], grid[row_end + x])
                    grid[row_mid + x] = base + noise(base)
        amplitude = (amplitude * falloff) >> 16
        step = half
    if tile.height_shift:
        shift = tile.height_shift * FIXED_ONE
        grid = [h + shift for h in grid]
    if tile.edge_falloff > 0:
        margin = tile.edge_falloff
        extent = tile.extent
        for j in range(n):
            dj = j if j <= extent - j else extent - j
            row = j * n
            for i in range(n):
                di = i if i <= extent - i else extent - i
                d = di if di < dj else dj
                if d < margin:
                    raw = grid[row + i]
                    if raw < outside:
                        raw = outside
                    grid[row + i] = outside + ((raw - outside) * d) // margin  # 64-bit product in the C++ port
    return grid


def merge_tile(land: list[int], samples_per_side: int, tile: Tile, grid: list[int]) -> None:
    n = tile.extent + 1
    outside = OUTSIDE_HEIGHT * FIXED_ONE
    tile_max = max(grid)
    desired = tile.desired_height * FIXED_ONE
    span = tile_max - outside
    scale = desired - outside
    x0 = max(0, -tile.x)
    x1 = min(n, samples_per_side - tile.x)
    y0 = max(0, -tile.y)
    y1 = min(n, samples_per_side - tile.y)
    if x0 >= x1 or y0 >= y1:
        return
    for j in range(y0, y1):
        src = j * n
        dst = (tile.y + j) * samples_per_side + tile.x
        for i in range(x0, x1):
            raw = grid[src + i]
            if raw < outside:
                raw = outside
            if span > 0:
                h = outside + ((raw - outside) * scale) // span  # 64-bit product in the C++ port
            else:
                h = raw
            if h > land[dst + i]:
                land[dst + i] = h


@dataclass
class Landscape:
    size_class: str
    seed: int
    cells_per_side: int
    samples_per_side: int
    tiles: list[Tile]
    heights: list[int]  # int16 whole units, row-major, samples_per_side^2


def generate(size_class: str, seed: int, tiles: list[Tile] | None = None) -> Landscape:
    cells = SIZE_CLASSES[size_class]
    samples = cells * SAMPLES_PER_CELL + 1
    tiles = recipe(size_class, seed) if tiles is None else tiles
    land = [OUTSIDE_HEIGHT * FIXED_ONE] * (samples * samples)
    for index, tile in enumerate(tiles):
        merge_tile(land, samples, tile, generate_tile(tile, derive_seed(seed, index)))
    heights = [h >> 8 for h in land]
    for h in heights:
        if not -32768 <= h <= 32767:
            raise ValueError("a height left the int16 range; the recipe is wrong")
    return Landscape(size_class, seed, cells, samples, tiles, heights)


def heights_bytes(landscape: Landscape) -> bytes:
    return struct.pack(f"<{len(landscape.heights)}h", *landscape.heights)


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


# ---------------------------------------------------------------------------------------------
# Cells and measurements
# ---------------------------------------------------------------------------------------------


@dataclass
class Cells:
    per_side: int
    water: list[bool]
    slope: list[int]  # percent
    low: list[int]
    high: list[int]


def analyse(landscape: Landscape) -> Cells:
    s = landscape.samples_per_side
    c = landscape.cells_per_side
    h = landscape.heights
    water = [False] * (c * c)
    slope = [0] * (c * c)
    low = [0] * (c * c)
    high = [0] * (c * c)
    for cy in range(c):
        for cx in range(c):
            lo = 32767
            hi = -32768
            steep = 0
            for j in range(SAMPLES_PER_CELL + 1):
                row = (cy * SAMPLES_PER_CELL + j) * s + cx * SAMPLES_PER_CELL
                prev = h[row]
                if prev < lo:
                    lo = prev
                if prev > hi:
                    hi = prev
                for i in range(1, SAMPLES_PER_CELL + 1):
                    cur = h[row + i]
                    d = cur - prev if cur >= prev else prev - cur
                    if d > steep:
                        steep = d
                    if cur < lo:
                        lo = cur
                    if cur > hi:
                        hi = cur
                    prev = cur
            for i in range(SAMPLES_PER_CELL + 1):
                col = cy * SAMPLES_PER_CELL * s + cx * SAMPLES_PER_CELL + i
                prev = h[col]
                for j in range(1, SAMPLES_PER_CELL + 1):
                    cur = h[col + j * s]
                    d = cur - prev if cur >= prev else prev - cur
                    if d > steep:
                        steep = d
                    prev = cur
            k = cy * c + cx
            water[k] = lo < 0
            slope[k] = steep * 100 // SAMPLE_SPACING
            low[k] = lo
            high[k] = hi
    return Cells(c, water, slope, low, high)


def passable(cells: Cells, drive: str) -> list[bool]:
    limit, crosses_water = DRIVES[drive]
    return [(crosses_water or not cells.water[k]) and cells.slope[k] <= limit for k in range(cells.per_side * cells.per_side)]


def components(cells: Cells, ok: list[bool]) -> list[int]:
    """Component id per cell over 4-neighbour adjacency; -1 where not passable."""
    c = cells.per_side
    comp = [-1] * (c * c)
    next_id = 0
    for start in range(c * c):
        if comp[start] != -1 or not ok[start]:
            continue
        comp[start] = next_id
        queue = deque([start])
        while queue:
            k = queue.popleft()
            x = k % c
            y = k // c
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < c and 0 <= ny < c:
                    m = ny * c + nx
                    if comp[m] == -1 and ok[m]:
                        comp[m] = next_id
                        queue.append(m)
        next_id += 1
    return comp


def buildable_windows(cells: Cells) -> list[bool]:
    """True at the centre cell of a buildable 3x3 footprint."""
    c = cells.per_side
    out = [False] * (c * c)
    for cy in range(1, c - 1):
        for cx in range(1, c - 1):
            lo = 32767
            hi = -32768
            ok = True
            for j in (-1, 0, 1):
                for i in (-1, 0, 1):
                    k = (cy + j) * c + (cx + i)
                    if cells.water[k]:
                        ok = False
                        break
                    if cells.low[k] < lo:
                        lo = cells.low[k]
                    if cells.high[k] > hi:
                        hi = cells.high[k]
                if not ok:
                    break
            if ok and (hi - lo) * 100 // (3 * SAMPLES_PER_CELL * SAMPLE_SPACING) < FOOTPRINT_SLOPE_LIMIT:
                out[cy * c + cx] = True
    return out


def window_density(cells: Cells, windows: list[bool], block: int = 8) -> dict[tuple[int, int], int]:
    """Buildable windows within about START_SEARCH_RADIUS of each block's centre, per block of `block` cells."""
    c = cells.per_side
    blocks = c // block
    counts = [[0] * blocks for _ in range(blocks)]
    for cy in range(c):
        for cx in range(c):
            if windows[cy * c + cx]:
                counts[cy // block][cx // block] += 1
    reach = START_SEARCH_RADIUS // block
    density: dict[tuple[int, int], int] = {}
    for by in range(blocks):
        for bx in range(blocks):
            total = 0
            for j in range(by - reach, by + reach + 1):
                for i in range(bx - reach, bx + reach + 1):
                    if 0 <= i < blocks and 0 <= j < blocks:
                        total += counts[j][i]
            density[(bx, by)] = total
    return density


def window_cell_near(cells: Cells, windows: list[bool], centre_x: int, centre_y: int, reach: int) -> tuple[int, int] | None:
    """The buildable window centre nearest a point, within reach cells."""
    c = cells.per_side
    best_cell = None
    best_d = None
    for cy in range(max(0, centre_y - reach), min(c, centre_y + reach + 1)):
        for cx in range(max(0, centre_x - reach), min(c, centre_x + reach + 1)):
            if windows[cy * c + cx]:
                d = (cx - centre_x) ** 2 + (cy - centre_y) ** 2
                if best_d is None or d < best_d:
                    best_d = d
                    best_cell = (cx, cy)
    return best_cell


def place_starts(cells: Cells, windows: list[bool], count: int, comps: dict[str, list[int]]) -> list[tuple[int, int]]:
    """Starts go on flat ground with room for a base, spread apart, and on one landmass: every start
    lies in the same component for wheels, tracks and hover, chosen as the landmass with the most
    candidate blocks. A seed whose largest landmass has too few candidates fails the starts guarantee."""
    c = cells.per_side
    block = 8
    density = window_density(cells, windows, block)
    margin = START_EDGE_MARGIN // block + 1
    blocks = c // block
    candidates: list[tuple[int, int]] = []
    landmass: dict[tuple[int, int], tuple[int, ...]] = {}
    for (bx, by), d in density.items():
        if d < START_MIN_WINDOWS or not (margin <= bx < blocks - margin and margin <= by < blocks - margin):
            continue
        cell = window_cell_near(cells, windows, bx * block + block // 2, by * block + block // 2, block)
        if cell is None:
            continue
        ids = tuple(comps[drive][cell[1] * c + cell[0]] for drive in ("Wheels", "Tracks", "Hover"))
        if -1 in ids:
            continue
        candidates.append((bx, by))
        landmass[(bx, by)] = ids
    if not candidates:
        return []
    by_landmass: dict[tuple[int, ...], list[tuple[int, int]]] = {}
    for b in candidates:
        by_landmass.setdefault(landmass[b], []).append(b)
    best_ids = max(by_landmass, key=lambda ids: (len(by_landmass[ids]), sum(density[b] for b in by_landmass[ids]), ids))
    candidates = by_landmass[best_ids]
    chosen: list[tuple[int, int]] = []
    while candidates and len(chosen) < count:
        if not chosen:
            best = max(candidates, key=lambda b: (density[b], -b[1], -b[0]))
        else:

            def spread(b: tuple[int, int]) -> tuple[int, int, int, int]:
                nearest = min((b[0] - o[0]) ** 2 + (b[1] - o[1]) ** 2 for o in chosen)
                return (nearest, density[b], -b[1], -b[0])

            best = max(candidates, key=spread)
        chosen.append(best)
        candidates.remove(best)
    starts: list[tuple[int, int]] = []
    for bx, by in chosen:
        cell = window_cell_near(cells, windows, bx * block + block // 2, by * block + block // 2, block)
        if cell is not None:
            starts.append(cell)
    return starts


def place_deposits(cells: Cells, starts: list[tuple[int, int]], wheels_comp: list[int], total: int) -> list[tuple[int, int]]:
    c = cells.per_side
    per_start = 4 if len(starts) * 4 <= total - len(starts) else max(1, (total // len(starts)) - 1)
    deposits: list[tuple[int, int]] = []

    def usable(cx: int, cy: int) -> bool:
        k = cy * c + cx
        if cells.water[k] or cells.slope[k] > DRIVES["Wheels"][0]:
            return False
        if not any(wheels_comp[k] == wheels_comp[sy * c + sx] and wheels_comp[k] != -1 for sx, sy in starts):
            return False
        return all(max(abs(cx - dx), abs(cy - dy)) >= DEPOSIT_SPACING for dx, dy in deposits)

    for sx, sy in starts:
        ring = []
        for cy in range(max(0, sy - 20), min(c, sy + 21)):
            for cx in range(max(0, sx - 20), min(c, sx + 21)):
                d = max(abs(cx - sx), abs(cy - sy))
                if 6 <= d <= 20:
                    ring.append((d, cy, cx))
        ring.sort()
        placed = 0
        for _, cy, cx in ring:
            if placed >= per_start:
                break
            if usable(cx, cy):
                deposits.append((cx, cy))
                placed += 1

    remaining = total - len(deposits)
    if remaining > 0 and len(starts) >= 2:
        # midway cells lie about as far from their two nearest starts as each other, and between them
        # rather than off to one side: the sum of the two distances is within a quarter of the
        # distance between those two starts.
        midway = []
        for cy in range(c):
            for cx in range(c):
                ranked = sorted((max(abs(cx - sx), abs(cy - sy)), i) for i, (sx, sy) in enumerate(starts))
                (d1, a), (d2, b) = ranked[0], ranked[1]
                between = max(abs(starts[a][0] - starts[b][0]), abs(starts[a][1] - starts[b][1]))
                if d1 >= 12 and (d2 - d1) * 5 <= d2 and (d1 + d2) * 2 <= between * 3:
                    midway.append((d2 - d1, -d1, cy, cx))
        midway.sort()
        for _, _, cy, cx in midway:
            if remaining <= 0:
                break
            if usable(cx, cy):
                deposits.append((cx, cy))
                remaining -= 1
    if remaining > 0 and starts:
        # the rest go on usable ground nearest the starts' centroid, so that a seed whose midway
        # ground is water or cliff still gets its full count where the fight will be
        mx = sum(sx for sx, _ in starts) // len(starts)
        my = sum(sy for _, sy in starts) // len(starts)
        near_centre = sorted((max(abs(cx - mx), abs(cy - my)), cy, cx) for cy in range(c) for cx in range(c))
        for _, cy, cx in near_centre:
            if remaining <= 0:
                break
            if usable(cx, cy) and all(max(abs(cx - sx), abs(cy - sy)) >= 12 for sx, sy in starts):
                deposits.append((cx, cy))
                remaining -= 1
    return deposits


@dataclass
class Report:
    seed: int
    starts: list[tuple[int, int]]
    deposits: list[tuple[int, int]]
    land_fraction: float
    tracks_fraction: float
    windows_near_starts: list[int]
    connected: dict[str, bool]
    passes: dict[str, bool]

    @property
    def ok(self) -> bool:
        return all(self.passes.values())


def measure(landscape: Landscape) -> tuple[Cells, Report]:
    cells = analyse(landscape)
    c = cells.per_side
    total = c * c
    land = sum(1 for w in cells.water if not w)
    windows = buildable_windows(cells)
    comps = {drive: components(cells, passable(cells, drive)) for drive in ("Wheels", "Tracks", "Hover")}
    starts = place_starts(cells, windows, COMMANDERS[landscape.size_class], comps)
    near = []
    for sx, sy in starts:
        near.append(
            sum(
                1
                for cy in range(max(0, sy - START_SEARCH_RADIUS), min(c, sy + START_SEARCH_RADIUS + 1))
                for cx in range(max(0, sx - START_SEARCH_RADIUS), min(c, sx + START_SEARCH_RADIUS + 1))
                if windows[cy * c + cx]
            )
        )
    connected = {}
    for drive, comp in comps.items():
        ids = {comp[sy * c + sx] for sx, sy in starts}
        connected[drive] = len(starts) >= 2 and len(ids) == 1 and -1 not in ids
    deposits = place_deposits(cells, starts, comps["Wheels"], DEPOSITS[landscape.size_class]) if starts else []
    tracks_ok = passable(cells, "Tracks")
    tracks_land = sum(1 for k in range(total) if tracks_ok[k] and not cells.water[k])
    passes = {
        "starts": len(starts) == COMMANDERS[landscape.size_class] and all(n >= START_MIN_WINDOWS for n in near),
        "connected": all(connected.values()),
        "deposits": len(deposits) == DEPOSITS[landscape.size_class],
        "passable": land > 0 and tracks_land / land >= LAND_PASSABLE_FRACTION,
    }
    return cells, Report(
        landscape.seed,
        starts,
        deposits,
        land / total,
        (tracks_land / land) if land else 0.0,
        near,
        connected,
        passes,
    )


# ---------------------------------------------------------------------------------------------
# Outputs
# ---------------------------------------------------------------------------------------------


def definition(landscape: Landscape, report: Report) -> dict:
    return {
        "version": 1,
        "sizeClass": landscape.size_class,
        "cellsPerSide": landscape.cells_per_side,
        "samplesPerSide": landscape.samples_per_side,
        "sampleSpacingWorldUnits": SAMPLE_SPACING,
        "outsideHeight": OUTSIDE_HEIGHT,
        "seed": landscape.seed,
        "palette": "Default",
        "tiles": [
            {
                "x": t.x,
                "y": t.y,
                "extent": t.extent,
                "fractalDimensionHundredths": t.fractal_dimension,
                "amplitude": t.amplitude,
                "desiredHeight": t.desired_height,
                "heightShift": t.height_shift,
                "lowlandExponentHundredths": t.lowland,
                "method": t.method,
                "edgeFalloff": t.edge_falloff,
            }
            for t in landscape.tiles
        ],
        "starts": [{"cellX": x, "cellY": y} for x, y in report.starts],
        "deposits": [{"cellX": x, "cellY": y} for x, y in report.deposits],
    }


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    raw = b"".join(b"\x00" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def render(landscape: Landscape, cells: Cells, report: Report, path: Path) -> None:
    s = landscape.samples_per_side
    h = landscape.heights
    highest = max(1, max(h))
    rgb = bytearray(s * s * 3)
    for y in range(s):
        for x in range(s):
            v = h[y * s + x]
            if v < 0:
                depth = min(1.0, -v / 26.0)
                r, g, b = int(20 * (1 - depth)), int(60 + 60 * (1 - depth)), int(120 + 80 * (1 - depth))
            else:
                t = v / highest
                if t < 0.5:
                    u = t / 0.5
                    r, g, b = int(60 + 100 * u), int(140 + 40 * u), int(60 - 20 * u)
                else:
                    u = (t - 0.5) / 0.5
                    r, g, b = int(160 + 90 * u), int(180 + 70 * u), int(40 + 210 * u)
                # darken by the steepness of the sample's cell
                cx = min(cells.per_side - 1, x // SAMPLES_PER_CELL)
                cy = min(cells.per_side - 1, y // SAMPLES_PER_CELL)
                shade = max(0.35, 1.0 - cells.slope[cy * cells.per_side + cx] / 80.0)
                r, g, b = int(r * shade), int(g * shade), int(b * shade)
            k = (y * s + x) * 3
            rgb[k], rgb[k + 1], rgb[k + 2] = r, g, b

    def dot(cx: int, cy: int, colour: tuple[int, int, int], radius: int) -> None:
        px, py = cx * SAMPLES_PER_CELL + 2, cy * SAMPLES_PER_CELL + 2
        for y in range(max(0, py - radius), min(s, py + radius + 1)):
            for x in range(max(0, px - radius), min(s, px + radius + 1)):
                k = (y * s + x) * 3
                rgb[k], rgb[k + 1], rgb[k + 2] = colour

    for cx, cy in report.deposits:
        dot(cx, cy, (255, 230, 0), 3)
    for cx, cy in report.starts:
        dot(cx, cy, (255, 40, 40), 5)
    write_png(path, s, s, bytes(rgb))


def fractal_table_header() -> str:
    lines = [
        "// Sim/FractalTable.h -- GENERATED by `python3 Tools/LandscapeTool.py --fractal-table`; do not edit.",
        "// The fixed-point tables of the landscape generator (TechnicalDesign.md §4.4): FALLOFF is",
        "// 2^(-fd/100) in 16.16 per fractal dimension in hundredths from FD_MIN, and LOWLAND_POWER is",
        "// x^(s/100) in 16.16 for the lowland exponents the recipes use. Tools/GenerateFractalTable.py",
        "// is the only place float touches them.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace Frontier",
        "{",
        "",
        f"inline constexpr int FRACTAL_DIMENSION_MIN = {FD_MIN};",
        f"inline constexpr int FRACTAL_DIMENSION_MAX = {FD_MAX};",
        f"inline constexpr int LOWLAND_HEIGHT_ENTRIES = {HEIGHT_ENTRIES};",
        f"inline constexpr std::int32_t LOWLAND_MIN_16 = {LOWLAND_MIN_16};",
        f"inline constexpr std::int32_t LOWLAND_GAIN_16 = {LOWLAND_GAIN_16};",
        "",
        f"inline constexpr std::array<std::int32_t, {FD_MAX - FD_MIN + 1}> FALLOFF_16_16 = {{",
    ]
    for start in range(0, len(FALLOFF_16_16), 12):
        lines.append("  " + ", ".join(str(v) for v in FALLOFF_16_16[start : start + 12]) + ",")
    lines.append("};")
    lines.append("")
    lines.append(f"inline constexpr std::array<int, {len(LOWLAND_EXPONENTS)}> LOWLAND_EXPONENTS = {{{', '.join(str(s) for s in LOWLAND_EXPONENTS)}}};")
    lines.append("")
    lines.append(f"inline constexpr std::array<std::array<std::int32_t, {HEIGHT_ENTRIES}>, {len(LOWLAND_EXPONENTS)}> LOWLAND_POWER_16_16 = {{{{")
    for s in LOWLAND_EXPONENTS:
        lines.append(f"  {{ // exponent {s}")
        table = LOWLAND_POWER_16_16[s]
        for start in range(0, HEIGHT_ENTRIES, 12):
            lines.append("    " + ", ".join(str(v) for v in table[start : start + 12]) + ",")
        lines.append("  },")
    lines.append("}};")
    lines.append("")
    lines.append("} // namespace Frontier")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------------------------


def command_report(size_class: str, seeds: int, first: int, verbose: bool) -> int:
    passed = 0
    per_guarantee = {"starts": 0, "connected": 0, "deposits": 0, "passable": 0}
    for seed in range(first, first + seeds):
        landscape = generate(size_class, seed)
        _, report = measure(landscape)
        for name, ok in report.passes.items():
            per_guarantee[name] += int(ok)
        passed += int(report.ok)
        flags = " ".join(f"{name}={'ok' if ok else 'FAIL'}" for name, ok in report.passes.items())
        print(
            f"seed {seed:4d}: {'PASS' if report.ok else 'fail'}  {flags}  land={report.land_fraction:.2f} "
            f"tracks={report.tracks_fraction:.2f} starts={report.starts} windows={report.windows_near_starts} "
            f"deposits={len(report.deposits)} connected={report.connected}" if verbose else
            f"seed {seed:4d}: {'PASS' if report.ok else 'fail'}  {flags}  land={report.land_fraction:.2f} tracks={report.tracks_fraction:.2f}"
        )
    print(f"{size_class}: {passed}/{seeds} seeds pass every guarantee; " + ", ".join(f"{k} {v}/{seeds}" for k, v in per_guarantee.items()))
    return 0 if passed == seeds else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--report", metavar="SIZE", help="measure the guarantees over --seeds seeds")
    parser.add_argument("--seeds", type=int, default=100)
    parser.add_argument("--first", type=int, default=1, help="the first seed of a --report or --hashes run")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--render", nargs=3, metavar=("SIZE", "SEED", "PNG"))
    parser.add_argument("--define", nargs=3, metavar=("SIZE", "SEED", "JSON"))
    parser.add_argument("--golden", nargs=3, metavar=("SIZE", "SEED", "FILE"))
    parser.add_argument("--hashes", nargs=4, metavar=("SIZE", "FIRST", "LAST", "FILE"))
    parser.add_argument("--fractal-table", action="store_true", help="print the C++ header of the tables")
    args = parser.parse_args()

    if args.fractal_table:
        sys.stdout.write(fractal_table_header())
        return 0
    if args.report:
        return command_report(args.report, args.seeds, args.first, args.verbose)
    if args.render:
        size, seed, out = args.render
        landscape = generate(size, int(seed))
        cells, report = measure(landscape)
        render(landscape, cells, report, Path(out))
        print(f"wrote {out}: {'PASS' if report.ok else 'fail'} {report.passes}")
        return 0
    if args.define:
        size, seed, out = args.define
        landscape = generate(size, int(seed))
        _, report = measure(landscape)
        Path(out).write_text(json.dumps(definition(landscape, report), indent=2) + "\n", encoding="utf-8")
        print(f"wrote {out}: {'PASS' if report.ok else 'fail'} {report.passes}")
        return 0 if report.ok else 1
    if args.golden:
        size, seed, out = args.golden
        landscape = generate(size, int(seed))
        data = heights_bytes(landscape)
        Path(out).write_bytes(data)
        print(f"{size} seed {seed}: {landscape.samples_per_side}x{landscape.samples_per_side} int16 LE, fnv1a64 {fnv1a64(data):016x}, wrote {out}")
        return 0
    if args.hashes:
        size, first, last, out = args.hashes
        lines = [f"# fnv1a64 of the little-endian int16 heights of {size} seeds {first}..{last}, written by Tools/LandscapeTool.py --hashes"]
        for seed in range(int(first), int(last) + 1):
            lines.append(f"{seed} {fnv1a64(heights_bytes(generate(size, seed))):016x}")
        Path(out).write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"wrote {out}")
        return 0
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
