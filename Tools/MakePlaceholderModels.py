#!/usr/bin/env python3
"""Generate the M1 model set as primitives, so that the tables have models to name.

    python3 Tools/MakePlaceholderModels.py [--out GameData/Models]

The owner authors the real models in OBJ and Tools/ImportObj.py converts them
(m1-vertical-slice/C4); until those exist, every row of GameData/Components.json and
GameData/Structures.json still has to name a model that is on disk, because ContentValidator
refuses a row naming one that is not and CI runs `FrontierHost --validate GameData`. These are
boxes with the markers a real model would carry, at the sizes SpeciesLineage.md 4 measured, so
that the geometry pass of K1 composes them at the right scale and an authored model can replace
one file at a time.

Positions are subunits: 256 to the world unit, 64 world units to a cell (Core/FixedPoint.h).
A triangle colour with alpha 0 is a team-colour slot, drawn neither lit nor fogged (ADR-005).
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

SUBUNITS_PER_WORLD_UNIT = 256
WORLD_UNITS_PER_CELL = 64

TEAM_SLOT = [0, 0, 0, 0]


def box(length: int, width: int, height: int, lift: int = 0) -> tuple[list, list]:
    """A box centred on the origin in x and z, standing from `lift` to `lift + height` in y."""
    hl, hw = length // 2, width // 2
    lo, hi = lift, lift + height
    vertices = [
        [-hl, lo, -hw], [hl, lo, -hw], [hl, lo, hw], [-hl, lo, hw],
        [-hl, hi, -hw], [hl, hi, -hw], [hl, hi, hw], [-hl, hi, hw],
    ]
    faces = [
        (4, 5, 6, 7),  # top
        (3, 2, 1, 0),  # bottom
        (0, 1, 5, 4),  # -z
        (2, 3, 7, 6),  # +z
        (1, 2, 6, 5),  # +x
        (3, 0, 4, 7),  # -x
    ]
    return vertices, faces


def scale(vertices: list, factor: int = SUBUNITS_PER_WORLD_UNIT) -> list:
    return [[axis * factor for axis in vertex] for vertex in vertices]


def triangles(faces: list, colors: list[list[int]]) -> list:
    """Two triangles a face, each face in its own colour, all in fragment 0."""
    out = []
    for face, color in zip(faces, colors):
        a, b, c, d = face
        out.append({"a": a, "b": b, "c": c, "color": color, "fragment": 0})
        out.append({"a": a, "b": c, "c": d, "color": color, "fragment": 0})
    return out


def marker(name: str, position: list[int], heading: int = 0, pitch: int = 0) -> dict:
    return {
        "name": name,
        "position": [axis * SUBUNITS_PER_WORLD_UNIT for axis in position],
        "headingBinaryAngle": heading,
        "pitchBinaryAngle": pitch,
    }


def model(identifier: str, vertices: list, faces: list, colors: list, markers: list) -> dict:
    return {
        "version": 1,
        "id": identifier,
        "vertices": scale(vertices),
        "triangles": triangles(faces, colors),
        "markers": markers,
        "fragments": [],
    }


def solid(color: list[int], slot_face: int | None = None) -> list:
    """Six face colours, optionally with one face as the team-colour slot."""
    colors = [list(color) for _ in range(6)]
    if slot_face is not None:
        colors[slot_face] = list(TEAM_SLOT)
    return colors


def chassis(identifier: str, length: int, width: int, height: int, color: list[int], mounts: int) -> dict:
    vertices, faces = box(length, width, height, lift=6)
    markers = [marker("MarkerDrive0", [0, 6, 0])]
    # The mount sits on the roof, forward of centre, which is where a turret belongs.
    for index in range(mounts):
        offset = 0 if mounts == 1 else (index * 2 - 1) * (width // 4)
        markers.append(marker(f"MarkerMount{index}", [length // 6, 6 + height, offset]))
    return model(identifier, vertices, faces, solid(color, slot_face=0), markers)


def drive(identifier: str, length: int, width: int, height: int, color: list[int]) -> dict:
    vertices, faces = box(length, width, height)
    return model(identifier, vertices, faces, solid(color), [])


def weapon(identifier: str, length: int, color: list[int]) -> dict:
    vertices, faces = box(length, 10, 10)
    return model(identifier, vertices, faces, solid(color), [marker("MarkerMuzzle", [length // 2, 5, 0])])


def system_module(identifier: str, color: list[int]) -> dict:
    vertices, faces = box(14, 14, 12)
    return model(identifier, vertices, faces, solid(color), [])


def structure(identifier: str, cells_x: int, cells_y: int, height: int, color: list[int]) -> dict:
    # A cell short of the footprint, so that neighbouring structures do not touch.
    length = cells_x * WORLD_UNITS_PER_CELL - 8
    width = cells_y * WORLD_UNITS_PER_CELL - 8
    vertices, faces = box(length, width, height)
    return model(identifier, vertices, faces, solid(color, slot_face=0), [])


# Every model the M1 tables name, plus the shell, wreck and deposit the simulation spawns.
# A MODEL ID IS PREFIXED BY WHAT IT IS, and that is not decoration: ContentValidator refuses an
# id used twice across every table, models included, so a model may not be named after the row
# that names it. `LightI` the chassis and `LightI` the model are one collision, not two rows.
GREY = [110, 110, 115, 255]
STEEL = [130, 128, 120, 255]
DARK = [70, 70, 74, 255]
RUST = [120, 84, 52, 255]
BRASS = [150, 130, 70, 255]

MODELS = {
    "ChassisLightI": chassis("ChassisLightI", 40, 24, 14, GREY, mounts=1),
    "ChassisMediumI": chassis("ChassisMediumI", 49, 30, 18, GREY, mounts=1),
    "ChassisHeavyI": chassis("ChassisHeavyI", 60, 36, 22, STEEL, mounts=1),
    "DriveWheels": drive("DriveWheels", 40, 30, 12, DARK),
    "DriveHalfTrack": drive("DriveHalfTrack", 44, 32, 12, DARK),
    "DriveTracks": drive("DriveTracks", 48, 34, 14, DARK),
    "ModuleMachineGun": weapon("ModuleMachineGun", 20, DARK),
    "ModuleCannon": weapon("ModuleCannon", 30, STEEL),
    "ModuleMortar": weapon("ModuleMortar", 18, STEEL),
    "ModuleBuilder": system_module("ModuleBuilder", BRASS),
    "StructureCommandPost": structure("StructureCommandPost", 3, 3, 52, STEEL),
    "StructureExtractor": structure("StructureExtractor", 1, 1, 40, RUST),
    "StructureGenerator": structure("StructureGenerator", 2, 2, 34, STEEL),
    "StructureFactory": structure("StructureFactory", 3, 3, 44, GREY),
    "StructureResearchLab": structure("StructureResearchLab", 2, 2, 38, GREY),
    "StructureTower": structure("StructureTower", 1, 1, 46, STEEL),
    "StructureModuleFactory": structure("StructureModuleFactory", 1, 1, 16, BRASS),
    "StructureModuleLab": structure("StructureModuleLab", 1, 1, 16, BRASS),
    "StructureModuleGenerator": structure("StructureModuleGenerator", 1, 1, 16, BRASS),
    "EffectShell": weapon("EffectShell", 6, BRASS),
    "EffectWreck": structure("EffectWreck", 1, 1, 10, DARK),
    "FeatureDeposit": structure("FeatureDeposit", 1, 1, 8, RUST),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default="GameData/Models", help="where the model files are written")
    # Also positionally, because m1-vertical-slice/C4's verify line spells it that way and a tool
    # whose documented invocation fails is a tool nobody runs.
    parser.add_argument("directory", nargs="?", help="the same, positionally")
    arguments = parser.parse_args()
    directory = Path(arguments.directory or arguments.out)
    directory.mkdir(parents=True, exist_ok=True)
    for identifier, body in sorted(MODELS.items()):
        path = directory / f"{identifier}.json"
        path.write_text(json.dumps(body, indent=2) + "\n", encoding="utf-8")
        print(f"{path}: {len(body['vertices'])} vertices, {len(body['triangles'])} triangles, {len(body['markers'])} markers")
    print(f"{len(MODELS)} models written to {directory}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
