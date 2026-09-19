#!/usr/bin/env python3
"""Read a Species .shp shape and write it as the model JSON Content/ModelDesc.h loads.

It exists for REFERENCE IMPORTS and imports nothing into GameData in this task
(m1-vertical-slice/C4): the owner is authoring new models, and what Species's shapes are worth here
is as a way to look at how a shape of this game's kind was put together, and as the fallback if a
placeholder needs to become something recognisable before the owner's own models exist. The
provenance ADR says what may and may not be taken.

THE FORMAT IS TEXT, not the binary its extension suggests. A file is a sequence of Fragment blocks
and Marker blocks:

    Fragment: <name>          a piece, with a parent, an up, a front and a position
        Positions: n              n lines of "index: x y z"
        Normals: n                ignored: a Frontier model takes one normal per triangle
        Colours: n                n lines of "index: r g b", 0 to 255
        Vertices: n               n lines of "index: positionId colourId"
        Triangles: n              n lines of "a,b,c" over VERTEX ids
      or
        Strips: n                 each "Strip: i", a Material, a Verts count, then vNN tokens

    Marker: <name>            a point with an orientation, ended by MarkerEnd

BOTH ENCODINGS ARE READ because both ship: Armour.shp is strips and AiTarget.shp is triangles, and
a reader that handled one would refuse half the set. A strip is unrolled into triangles with the
winding alternating, and a degenerate triangle - which a strip uses to stitch two runs together -
is dropped rather than emitted, because a zero-area triangle has no normal and the pixel shader
takes its normal from the derivatives.

    python3 Tools/ImportShp.py Armour.shp out.json --id ReferenceArmour
"""

import argparse
import json
import math
import os
import re
import sys

SUBUNITS_PER_WORLD_UNIT = 256
MODEL_DESC_VERSION = 1
BINARY_ANGLE_TURN = 65536


class Fault(Exception):
    """Something the file says that cannot be turned into a model."""


def binary_angle(radians):
    return int(round(radians / (2.0 * math.pi) * BINARY_ANGLE_TURN)) % BINARY_ANGLE_TURN


