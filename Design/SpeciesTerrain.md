# Species Terrain — the landscape and the water, as a system

**Status: REFERENCE (2026-09-17).** Read from the Species repository at commit `d1add55`: how a landscape is defined, generated, merged, flattened, sampled, coloured and drawn, and how the water around it works, with the source line for each rule. Nothing was run; every figure is either the code's or arithmetic on it, and is labelled. Paths are relative to the Species repository.

The terrain is the second half of the Species look and the one piece of Darwinia's game code *Frontier Commander* keeps as a design ([`SpeciesLineage.md`](SpeciesLineage.md) §3). This document is what the integer port in [`TechnicalDesign.md`](TechnicalDesign.md) §4.4 ports.

---

## 1. The model

A `Landscape` (`GameLogic/Landscape.h:76`) is:

- **a heightmap**, `SurfaceMap2D<float>`: a grid of height samples with a cell size in world units, an origin at (0, 0), and a **default value for any sample outside the grid, the `outsideHeight`**;
- **a normal map** of the same grid, used for physics and hit tests, not for drawing;
- **a renderer**, built from the heightmap after generation (§6).

The world extends from 0 to `cellSize × samples` on each axis, so a requested world size is rounded up to whole cells (`Landscape.cpp:433`). **Sea level is y = 0.** Every map's `outsideHeight` is below it (−26.13 on nine maps, −15.39 on two, −2.25 on one), and generation starts by filling the whole grid with it: **a Species map is an island rising out of a plain that lies under the sea**, and beyond the map's edge the plain continues at that depth for ever. The far sea is not drawn at all (§7); the island floats in the black background, which is the composition every Darwinia screenshot has.

**The twelve maps**, read from `GameData/Levels/Map*.txt`:

| Map | World size | Cell size | Samples per side | Outside height | Land palette | Water | Waves | Tiles | Flatten areas |
|---|---|---|---|---|---|---|---|---|---|
| Biosphere | 4,580 | 20.0 | 229 | −26.13 | Earth | Icecaps | Earth | 8 | 0 |
| Containment | 4,442 | 17.2 | 258 | −2.25 | Default | Default | ContainmentField | 12 | 0 |
| Escort | 3,972 | 12.87 | 309 | −26.13 | Default | Default | Generator | 4 | 0 |
| Garden | 2,002 | 10.66 | 188 | −26.13 | Default | Default | Default | 7 | 0 |
| Generator | 5,372 | 15.61 | 344 | −26.13 | Mine | Default | Generator | 11 | 1 |
| Launchpad | 3,677 | 15.76 | 233 | −15.39 | Launchpad | Launchpad | Launchpad | 13 | 0 |
| Mine | 3,782 | 20.0 | 189 | −26.13 | Mine | Default | ContainmentField | 13 | 4 |
| PatternBuffer | 3,900 | 16.0 | 244 | −26.13 | Icecaps | Icecaps | Icecaps | 6 | 0 |
| Receiver | 4,800 | 20.0 | 240 | −26.13 | Mine | Default | ContainmentField | 18 | 0 |
| Sandbox | 5,000 | 20.35 | 246 | −15.39 | Default | Default | Default | 2 | 0 |
| Temple | 4,360 | 14.29 | 305 | −26.13 | Desert | Icecaps | Earth | 9 | 0 |
| Yard | 4,990 | 21.17 | 236 | −26.13 | Default | Default | ContainmentField | 12 | 3 |

Every map is square, 188 to 344 samples a side, with a cell of 10.66 to 21.17 units: between 3 and 6 samples across a tank. Flatten areas are rare because buildings flatten their own footprints at load (§5).

---

## 2. The definition

Three blocks in the map file (`GameLogic/LevelFile.cpp:487`, `:533`, `:591`), and the defaults when a field is absent (`GameLogic/LevelFile.h:141` `LandscapeDef`, `Landscape.cpp:29` `LandscapeTile`):

**`Landscape_StartDefinition`** — one keyword per line:

| Field | Default | Meaning |
|---|---|---|
| `worldSizeX`, `worldSizeZ` | 2,000 | Extent in world units (rounded up to whole cells) |
| `cellSize` | 12.0 | Sample spacing in world units |
| `outsideHeight` | −10 | The height everywhere before the tiles, and beyond the edges |
| `landColourFile` | | A 64×64 palette bitmap under `Terrain/` (§6) |
| `wavesColourFile` | | A 400×10 ramp bitmap under `Terrain/` (§7) |
| `waterColourFile` | | A 128×128 caustic bitmap under `Terrain/` (§7) |

