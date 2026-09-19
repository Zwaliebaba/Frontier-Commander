#!/usr/bin/env python3
"""Select the WAVs a Species Sounds.txt references, and copy them where the game will look.

It exists for m2-skirmish/T9, which authors the sound events, and it COPIES NOTHING NOW
(m1-vertical-slice/C4): M1's GameData/Sounds.json is empty on purpose, and a file under Sounds that
no event names would be a file nobody can account for. Run without --copy it lists what an event
set would need, which is the shape T9 starts from.

WHAT Sounds.txt SAYS. Blocks of

    ENTITY <name>
        EVENT <name>
            SOUNDNAME   <wav stem, or a list of stems>
            SOURCETYPE  <n>
            ...

A SOUNDNAME IS OFTEN A GROUP RATHER THAN A FILE. `0lasers` is not Sounds/0lasers.wav; it is the
name of a set, and Species resolves it to Laser1.wav through Laser11.wav at load. So a name is
matched as a file first and, failing that, as a case-insensitive prefix of one or more files, which
is reported as a GUESS and not as a match: 163 names resolve to 54 files exactly and the rest are
groups. Everything but SOUNDNAME is Species's own mixer settings and is read past, because
Frontier's sound event row (Content/SoundEventDesc.h) carries its own.

THE PROVENANCE. Which of these may be taken is the provenance ADR's to say, not this tool's; the
soundtrack, the branding and the narration are excluded regardless (SpeciesLineage.md 1). The tool
names what it would copy so that a person decides, and refuses to copy anything the exclusion list
matches.

    python3 Tools/ImportSounds.py /path/to/Species/GameData
    python3 Tools/ImportSounds.py /path/to/Species/GameData --copy GameData/Sounds
"""

import argparse
import os
import re
import shutil
import sys

#: Never copied, whatever an event asks for (SpeciesLineage.md 1): the soundtrack, the branding and
#: the narration are outside the accepted provenance risk and stay outside it.
EXCLUDED = ("music", "theme", "intro", "narrat", "speech", "logo", "darwinia", "introversion")


def referenced(sounds_txt):
    """Every stem a SOUNDNAME line names, in the order the file names them."""
    names = []
    with open(sounds_txt, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            stripped = line.strip()
            if not stripped.upper().startswith("SOUNDNAME"):
                continue
            # The rest of the line is one stem or several, separated by spaces or commas.
            for token in re.split(r"[\s,]+", stripped[len("SOUNDNAME") :].strip()):
                if token and token not in names:
                    names.append(token)
    return names


def excluded(stem):
    lowered = stem.lower()
    return any(word in lowered for word in EXCLUDED)


def main(argv):
    parser = argparse.ArgumentParser(description="List or copy the WAVs a Species Sounds.txt references.")
    parser.add_argument("species", help="a Species GameData directory, holding Sounds.txt and Sounds/")
    parser.add_argument("--copy", help="copy into this directory; without it, nothing is copied")
    arguments = parser.parse_args(argv[1:])

    sounds_txt = os.path.join(arguments.species, "Sounds.txt")
    library = os.path.join(arguments.species, "Sounds")
    if not os.path.exists(sounds_txt):
        print("ImportSounds: no Sounds.txt under %s" % arguments.species, file=sys.stderr)
        return 2

    if not os.path.isdir(library):
        print("ImportSounds: no Sounds directory under %s" % arguments.species, file=sys.stderr)
        return 2
    catalogue = sorted(name for name in os.listdir(library) if name.lower().endswith(".wav"))

    stems = referenced(sounds_txt)
    found = []
    guessed = []
    missing = []
    refused = []
    for stem in stems:
        if excluded(stem):
            refused.append(stem)
            continue
        exact = None
        for extension in ("", ".wav", ".WAV"):
            path = os.path.join(library, stem + extension)
            if os.path.isfile(path):
                exact = path
                break
        if exact is not None:
            found.append(exact)
            continue
        # A group: every file whose name starts the same way, ignoring a leading digit, which
        # Species uses as a flag on the group's name rather than as part of it.
        prefix = stem.lstrip("0123456789").lower()
        matches = [name for name in catalogue if prefix and name.lower().startswith(prefix)]
        if matches:
            guessed.append((stem, matches))
        else:
            missing.append(stem)

    grouped = sum(len(matches) for _, matches in guessed)
    print(
        "ImportSounds: %d names referenced, %d matched a file, %d look like groups covering %d files, "
        "%d matched nothing, %d refused by the exclusion list"
        % (len(stems), len(found), len(guessed), grouped, len(missing), len(refused))
    )
    for stem in refused:
        print("    refused: %s" % stem)
    for stem, matches in guessed[:10]:
        print("    group %s -> %s%s" % (stem, ", ".join(matches[:4]), " ..." if len(matches) > 4 else ""))
    if not arguments.copy:
        print(
            "ImportSounds: nothing copied. --copy <directory> would copy the %d matched; the groups are "
            "m2-skirmish/T9's to resolve, because which alternative an event wants is an event's business"
            % len(found)
        )
        return 0

    os.makedirs(arguments.copy, exist_ok=True)
    for path in found:
        shutil.copy2(path, os.path.join(arguments.copy, os.path.basename(path)))
    print("ImportSounds: copied %d into %s" % (len(found), arguments.copy))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
