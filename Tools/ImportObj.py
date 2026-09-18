#!/usr/bin/env python3
"""Convert a Wavefront OBJ into the model JSON Content/ModelDesc.h loads.

The owner authors models in OBJ (TechnicalDesign.md 8), so this is the importer that exists before
the models do. What it carries across is what a Frontier model is: positions, one flat colour per
triangle, markers, and fragments.

COLOUR COMES FROM THE MATERIAL, and a material is a name in the OBJ and a `Kd` line in the MTL
beside it. A triangle's colour is its face's material, which is what "one colour per triangle"
means when the authoring tool thinks in materials. A material whose `d` (dissolve) is zero becomes
the TEAM-COLOUR SLOT: alpha zero, which the pixel shader draws neither lit nor fogged (ADR-005).
Zero opacity is the one thing an artist can set in any tool and that no real surface wants, so it
is the flag rather than a naming convention that a rename would break.

AN OBJECT NAMED Marker<Name> IS A MARKER, not geometry. Its origin is the marker's position - the
mean of its vertices, so a small cross or a cone works as well as a single point - and its
orientation comes from the direction its geometry points, taken as the vector from the first vertex
to the last. Markers are how a device is a tree: MarkerDrive* carry the drive, MarkerMount* the
modules, MarkerMuzzle the shot.

AN OBJECT NAMED Fragment<Name> IS A FRAGMENT: a piece that comes away when the thing is destroyed,
as the Species models carried them. Its triangles are geometry like any other and are tagged with
the fragment's index.

UNITS. An OBJ is in whatever the artist worked in; the simulation is in subunits, 256 to the world
unit (ADR-002). --scale says how many subunits one OBJ unit is, and defaults to the 256 that makes
one OBJ unit one world unit.

    python3 Tools/ImportObj.py model.obj GameData/Models/Thing.json --id Thing
    python3 Tools/ImportObj.py model.obj out.json --id Thing --scale 256
"""

import argparse
import json
import math
import os
import sys

SUBUNITS_PER_WORLD_UNIT = 256
MODEL_DESC_VERSION = 1

#: A binary angle: the full turn in 65,536 parts (Core/BinaryAngle.h), which is what a marker's
#: heading and pitch are stored in.
BINARY_ANGLE_TURN = 65536


class Fault(Exception):
    """Something the file says that cannot be turned into a model."""


def binary_angle(radians):
    """Radians as the binary angle of Core/BinaryAngle.h, wrapped into one turn."""
    turns = radians / (2.0 * math.pi)
    return int(round(turns * BINARY_ANGLE_TURN)) % BINARY_ANGLE_TURN


def read_materials(path):
    """The `Kd` and `d` of every material in an MTL, as (r, g, b, a) bytes."""
    materials = {}
    if not os.path.exists(path):
        return materials
    current = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "newmtl":
                current = parts[1]
                materials[current] = [204, 204, 204, 255]
            elif parts[0] == "Kd" and current is not None and len(parts) >= 4:
                for channel in range(3):
                    value = float(parts[1 + channel])
                    materials[current][channel] = max(0, min(255, int(round(value * 255.0))))
            elif parts[0] == "d" and current is not None and len(parts) >= 2:
                # Dissolve zero is the team-colour slot; anything else is opaque. There is no
                # partial transparency in a Frontier model, so the middle is not a case.
                materials[current][3] = 0 if float(parts[1]) == 0.0 else 255
    return materials


def parse_obj(path, scale):
    """The OBJ's vertices, its faces grouped by object, and each face's material."""
    positions = []
    objects = []  # (name, [(indices, material)])
    current_name = ""
    current_faces = []
    material = None
    material_library = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for number, line in enumerate(handle, 1):
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            keyword = parts[0]
            if keyword == "v":
                if len(parts) < 4:
                    raise Fault("%s:%d: a vertex needs three numbers" % (path, number))
                positions.append([int(round(float(parts[axis + 1]) * scale)) for axis in range(3)])
            elif keyword == "mtllib" and len(parts) >= 2:
                material_library = os.path.join(os.path.dirname(path), parts[1])
            elif keyword == "usemtl" and len(parts) >= 2:
                material = parts[1]
            elif keyword in ("o", "g"):
                if current_faces:
                    objects.append((current_name, current_faces))
                current_name = parts[1] if len(parts) >= 2 else ""
                current_faces = []
            elif keyword == "f":
                # An OBJ face index is one-based and may be negative (from the end); the vertex may
                # carry texture and normal indices after slashes, which a flat-shaded model has no
                # use for.
                indices = []
                for token in parts[1:]:
                    value = int(token.split("/")[0])
                    indices.append(value - 1 if value > 0 else len(positions) + value)
                if len(indices) < 3:
                    raise Fault("%s:%d: a face needs three corners" % (path, number))
                # A fan, so a quad or an n-gon becomes triangles without a triangulator.
                for corner in range(1, len(indices) - 1):
                    current_faces.append(((indices[0], indices[corner], indices[corner + 1]), material))
    if current_faces:
        objects.append((current_name, current_faces))
    return positions, objects, material_library