**`LandscapeTiles_StartDefinition`** — one tile per line, eleven fields in order, then an optional guide-grid string:

| Field | Type | Default | Meaning |
|---|---|---|---|
| `x`, `y`, `z` | int, float, int | | Position: `x`, `z` in world units; `y` a height shift added to the whole tile |
| `size` | int | 256 | Extent in world units of the square the tile covers |
| `fractalDimension` | float | 1.2 | Roughness: the exponent of the noise falloff (§4) |
| `heightScale` | float | 1.0 | Noise amplitude, before compensation (§4) |
| `desiredHeight` | float | 200 | The tile's highest point after merging, in world units |
| `generationMethod` | int | 1 | 0 average, 1 random pair, 2 random neighbour (§4) |
| `randomSeed` | int | 1 | Seeds the generator for this tile |
| `lowlandSmoothingFactor` | float | 1.0 | How much flatter low ground is than high ground (§4) |
| `guideGridPower` | int | 0 | log₂ of the guide grid resolution; 0 means none |
| guide grid | string | | (2^power − 1)² bytes as letter pairs `A`–`P`, each a hex nibble (`Landscape.cpp:89`) |

**`LandFlattenAreas_StartDefinition`** — `x y z size`: a square of half-width `size` centred on (`x`, `z`) set to height `y` after all tiles are merged.

The Garden, as the worked example: 2,002 units, cell 10.66, outside −26.13, seven tiles of size 564 to 884, fractal dimension 1.87 to 3.23, height scale 2.0 to 4.66, desired heights 43 to 107, method 1 throughout, smoothing 0.70 to 0.87, no guide grids, no flatten areas.

---

## 3. What "generate" means, in order

`Landscape::Init` (`Landscape.cpp:535`) → `GenerateHeightMap` (`:410`):

```
1  scale the cell size by the detail preference       ×1, ×1.5, ×2, ×2.5 for RenderLandscapeDetail 1–4
2  allocate the heightmap and normal map              (worldSize / cellSize) samples a side, filled with outsideHeight
3  for each tile, in file order:
     generate the tile's own heightmap                §4
     merge it into the landscape                      §5: rescale to desiredHeight, take the maximum
4  apply the flatten areas                            §5
5  worldSize = cellSize × samples
6  generate the physics normals                       §5
7  build the renderer                                 §6
8  regenerate the water lightmap                      §7
```

Step 1 is the one to notice: the detail preference changes the heightmap the **simulation** reads, so two clients on different settings simulate different terrain. `AGENTS.md` R16 forbids exactly this, and it is why the port must keep generation resolution independent of any preference.

---

## 4. Tile generation

`LandscapeTile::Generate` (`Landscape.cpp:270`). Each tile is generated on its own square grid and only then resampled into the landscape.

**Resolution.** The tile grid has `2^ceil(log₂(size / cellSize − 1)) + 1` samples a side — the diamond-square requirement of a power of two plus one — at a spacing of exactly 1 in "tile space"; a Garden tile of 884 units at cell 10.66 wants 83 cells and gets a 129-sample grid.

**Initial state.** Every sample is `outsideHeight`. If a guide grid is present, its `(2^power − 1)²` bytes (0–255) are written as heights at the lattice points `((i + 1) × scale, (j + 1) × scale)` with `scale = (samples − 1) / (res + 1)`, and the first `power` subdivision passes are skipped so those points are the coarsest level the fractal refines. A guide grid is a designer's sketch of the tile at a resolution of 1, 3, 7 or 15 points a side.

**The compensated amplitude.** Before any noise:

```
fracDimModifier   = 30.7   × e^(−6.5 × fractalDimension)
smoothingModifier = 15.353 × e^(−3.1 × lowlandSmoothingFactor)
compensated       = heightScale × fracDimModifier × smoothingModifier
```

These two exponentials exist so that the editor's sliders for dimension and smoothing do not also change the overall amplitude, which the noise formula below would otherwise make them do.

