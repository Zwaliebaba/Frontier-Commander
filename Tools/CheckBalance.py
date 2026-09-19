#!/usr/bin/env python3
"""Check the component tables for a design that dominates its research tier.

GameDesign.md 8 asks for rock-paper-scissors and gives a damage matrix to get it. A matrix can
still hide a design that simply wins: one that beats every design of equal or lower cost, in both
directions, at every tier it is available. That is a table bug, and this finds it before the design
screen exists and before a player does.

WHAT IT DOES. It enumerates every buildable design at each research tier - a chassis, a drive and
one module set - derives its statistics by the same formulas as Content/DesignStats, and for each
ordered pair computes the expected time to kill. A design DOMINATES when, among the designs of its
tier costing no more than it does, there is none it does not beat. The exit code is non-zero when
any tier has one.

THE ARITHMETIC IS Content/DesignStats.cpp's AND Content/DamageTable.h's, deliberately reimplemented
here rather than bound to the C++ through some bridge: this script has to run on Linux on the
standard library (the task's own acceptance), and a second implementation that disagrees with the
first is a finding in itself. Tests/ContentTests/DesignStatsTests.cpp re-derives GameDesign.md 6's
two worked examples from the shipped tables, so the two implementations are pinned to the same
numbers from both ends.

WHAT IT DOES NOT MODEL, and what that costs. There is no movement, no terrain, no micro, no repair
and no reload state at contact; two sides simply shoot at short range until one is gone. Two blind
spots follow from that, and both are declared rather than left to produce findings that are really
this script's own limits:

  MOVEMENT. A drive's whole purpose - speed, the slope it climbs, whether it swims - is invisible
  here, so a pair of designs differing ONLY in drive cannot be compared and is excluded from the
  dominance rule. Without that exclusion the script reports that half-tracks dominate wheels and
  tracks dominate half-tracks, which is not a table bug but a restatement of "this script cannot
  see the 130 speed factor wheels are bought for".

  INDIRECT FIRE. GameDesign.md 8: an indirect weapon decides its hit "at impact against whatever is
  in the splash radius at the predicted landing cell, so a moving target can walk out from under a
  mortar", and artillery is "poor against anything that walks out from under it". The table states
  the hit chance against the LANDING CELL, not against a device that has moved, and carries no
  shell flight time to work the difference out from. So a design whose only weapons are indirect is
  listed and left out of the duel: ranking it on its stated 100% would make every mortar design a
  dominator, which says only that this script cannot model the mechanic that balances it.

Both exclusions narrow what the script claims, never what it checks: a design excluded for one is
still compared on every other axis.

    python3 Tools/CheckBalance.py GameData
    python3 Tools/CheckBalance.py GameData --verbose
"""

import argparse
import itertools
import json
import os
import sys

# The enumerations of Content/ComponentDesc.h, in their declared order, because the damage matrix's
# columns are that order and a row read by name has to land in the right column.
WEAPON_CLASSES = ["AntiLight", "AntiTank", "Flame", "Artillery", "Energy"]
DRIVE_CLASSES = ["Wheels", "HalfTrack", "Tracks", "Hover", "Legs", "Lift"]
CHASSIS_CLASSES = ["Light", "Medium", "Heavy"]
STRENGTH_CLASSES = ["Soft", "Medium", "Hard", "Bunker"]
TARGET_CLASSES = DRIVE_CLASSES + STRENGTH_CLASSES

TICKS_PER_SECOND = 20

# A pursuit that never ends is not a loss to the table: it is a design with no weapon, or one whose
# damage the target's armour eats whole. Capped so the score stays finite and comparable.
MAX_SECONDS_TO_KILL = 3600.0


class Fault(Exception):
    """A fault in the tables or in how this script was called."""


def read_json(directory, name):
    path = os.path.join(directory, name)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as error:
        raise Fault("cannot read %s: %s" % (path, error))
    except ValueError as error:
        raise Fault("%s is not valid JSON: %s" % (path, error))


def rows_of(document, path, *keys):
    """The first list under any of `keys`. The tables spell their array differently by file."""
    for key in keys:
        value = document.get(key)
        if isinstance(value, list):
            return value
    raise Fault("%s has none of %s as a list" % (path, ", ".join(keys)))


def apply_percent(value, factor_hundredths):
    """value * factor / 100, rounded half away from zero: DesignStats.cpp's ApplyPercent."""
    product = value * factor_hundredths
    rounded = product + 50 if product >= 0 else product - 50
    return int(rounded / 100) if rounded >= 0 else -int(-rounded / 100)


def build_time_ticks_for(cost_hundredths):
    """DesignStats.cpp's BuildTimeTicksFor: ten power a second, rounded half up, never zero."""
    ticks = (cost_hundredths * TICKS_PER_SECOND + 500) // 1000
    return max(1, ticks)


