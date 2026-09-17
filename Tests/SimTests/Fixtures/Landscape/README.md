# Landscape golden fields

Written by `Tools/LandscapeTool.py`, which is the specification of the generator; the C++ port in
`Sim` (`m0-foundation/T17`) is tested against these and must never be the one that changes them.
Regenerate with the commands below after a deliberate change to the algorithm or a recipe, in the
same commit as that change, and say so in the commit message.

| File | Command | What it holds |
|---|---|---|
| `small-0001.heights` | `python3 Tools/LandscapeTool.py --golden Small 1 Tests/SimTests/Fixtures/Landscape/small-0001.heights` | The Small landscape of seed 1: 513 × 513 samples, little-endian `int16` whole world units, row-major, 526,338 bytes |
| `small-hashes.txt` | `python3 Tools/LandscapeTool.py --hashes Small 1 20 Tests/SimTests/Fixtures/Landscape/small-hashes.txt` | The FNV-1a 64 hash of the same bytes for Small seeds 1 to 20 |

The test compares seed 1 sample for sample, so that a mismatch names the first differing sample,
and the other nineteen by hash. A landscape's samples depend on nothing but the size class and
the seed: the recipe of the size class turns the seed into the tile list, and each tile's
generator is seeded from the match seed and the tile's index (`Tools/Xoshiro.py`, `derive_seed`).