**The subdivision.** `speciesSeedRandom(randomSeed)` seeds the cosmetic LCG, then for each level from the coarsest step to 1, every square of the current step gets a centre point (`GenerateSquareMidpoint`) and then the midpoints of its top edge (if not on the grid's top row) and left edge (if not on the left column) (`GenerateDiamondMidpoint`). The new height is a base from its neighbours plus noise:

| Method | Square midpoint base | Diamond midpoint base |
|---|---|---|
| 0 | mean of the four corners | mean of the four edge neighbours |
| 1 | mean of one diagonal pair, chosen by a random bit | mean of the horizontal or the vertical pair, chosen by a random bit |
| 2 | one corner, chosen at random | one edge neighbour, chosen at random |

and the noise (`GenerateNoise`, `:149`):

```
length = 256 × halfStep / samples
noise  = sfrand( (10 × length) ^ fractalDimension ) × compensated × (0.1 + |base| ^ lowlandSmoothingFactor × 0.15)
```

where `sfrand(a)` is uniform in ±a. The `length` term halves each level, so the amplitude falls by `2^fractalDimension` per level: a dimension of 1.87 gives a 3.65× drop per octave; 3.23 gives 9.4× — smooth, rounded hills at the low dimension and a rough surface at the high one. The last factor makes noise near sea level (`|base|` small) an order of magnitude smaller than noise on high ground: the lowlands are smooth, the peaks are craggy, and the smoothing factor is the exponent that decides how quickly craggy begins.

**Arithmetic for one Garden tile** (size 884, dimension 1.87, height scale 4.31, smoothing 0.70, 129 samples): `compensated` = 4.31 × 30.7 e^(−12.16) × 15.353 e^(−2.17) ≈ 1.2 × 10⁻³; at the first level `length` = 256 × 64 / 129 = 127 and `(1270)^1.87` ≈ 6.4 × 10⁵, so the first-level noise is uniform in about ±78 units (times the lowland factor, 0.1 at a base of zero → ±7.8 relative to the plain); each further level divides by 3.65. The tile's raw peak is then rescaled to 82 by the merge (§5), so the raw amplitude only sets the *shape*.

**Then** every sample gets `+ posY`, the tile's height shift.

**Method 1 in every shipped map.** Method 0 is classic diamond-square; method 1's random pair choice puts the randomness into the base as well as the noise, giving sharper ridges; method 2 gives terraces. The maps use 1 everywhere.

---

## 5. Merge, flatten, normals

**Merge** (`MergeTileIntoLandscape`, `:362`). The tile grid is given real-world coordinates (origin at the tile's `x`, `z`; spacing `size / samples`), and for every landscape cell inside the tile's square:

```
heightFactor = (desiredHeight − outsideHeight) / (tileMax − outsideHeight + 0.001)
h            = (tile.GetValue(cellX, cellZ) − outsideHeight) × heightFactor + outsideHeight
landscape    = max(landscape, h)
```

So a tile is stretched so that its highest point lands exactly at `desiredHeight` above the plain, sampled bilinearly, and **tiles combine by maximum**: overlapping tiles merge as a union of islands, never as a sum, which is why a designer can drop a tall narrow tile onto a broad low one and get a mountain on a plateau.

**Flatten areas** set a square of samples to one height after merging. Buildings do the same for their own footprints when the location loads (`LandscapeFlattenArea`), which is the flatten-under-buildings mechanic `GameDesign.md` §5 keeps.

**Physics normals** (`GenerateNormals`, `:447`): for each sample, the two face normals from the west-north and east-south neighbour pairs, each normalised, averaged, normalised; a sample whose four neighbours are level gets straight up. Rendering does not use these (§6); ray hits and slope queries do.

---

## 6. Rendering the landscape

`GameLogic/LandscapeRenderer.cpp`, built once from the heightmap and then drawn from a vertex buffer or a display list (`RenderLandscapeMode`).

**The mesh** (`BuildVertArrayAndTriStrip`, `:26`): one triangle strip over the whole grid, row by row, two vertices per column, with degenerate joints; **a quad whose six surrounding samples are all at or below sea level is skipped entirely**, and **any vertex below 0.3 is pushed down to −10**, one unit under the flat water plane at −9 (§7), so the shore dips under the water with no seam. The underwater plain is never drawn.

**Normals** (`BuildNormArray`, `:95`): one per triangle, from the strip's north, north-east and east edge vectors — flat-shaded facets, matching the shapes.

**Colour** (`BuildColourArray`, `:200`; `GetLandscapeColour`, `:167`): one colour per triangle, from the triangle's centre height `h` and its normal's y component (1 = flat, 0 = vertical):

```
u = (1 − normal.y) ^ 0.4                                  0 flat … 1 vertical
v = 1 − max(h, 0) / highest + sfrand( 0.45 / (h + 2) )    0 summit … 1 sea level, plus noise
colour = palette[ clamp(u × 64), clamp(v × 64) ]
```

The palette is the 64×64 `Terrain/Landscape*.bmp`. **Frontier reads two of them where tiles of different biomes overlap** and blends by the tiles' merge weight (`OpenQuestions.md` Q18, owner 2026-09-18); Species read one per map. The lookup is per vertex on the processor when a chunk is built, so the second read and the blend are build-time work and cost nothing per frame. The bitmap loader keeps the file's bottom-up row order (`NeuronClient/Bitmap.cpp:209`), so **row 0 is the bottom row of the image as normally viewed: the bottom of the palette is the summit, the top is sea level, the left column is flat ground and the right column is a cliff.** In `LandscapeDefault.bmp` that reads, bottom to top, white, green, blue: white peaks, green slopes, blue lowlands at the shore. The noise term is large near sea level (±0.22 of the palette height at h = 0) and vanishes with altitude (±0.02 at h = 20), which is the mottling of the low ground; it is seeded per position (`speciesSeedRandom(x | (y + rand))`, note the precedence) so it is stable frame to frame.

**Lighting, main pass** (`RenderMainSlow`, `:327`): lights 0 and 1, colour-material on, material ambient-and-diffuse 1.0, specular 0, shininess 100, flat shading, fog on. Lambert × over-bright lights × palette colour, as `SpeciesLook.md` §2 describes.

**The outline overlay** (`RenderOverlaySlow`, `:390`), drawn over the main pass unless `RenderLandscapeDetail` is 4: the same mesh again with `Textures/TriangleOutline.bmp` mapped **once per cell** (u = x / cellSize, v = z / cellSize), **additive** (src-alpha, one), depth writes off, lit with material ambient-and-diffuse 1.2, **specular 0.5, shininess 40**, so the wireframe glints where a light reflects. This is the fine grid of light lines on every Darwinia hillside, and it is the one place the Species look uses specular at all.

**Level of detail**: none within a map. The whole mesh draws every frame; only the preference changes its resolution, and it does so by regenerating (§3).

---

## 7. Water

`GameLogic/Water.cpp`. Three parts: a **lightmap** computed from the landscape, a **flat plane**, and **dynamic waves** near the shore.

**The lightmap** (`GenerateLightMap`, `:109`): a 128×128 mask over a square **twice the map's size, centred on the map**; a mask cell is 1 where the landscape is above sea level and 0 elsewhere; blurred horizontally then vertically with an 11-tap kernel (0.2, 0.3, 0.4, 0.5, 0.8, 1.0, 0.8, 0.5, 0.4, 0.3, 0.2, normalised); multiplied by 855/255 and clamped, and uploaded as a greyscale texture. It is white on and around land and fades to black within five mask cells — a twelfth of the map's width, about 160 units on the Garden — of any shore. From it, a **depth map** `depth = 1 − mask` (0 at the shore, 1 in open water) and a 32×32 grid of **flat-water tiles** marking the 16-sub-cell tiles that are not entirely at depth 1.

**The flat plane** (`RenderFlatWater`, `:443`): quads at **y = −9** over the doubled square, 32 tiles a side, **drawn only for tiles the grid marks** — those within reach of the shore glow. Two textures: the map's `Water*.bmp` caustic pattern, repeated 30 times across the square and scrolled by game time ÷ 30, modulated by the lightmap on a second texture unit. Fog on, no blending, depth writes off, culling off. **Beyond the shore band no water is drawn at all**: the open sea is the clear colour, black, under fog. This is why every Species island floats in a void.

**Dynamic waves** (`BuildTriangleStrips`, `:304`; `UpdateDynamicWater`, `:517`; `RenderDynamicWater`, `:626`), when `RenderWaterDetail` is above 0: a strip mesh over the doubled square with a cell of `detail × worldSize / 100` (20 units on the Garden at high detail), keeping only vertices where the land is at most 4 units high and the depth is under 0.999 — the shore band again. Every tick the vertex heights become

```
waveX[i] = 7 × ( 1.2 sin(0.01 x + 0.65 t) + 0.9  sin(0.03 x + 1.5  t) )
waveZ[j] = 7 × ( 0.9 sin(0.02 z + 0.75 t) + 0.65 sin(0.03 z + 1.85 t) )
y        = ( waveX[i] + waveZ[j] ) × depth
```

with `t` in seconds: two sums of sines, separable in x and z, up to ±25 units in open water and damped to nothing at the shore by `depth`. Each vertex's brightness is `(sum of three neighbouring heights) × 4 × (1 − depth) + shoreNoise`, where `shoreNoise = ((1 − depth) + sfrand(1 − depth) × 0.25) × 250` is fixed at build time; the brightness indexes **row 1 of the 400-wide `Waves*.bmp` ramp**, dark at 0 and white at 399, so the band nearest the land is white foam with noise and the water darkens outward. Drawn **additively** with vertex colours, no texture, fog on. A vertex normal is computed and stored but the additive pass does not light it.

---

## 8. Queries the game makes

- **Height at a point** (`SurfaceMap2D::GetValue`, `NeuronClient/2dSurfaceMap.h:109`): bilinear between the four surrounding samples. Indices past the last column or row wrap to 0, so a query exactly on the far edge blends with the near edge — a quirk to not port.
- **Slope**: the physics normal (§5) at the nearest sample.
- **Ray against the terrain** (`Landscape::RayHit`, `:628`): clip the ray to the grid from whichever quadrant it approaches, then walk cells (`UnsafeRayHit`, `RayHitCell`) — the ground pick under the mouse.
- **Sphere against the terrain** (`SphereHit`, `:957`): the penetration depth of a sphere, for entities walking.
- **Inside the map** (`IsInLandscape`): 0 ≤ x < worldSizeX and 0 ≤ z < worldSizeZ.

---

## 9. The editor

`GameLogic/LandscapeWindow.cpp`: a window per tile with a value control for each field in §2 and a guide-grid editor; changing any field regenerates the whole landscape (`Landscape::Init`) and rebuilds the water lightmap. That is affordable because a 344-sample map generates in well under a second (a debug trace times the water lightmap in milliseconds), and it is the workflow the Species maps were made with: a handful of tiles, tuned live.

---

## 10. What this means for Frontier Commander

**What is simulation, and therefore integer** (`AGENTS.md` R16): the heightmap, `outsideHeight`, sea level at 0, the flatten rule, the slope normals, the height query and the ray walk. The generator in §4 becomes a function of `(landscape seed, tile definition)` on the simulation's PRNG, with `pow` and `exp` replaced by tables over the few fractal dimensions and smoothing factors the tile format allows — the compensated amplitude is a constant per tile, and `(10 × length)^d` takes one table row per level per dimension. Method 1 keeps its random bits; they come from the pinned stream. **A generated landscape must not depend on any preference**: §3 step 1 is the bug not to port.

**What is presentation, and stays float**: the colour formula and its noise, the outline overlay, the water lightmap, the flat plane and the waves. Each is a page of code and a few constants, and the constants are the table below.

| Constant | Value | Where |
|---|---|---|
| Sea level | y = 0 | camera and mesh rules |
| Flat water plane | y = −9; shore vertices pushed to −10 | `Water.cpp:492`, `LandscapeRenderer.cpp:52` |
| Shore band | the 11-tap blur of a 128-cell mask over 2× the map | `Water.cpp:109` |
| Water texture | 30 repeats over 2× the map, scrolling at 1/30 per second | `Water.cpp:492` |
| Wave amplitude | ±25 units × depth, four sines | `Water.cpp:519` |
| Foam ramp | 400 entries, index = brightness | `Water.cpp:64` |
| Palette axes | slope on x (`(1 − n.y)^0.4`), height on y (`1 − h / highest`), noise `0.45 / (h + 2)` | `LandscapeRenderer.cpp:167` |
| Overlay | one outline per cell, additive, diffuse 1.2, specular 0.5, shininess 40 | `LandscapeRenderer.cpp:390` |

**Two things the Species terrain does not have that a Frontier landscape needs.** Level of detail: the Species mesh is one strip drawn whole, fine at 344 samples a side and impossible at 8,193. And chunking for incremental rebuilds when a structure flattens its footprint. Both are in `TechnicalDesign.md` §6.2; neither changes the look.

**One convention worth keeping.** `outsideHeight` below sea level, an island, and no sea drawn beyond the shore band: it is the Species composition, it makes the edge of the world a coast instead of a wall, and for a landscape 65,536 units across it is also the cheapest possible horizon.