class Tables:
    """The four files this script reads, indexed the way it asks questions of them."""

    def __init__(self, directory):
        components = read_json(directory, "Components.json")
        self.chassis = {row["id"]: row for row in rows_of(components, "Components.json", "chassis")}
        self.drives = {row["id"]: row for row in rows_of(components, "Components.json", "drives")}
        self.modules = {row["id"]: row for row in rows_of(components, "Components.json", "modules")}

        damage = read_json(directory, "Damage.json")
        self.modifier = {}
        self.armor_factor = {}
        self.armor_kind = {}
        for row in rows_of(damage, "Damage.json", "weapons"):
            name = row["class"]
            if name not in WEAPON_CLASSES:
                raise Fault("Damage.json names weapon class '%s', which is not one of %s" % (name, WEAPON_CLASSES))
            modifiers = row["modifierPercent"]
            if len(modifiers) != len(TARGET_CLASSES):
                raise Fault(
                    "Damage.json row '%s' has %d modifiers, not the %d target classes"
                    % (name, len(modifiers), len(TARGET_CLASSES))
                )
            self.modifier[name] = modifiers
            self.armor_factor[name] = row["armorFactorPercent"]
            self.armor_kind[name] = row["armorKind"]
        missing = [name for name in WEAPON_CLASSES if name not in self.modifier]
        if missing:
            raise Fault("Damage.json is missing weapon classes: %s" % ", ".join(missing))

        research = read_json(directory, "Research.json")
        self.research = {row["id"]: row for row in rows_of(research, "Research.json", "items", "research")}

        structures = read_json(directory, "Structures.json")
        self.structures = {row["id"]: row for row in rows_of(structures, "Structures.json", "structures")}

    def tiers(self):
        """Research items grouped by depth in the prerequisite graph.

        A tier is "everything reachable with N research items completed along any chain", which is
        what GameDesign.md 2's technology level means when it pre-completes tiers. Tier 0 is what a
        match starts with: every row no research unlocks.
        """
        depth = {}

        def depth_of(item_id, seen):
            if item_id in depth:
                return depth[item_id]
            if item_id in seen:
                raise Fault("Research.json has a prerequisite cycle through '%s'" % item_id)
            row = self.research.get(item_id)
            if row is None:
                raise Fault("a prerequisite names '%s', which no research item defines" % item_id)
            seen = seen | {item_id}
            prerequisites = row.get("prerequisites") or []
            value = 1 + max([depth_of(other, seen) for other in prerequisites], default=-1)
            depth[item_id] = value
            return value

        for item_id in self.research:
            depth_of(item_id, frozenset())
        return depth

    def unlocked_at(self, tier, depth):
        """The set of research ids a commander at `tier` has completed."""
        return {item_id for item_id, value in depth.items() if value < tier}

    def available(self, rows, completed):
        """The rows of a table a commander with `completed` research may build."""
        result = []
        for row in rows.values():
            unlocked_by = row.get("unlockedBy")
            if not unlocked_by or unlocked_by in completed:
                result.append(row)
        return result