def numbers(text):
    return [float(token) for token in re.findall(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?", text)]


def parse(path, scale):
    """The file's fragments and markers, each as plain lists."""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()

    fragments = []
    markers = []
    index = 0
    while index < len(lines):
        line = lines[index].strip()
        index += 1
        if line.startswith("Marker:"):
            marker = {"name": line.split(":", 1)[1].strip(), "pos": [0.0, 0.0, 0.0], "front": [0.0, 0.0, 1.0]}
            while index < len(lines) and not lines[index].strip().startswith("MarkerEnd"):
                body = lines[index].strip()
                index += 1
                if body.lower().startswith("pos:"):
                    marker["pos"] = numbers(body)[:3]
                elif body.lower().startswith("front:"):
                    marker["front"] = numbers(body)[:3]
            index += 1
            markers.append(marker)
            continue
        if not line.startswith("Fragment:"):
            continue

        fragment = {"name": line.split(":", 1)[1].strip(), "pos": [0.0, 0.0, 0.0]}
        positions = []
        colours = []
        vertices = []  # (positionId, colourId)
        triangles = []  # over VERTEX ids
        while index < len(lines):
            body = lines[index].strip()
            if body.startswith("Fragment:") or body.startswith("Marker:"):
                break
            index += 1
            if not body:
                continue
            if body.lower().startswith("pos:"):
                fragment["pos"] = numbers(body)[:3]
            elif body.startswith("Positions:"):
                count = int(numbers(body)[0])
                for _ in range(count):
                    values = numbers(lines[index])
                    index += 1
                    positions.append([int(round(value * scale)) for value in values[1:4]])
            elif body.startswith("Normals:"):
                # A Frontier model takes one normal per triangle from the geometry, so the file's
                # own normals are read past rather than kept.
                for _ in range(int(numbers(body)[0])):
                    index += 1
            elif body.startswith("Colours:") or body.startswith("Colors:"):
                for _ in range(int(numbers(body)[0])):
                    values = numbers(lines[index])
                    index += 1
                    channels = [max(0, min(255, int(value))) for value in values[1:4]]
                    colours.append(channels + [255])
            elif body.startswith("Vertices:"):
                for _ in range(int(numbers(body)[0])):
                    values = numbers(lines[index])
                    index += 1
                    vertices.append((int(values[1]), int(values[2])))
            elif body.startswith("Triangles:"):
                for _ in range(int(numbers(body)[0])):
                    values = [int(token) for token in re.findall(r"\d+", lines[index])]
                    index += 1
                    triangles.append(tuple(values[:3]))
            elif body.startswith("Strips:"):
                for _ in range(int(numbers(body)[0])):
                    # "Strip: i", then Material, then "Verts: n", then n "vNN" tokens over as many
                    # lines as the writer felt like using.
                    while index < len(lines) and not lines[index].strip().startswith("Verts:"):
                        index += 1
                    if index >= len(lines):
                        raise Fault("%s: a strip has no Verts count" % path)
                    wanted = int(numbers(lines[index])[0])
                    index += 1
                    strip = []
                    while len(strip) < wanted and index < len(lines):
                        strip.extend(int(token[1:]) for token in re.findall(r"v\d+", lines[index]))
                        index += 1
                    for corner in range(len(strip) - 2):
                        a, b, c = strip[corner], strip[corner + 1], strip[corner + 2]
                        # A degenerate stitches two runs of a strip together and is not a face.
                        if a == b or b == c or a == c:
                            continue
                        triangles.append((a, b, c) if corner % 2 == 0 else (a, c, b))
        fragment["positions"] = positions
        fragment["colours"] = colours
        fragment["vertices"] = vertices
        fragment["triangles"] = triangles
        fragments.append(fragment)
    return fragments, markers


def convert(path, identifier, scale):
    fragments, markers = parse(path, scale)
    vertices = []
    triangles = []
    out_fragments = []
    for number, fragment in enumerate(fragments):
        if not fragment["triangles"]:
            continue
        # A fragment's positions are relative to its own origin, which is what pos says.
        origin = [int(round(value * scale)) for value in fragment["pos"]] + [0, 0, 0]
        base = len(vertices)
        for position in fragment["positions"]:
            vertices.append([position[axis] + origin[axis] for axis in range(3)])
        # The first fragment is the body: fragment 0 means "not a piece that comes away".
        tag = 0 if number == 0 else len(out_fragments) + 1
        if number != 0:
            out_fragments.append({"name": fragment["name"], "center": origin[:3]})
        for corner in fragment["triangles"]:
            try:
                indices = [fragment["vertices"][vertex][0] for vertex in corner]
                colour_id = fragment["vertices"][corner[0]][1]
            except IndexError:
                continue
            colour = fragment["colours"][colour_id] if colour_id < len(fragment["colours"]) else [204, 204, 204, 255]
            triangles.append({"a": base + indices[0], "b": base + indices[1], "c": base + indices[2], "color": colour, "fragment": tag})

    # A shape may be markers and nothing else - BattleCannonBase.shp is - and that is a real file
    # rather than a broken one: it is a set of mount points for other shapes to hang on. It comes
    # across as a model with no geometry, and the caller is told so rather than being given an
    # error that reads like a parse failure.
    used = sorted({index for triangle in triangles for index in (triangle["a"], triangle["b"], triangle["c"])})
    renumbered = {old: new for new, old in enumerate(used)}
    for triangle in triangles:
        for corner in ("a", "b", "c"):
            triangle[corner] = renumbered[triangle[corner]]

    out_markers = []
    for marker in markers:
        position = [int(round(value * scale)) for value in marker["pos"]] + [0, 0, 0]
        front = marker["front"] + [0.0, 0.0, 0.0]
        out_markers.append(
            {
                "name": marker["name"],
                "position": position[:3],
                "headingBinaryAngle": binary_angle(math.atan2(front[0], front[2])),
                "pitchBinaryAngle": binary_angle(math.atan2(front[1], math.hypot(front[0], front[2]))),
            }
        )
    return {
        "version": MODEL_DESC_VERSION,
        "id": identifier,
        "vertices": [vertices[index] for index in used],
        "triangles": triangles,
        "markers": out_markers,
        "fragments": out_fragments,
    }


def main(argv):
    parser = argparse.ArgumentParser(description="Read a Species .shp and write a Frontier model JSON.")
    parser.add_argument("shp", help="the .shp to read")
    parser.add_argument("json", help="the model JSON to write")
    parser.add_argument("--id", help="the model id; the file's stem by default")
    parser.add_argument("--scale", type=float, default=float(SUBUNITS_PER_WORLD_UNIT), help="subunits per Species unit")
    arguments = parser.parse_args(argv[1:])
    identifier = arguments.id or os.path.splitext(os.path.basename(arguments.shp))[0]
    try:
        model = convert(arguments.shp, identifier, arguments.scale)
    except (Fault, OSError, ValueError, IndexError) as fault:
        print("ImportShp: %s" % fault, file=sys.stderr)
        return 2
    with open(arguments.json, "w", encoding="utf-8") as handle:
        json.dump(model, handle, indent=1)
        handle.write("\n")
    note = " (markers only: this shape carries no geometry)" if not model["triangles"] else ""
    print(
        "ImportShp: %s -> %s, %d vertices, %d triangles, %d markers, %d fragments%s"
        % (
            arguments.shp,
            arguments.json,
            len(model["vertices"]),
            len(model["triangles"]),
            len(model["markers"]),
            len(model["fragments"]),
            note,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