def marker_from(name, positions, indices):
    """A marker at the mean of its geometry, facing the way that geometry points."""
    used = sorted({index for face, _ in indices for index in face})
    if not used:
        raise Fault("the marker object '%s' has no geometry to take a position from" % name)
    center = [sum(positions[index][axis] for index in used) // len(used) for axis in range(3)]
    # The direction from the first vertex to the last: a cross, a cone or a two-vertex line all
    # point somewhere, and a single point points along +z, which is the model's own forward.
    first = positions[used[0]]
    last = positions[used[-1]]
    dx = float(last[0] - first[0])
    dy = float(last[1] - first[1])
    dz = float(last[2] - first[2])
    heading = 0
    pitch = 0
    if dx != 0.0 or dz != 0.0 or dy != 0.0:
        heading = binary_angle(math.atan2(dx, dz))
        pitch = binary_angle(math.atan2(dy, math.hypot(dx, dz)))
    return {"name": name, "position": center, "headingBinaryAngle": heading, "pitchBinaryAngle": pitch}


def convert(path, identifier, scale):
    positions, objects, material_library = parse_obj(path, scale)
    materials = read_materials(material_library) if material_library else {}

    markers = []
    fragments = []
    triangles = []
    for name, faces in objects:
        if name.startswith("Marker"):
            markers.append(marker_from(name, positions, faces))
            continue
        fragment_index = 0
        if name.startswith("Fragment"):
            used = sorted({index for face, _ in faces for index in face})
            center = [sum(positions[index][axis] for index in used) // len(used) for axis in range(3)]
            fragments.append({"name": name[len("Fragment") :] or name, "center": center})
            fragment_index = len(fragments)
        for face, material in faces:
            color = materials.get(material, [204, 204, 204, 255])
            triangles.append(
                {"a": face[0], "b": face[1], "c": face[2], "color": list(color), "fragment": fragment_index}
            )

    if not triangles:
        raise Fault("%s holds no geometry outside its markers" % path)
    # Only the vertices the triangles use, renumbered: an OBJ often carries the whole scene's
    # vertex list and a model that shipped the markers' vertices would draw them.
    used = sorted({index for triangle in triangles for index in (triangle["a"], triangle["b"], triangle["c"])})
    renumbered = {old: new for new, old in enumerate(used)}
    for triangle in triangles:
        for corner in ("a", "b", "c"):
            triangle[corner] = renumbered[triangle[corner]]
    return {
        "version": MODEL_DESC_VERSION,
        "id": identifier,
        "vertices": [positions[index] for index in used],
        "triangles": triangles,
        "markers": markers,
        "fragments": fragments,
    }


def main(argv):
    parser = argparse.ArgumentParser(description="Convert a Wavefront OBJ into a Frontier model JSON.")
    parser.add_argument("obj", help="the OBJ to read")
    parser.add_argument("json", help="the model JSON to write")
    parser.add_argument("--id", required=True, help="the model id, which no other row may use")
    parser.add_argument(
        "--scale",
        type=float,
        default=float(SUBUNITS_PER_WORLD_UNIT),
        help="subunits per OBJ unit; the default makes one OBJ unit one world unit",
    )
    arguments = parser.parse_args(argv[1:])
    try:
        model = convert(arguments.obj, arguments.id, arguments.scale)
    except (Fault, OSError, ValueError, IndexError) as fault:
        print("ImportObj: %s" % fault, file=sys.stderr)
        return 2
    with open(arguments.json, "w", encoding="utf-8") as handle:
        json.dump(model, handle, indent=1)
        handle.write("\n")
    print(
        "ImportObj: %s -> %s, %d vertices, %d triangles, %d markers, %d fragments"
        % (arguments.obj, arguments.json, len(model["vertices"]), len(model["triangles"]), len(model["markers"]), len(model["fragments"]))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