class Design:
    """A chassis, a drive and one module, with the statistics DesignStats derives."""

    def __init__(self, tables, chassis, drive, modules):
        self.chassis = chassis
        self.drive = drive
        self.modules = modules
        self.name = "%s/%s/%s" % (chassis["id"], drive["id"], "+".join(m["id"] for m in modules))

        weight_penalty = min(90, sum(m.get("weightPenaltyPercent", 0) for m in modules))
        speed_times_drive = chassis["baseSpeedSubunitsPerTick"] * drive["speedFactorHundredths"]
        self.speed_subunits_per_tick = apply_percent(speed_times_drive // 100, 100 - weight_penalty)

        hit_points_times_drive = chassis["hitPoints"] * drive["hitPointFactorHundredths"]
        self.hit_points = apply_percent(hit_points_times_drive // 100, 100)
        self.kinetic_armor = chassis["kineticArmor"]
        self.thermal_armor = chassis["thermalArmor"]
        self.cost_hundredths = (
            chassis["costHundredths"] + drive["costHundredths"] + sum(m["costHundredths"] for m in modules)
        )
        self.build_time_ticks = build_time_ticks_for(self.cost_hundredths)
        self.target_column = TARGET_CLASSES.index(drive["class"])
        self.weapons = [m for m in modules if m.get("systemKind", "None") == "None" and "weaponClass" in m]
        self.tables = tables

    def armor_against(self, weapon_class):
        kind = self.tables.armor_kind[weapon_class]
        return self.thermal_armor if kind == "Thermal" else self.kinetic_armor

    def damage_per_second_against(self, target):
        """Expected damage a second at short range: the hit chance, the matrix and the armour.

        DamageDealt of Content/DamageTable.h, times shots a salvo, over the reload in seconds,
        times the short-range hit percentage. Short range because that is where a fight is decided
        and because every weapon has one; long range would flatter the artillery that never has to
        close.
        """
        total = 0.0
        for weapon in self.weapons:
            weapon_class = weapon["weaponClass"]
            modifier = self.tables.modifier[weapon_class][target.target_column]
            scaled = weapon["damage"] * modifier // 100
            counted = target.armor_against(weapon_class) * self.tables.armor_factor[weapon_class] // 100
            dealt = max(scaled - counted, scaled // 3)
            reload_ticks = max(1, weapon.get("reloadTicks", 1))
            salvo = weapon.get("shotsPerSalvo", 1)
            hit = weapon.get("shortHitPercent", 100) / 100.0
            total += dealt * salvo * hit * TICKS_PER_SECOND / reload_ticks
        return total

    def seconds_to_kill(self, target):
        """One of this design against one of that one: the duel, reported but not compared."""
        rate = self.damage_per_second_against(target)
        if rate <= 0.0:
            return MAX_SECONDS_TO_KILL
        return min(MAX_SECONDS_TO_KILL, target.hit_points / rate)

    def seconds_to_wipe(self, target):
        """The engagement AT EQUAL POWER SPENT, which is the comparison that means something.

        A commander does not choose between one light and one heavy; they choose what to spend the
        next 500 power on. So each side fields cost-many of its design for the same budget, and the
        time for this side to wipe the other is

            (target hit points per power) x (this design's power per damage a second)

        with the budget cancelling out, which is why it does not appear. Comparing duels instead
        would report that every expensive design beats every cheap one, which is true, useless, and
        was this script's first answer: 48 findings, every one of them a heavier thing winning.
        """
        rate = self.damage_per_second_against(target)
        if rate <= 0.0:
            return MAX_SECONDS_TO_KILL
        target_hit_points_per_power = target.hit_points / max(1.0, target.cost_hundredths / 100.0)
        power_per_rate = max(1.0, self.cost_hundredths / 100.0) / rate
        return min(MAX_SECONDS_TO_KILL, target_hit_points_per_power * power_per_rate)


def beats(attacker, defender):
    """True when, for the same power spent, `attacker`'s side wipes `defender`'s side first.

    A draw is not a win, which matters: it keeps a design that merely ties from counting as a
    dominator.
    """
    return attacker.seconds_to_wipe(defender) < defender.seconds_to_wipe(attacker)


def power_normalised_score(design, opponents):
    """How fast this design clears the field for the power it costs, averaged over every opponent.

    The reciprocal of seconds_to_wipe, so a bigger number is a better design, and already
    power-normalised because seconds_to_wipe is. It ranks the best three per role; the dominance
    rule below is what has a right answer.
    """
    if not opponents:
        return 0.0
    total = 0.0
    for opponent in opponents:
        seconds = design.seconds_to_wipe(opponent)
        total += 0.0 if seconds >= MAX_SECONDS_TO_KILL else 1.0 / seconds
    return 10.0 * total / len(opponents)


def _refuses(module, chassis):
    """Whether a module will not go on this chassis class (ComponentDesc's chassisClassMask).

    No M1 module carries the field, so this is the rule waiting for the first one that does rather
    than a branch any shipped row takes.
    """
    mask = module.get("chassisClassMask")
    if not mask:
        return False
    allowed = mask if isinstance(mask, list) else [mask]
    return chassis["class"] not in allowed


def designs_at_tier(tables, completed):
    """Every chassis, drive and single weapon module the tier allows, as designs.

    One module, not every subset: a chassis in the M1 tables has one mount, so the subsets are the
    singletons. Written as a product over subsets of size one so that a chassis with two mounts
    widens it by changing the range rather than the shape.
    """
    chassis_rows = tables.available(tables.chassis, completed)
    drive_rows = tables.available(tables.drives, completed)
    module_rows = tables.available(tables.modules, completed)
    designs = []
    for chassis, drive in itertools.product(chassis_rows, drive_rows):
        mounts = chassis.get("mounts", 1)
        for count in range(1, mounts + 1):
            for modules in itertools.combinations(module_rows, count):
                if any(_refuses(module, chassis) for module in modules):
                    continue
                designs.append(Design(tables, chassis, drive, list(modules)))
    return designs


def comparable(designs):
    """The designs the duel can rank, and the two groups it cannot, each with its reason.

    A design with nothing that shoots is a builder or a scout: real, buildable, and unable to win a
    duel, so it would be "dominated" by everything and say nothing about the table. A design armed
    only with indirect weapons is the case at the top of this file.
    """
    armed = []
    unarmed = []
    indirect_only = []
    for design in designs:
        if not design.weapons:
            unarmed.append(design)
        elif all(weapon.get("fireKind") == "Indirect" for weapon in design.weapons):
            indirect_only.append(design)
        else:
            armed.append(design)
    return armed, unarmed, indirect_only


def only_the_drive_differs(left, right):
    """Whether two designs are the same but for their drive - the axis this script cannot see."""
    return left.chassis["id"] == right.chassis["id"] and [m["id"] for m in left.modules] == [m["id"] for m in right.modules]


def dominators(designs):
    """The designs that beat every design of equal or lower cost, at equal power spent.

    "Equal or lower cost" is what makes this a finding rather than a tautology: of course a heavy
    beats a light. A design that beats everything it does not outspend is one a commander never has
    a reason not to build.

    A design that differs from this one only in drive is not a rival it must BEAT - the drive is
    bought for speed, slope and water, none of which is in this model, so the comparison would be
    decided on the one axis the drive is not for. It does still count as a COUNTER: if such a
    sibling wins, the commander has a reason at that price not to build this, and that is exactly
    what dominance is meant to mean. Both halves are needed. Without the first the script reports
    that tracks dominate half-tracks dominate wheels, which is a restatement of its own blind spot;
    without the second it reports that a tracked cannon dominates every machine gun of its tier
    while a wheeled cannon of the same price beats it outright.
    """
    found = []
    for design in designs:
        affordable = [other for other in designs if other is not design and other.cost_hundredths <= design.cost_hundredths]
        rivals = [other for other in affordable if not only_the_drive_differs(design, other)]
        if not rivals:
            continue
        if any(beats(other, design) for other in affordable):
            continue
        if all(beats(design, rival) for rival in rivals):
            found.append((design, rivals))
    return found


def role_of(design):
    """The role a design is the best three of: its weapon's class, which is what it is for."""
    return design.weapons[0]["weaponClass"] if design.weapons else "None"


def main(argv):
    parser = argparse.ArgumentParser(description="Check the component tables for a dominating design.")
    parser.add_argument("directory", help="the GameData directory holding the four tables")
    parser.add_argument("--verbose", action="store_true", help="print every design's statistics per tier")
    arguments = parser.parse_args(argv[1:])

    try:
        tables = Tables(arguments.directory)
        depth = tables.tiers()
    except Fault as fault:
        print("CheckBalance: %s" % fault, file=sys.stderr)
        return 2

    tier_count = max(depth.values(), default=-1) + 2  # Tier 0 plus one past the deepest chain
    findings = 0
    for tier in range(tier_count):
        completed = tables.unlocked_at(tier, depth)
        try:
            designs = designs_at_tier(tables, completed)
        except (KeyError, TypeError) as error:
            print("CheckBalance: tier %d: a row is missing a field: %s" % (tier, error), file=sys.stderr)
            return 2
        designs, unarmed, indirect_only = comparable(designs)
        print(
            "tier %d: %d research complete, %d designs compared, %d unarmed, %d indirect-only"
            % (tier, len(completed), len(designs), len(unarmed), len(indirect_only))
        )
        for design in indirect_only:
            print("    not ranked (indirect fire, GameDesign.md 8): %s" % design.name)
        if not designs:
            continue

        if arguments.verbose:
            for design in sorted(designs, key=lambda d: d.cost_hundredths):
                print(
                    "    %-34s cost %7.2f  hp %4d  speed %4d  armour %2d/%2d"
                    % (
                        design.name,
                        design.cost_hundredths / 100.0,
                        design.hit_points,
                        design.speed_subunits_per_tick,
                        design.kinetic_armor,
                        design.thermal_armor,
                    )
                )

        by_role = {}
        for design in designs:
            by_role.setdefault(role_of(design), []).append(design)
        for role in sorted(by_role):
            ranked = sorted(by_role[role], key=lambda d: -power_normalised_score(d, designs))
            best = ", ".join(
                "%s (%.2f)" % (design.name, power_normalised_score(design, designs)) for design in ranked[:3]
            )
            print("    best %-9s %s" % (role, best))

        for design, rivals in dominators(designs):
            findings += 1
            print(
                "    DOMINATES: %s at %.2f power beats all %d designs costing no more"
                % (design.name, design.cost_hundredths / 100.0, len(rivals))
            )
            for rival in sorted(rivals, key=lambda d: d.seconds_to_wipe(design))[:3]:
                print(
                    "        vs %-32s wipes in %.1f s against %.1f s (duel %.1f s against %.1f s)"
                    % (
                        rival.name,
                        design.seconds_to_wipe(rival),
                        rival.seconds_to_wipe(design),
                        design.seconds_to_kill(rival),
                        rival.seconds_to_kill(design),
                    )
                )

    if findings:
        print("CheckBalance: %d dominating design(s); the tables need an edit" % findings)
        return 1
    print("CheckBalance: no tier has a dominating design")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
