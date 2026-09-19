#!/usr/bin/env python3
"""Validate and query the task plans under tasks/.

A plan is a set of tasks plus the edges between them. The graph must be acyclic,
every dependency must resolve, and status must be consistent with the edges: a
task cannot be done while something it depends on is not.

Edges come in two kinds. `depends_on` names tasks in the same plan and shapes the
waves. `blocked_by` names tasks in *another* plan, in `plan/Tn` form, for the
orderings a single plan cannot see (an M1 task that needs an M0 library). Both
kinds gate readiness and status identically; only `depends_on` affects wave
numbering, because a wave is a statement about one plan.

The point of the format is that an agent can compute what to do next instead of
being told. `--next` lists tasks whose dependencies are all satisfied; `--waves`
shows which tasks may run concurrently.

    python3 Tools/CheckTaskDag.py                          # validate every plan
    python3 Tools/CheckTaskDag.py tasks/m1-vertical-slice.yaml
    python3 Tools/CheckTaskDag.py --next tasks/m1-vertical-slice.yaml
    python3 Tools/CheckTaskDag.py --waves tasks/m1-vertical-slice.yaml
    python3 Tools/CheckTaskDag.py --mermaid tasks/m1-vertical-slice.yaml

The schema is documented in Design/ImplementationPlan.md §3. This script is a
port of the Species repository's tools/check_task_dag.py; it is a development
tool under Tools/ and never ships (AGENTS.md R14), which is why PyYAML is fine.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("PyYAML is required:  python3 -m pip install pyyaml")

REPO_ROOT = Path(__file__).resolve().parent.parent
TASKS_DIR = REPO_ROOT / "tasks"
# Plans with nothing left open move here. They are still LOADED, because open
# plans carry blocked_by edges into them, and an edge into a plan the loader
# cannot see is an unresolvable reference rather than a satisfied one. They are
# not VALIDATED or reported by default: a finished plan does not need re-listing
# on every run.
ARCHIVE_DIR = TASKS_DIR / "Archive"

REQUIRED_PLAN_KEYS = {"plan", "title", "tasks"}
REQUIRED_TASK_KEYS = {"id", "title", "intent", "acceptance", "status"}
OPTIONAL_TASK_KEYS = {
    "depends_on",
    "blocked_by",
    "project",
    "files",
    "verify",
    "notes",
    "phase",
    "parallel_safe",
}
STATUSES = {"todo", "in_progress", "blocked", "done", "abandoned"}

# A task in one of these states counts as satisfied for its dependents.
SATISFIED = {"done", "abandoned"}


class PlanError(Exception):
    pass


def display(path: Path) -> str:
    """Repo-relative where possible; plans may also be validated from elsewhere."""
    try:
        return str(path.resolve().relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


class StrictLoader(yaml.SafeLoader):
    """SafeLoader that refuses duplicate mapping keys.

    PyYAML accepts a repeated key and lets the last one win, discarding
    everything the earlier one held without a word. In the Species tree two
    completed tasks each grew a second `notes:` and every parser then read the
    later note and nothing else; the record of what the task did was in the
    file and invisible to every tool that reads the plan.
    """


def _no_duplicate_keys(loader: StrictLoader, node: yaml.MappingNode, deep: bool = False) -> dict:
    seen: set = set()
    for key_node, _ in node.value:
        key = loader.construct_object(key_node, deep=True)
        if key in seen:
            raise PlanError(
                f"line {key_node.start_mark.line + 1}: duplicate key '{key}' -- YAML keeps "
                "only the last one, so the earlier value is silently discarded"
            )
        seen.add(key)
    return yaml.SafeLoader.construct_mapping(loader, node, deep=deep)


StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _no_duplicate_keys)


def load_plan(path: Path) -> dict:
    data = yaml.load(path.read_text(encoding="utf-8"), StrictLoader)
    if not isinstance(data, dict):
        raise PlanError("plan must be a YAML mapping")

    missing = REQUIRED_PLAN_KEYS - data.keys()
    if missing:
        raise PlanError(f"plan is missing required key(s): {', '.join(sorted(missing))}")
    if not isinstance(data["tasks"], list) or not data["tasks"]:
        raise PlanError("'tasks' must be a non-empty list")
    return data


def validate_tasks(tasks: list) -> dict[str, dict]:
    by_id: dict[str, dict] = {}
    for index, task in enumerate(tasks):
        if not isinstance(task, dict):
            raise PlanError(f"task #{index} is not a mapping")

        missing = REQUIRED_TASK_KEYS - task.keys()
        if missing:
            raise PlanError(f"task #{index} is missing key(s): {', '.join(sorted(missing))}")

        unknown = task.keys() - REQUIRED_TASK_KEYS - OPTIONAL_TASK_KEYS
        if unknown:
            raise PlanError(f"task {task['id']}: unknown key(s): {', '.join(sorted(unknown))}")

        task_id = task["id"]
        if task_id in by_id:
            raise PlanError(f"duplicate task id: {task_id}")
        if task["status"] not in STATUSES:
            raise PlanError(
                f"task {task_id}: status '{task['status']}' is not one of {sorted(STATUSES)}"
            )
        if not task["acceptance"]:
            raise PlanError(f"task {task_id}: 'acceptance' must list at least one criterion")

        by_id[task_id] = task

    for task_id, task in by_id.items():
        for dependency in task.get("depends_on") or []:
            if dependency not in by_id:
                raise PlanError(f"task {task_id} depends on unknown task '{dependency}'")
            if dependency == task_id:
                raise PlanError(f"task {task_id} depends on itself")

    return by_id


def load_registry(paths: list[Path]) -> dict[str, dict[str, dict]]:
    """Map plan name -> tasks, across every plan, so blocked_by can resolve.

    Plans that do not parse are skipped silently here; their own process() call
    reports the error. A blocked_by pointing into an unparseable plan then fails
    as unresolvable, which is the right answer either way.
    """
    registry: dict[str, dict[str, dict]] = {}
    for path in paths:
        try:
            plan = load_plan(path)
            registry[plan["plan"]] = validate_tasks(plan["tasks"])
        except (PlanError, yaml.YAMLError, OSError):
            continue
    return registry


def parse_blocker(reference: str, task_id: str) -> tuple[str, str]:
    if not isinstance(reference, str) or reference.count("/") != 1:
        raise PlanError(
            f"task {task_id}: blocked_by entry '{reference}' must have the form 'plan/Tn'"
        )
    plan_name, blocker_id = reference.split("/")
    if not plan_name or not blocker_id:
        raise PlanError(
            f"task {task_id}: blocked_by entry '{reference}' must have the form 'plan/Tn'"
        )
    return plan_name, blocker_id


def validate_blockers(
    plan_name: str, by_id: dict[str, dict], registry: dict[str, dict[str, dict]]
) -> None:
    """Every blocked_by must resolve, and must point outside its own plan."""
    for task_id, task in by_id.items():
        for reference in task.get("blocked_by") or []:
            other_plan, blocker_id = parse_blocker(reference, task_id)
            if other_plan == plan_name:
                raise PlanError(
                    f"task {task_id}: blocked_by '{reference}' names its own plan -- "
                    "use depends_on for edges within a plan"
                )
            if other_plan not in registry:
                raise PlanError(
                    f"task {task_id}: blocked_by '{reference}' names unknown plan '{other_plan}'"
                )
            if blocker_id not in registry[other_plan]:
                raise PlanError(
                    f"task {task_id}: blocked_by '{reference}' names unknown task "
                    f"'{blocker_id}' in plan '{other_plan}'"
                )


def unmet_blockers(task: dict, registry: dict[str, dict[str, dict]]) -> list[str]:
    """The blocked_by references that are not yet satisfied. Empty means clear."""
    unmet = []
    for reference in task.get("blocked_by") or []:
        other_plan, blocker_id = reference.split("/")
        blocker = registry.get(other_plan, {}).get(blocker_id)
        if blocker is None or blocker["status"] not in SATISFIED:
            unmet.append(reference)
    return unmet


def find_cross_plan_cycle(registry: dict[str, dict[str, dict]]) -> list[str] | None:
    """Detect a cycle over depends_on and blocked_by combined.

    Neither edge kind can cycle on its own -- depends_on is checked per plan and
    blocked_by only ever leaves a plan -- but the union of the two can, and a
    cycle there deadlocks every agent that consults --next.
    """
    edges: dict[str, list[str]] = {}
    for plan_name, by_id in registry.items():
        for task_id, task in by_id.items():
            node = f"{plan_name}/{task_id}"
            targets = [f"{plan_name}/{d}" for d in task.get("depends_on") or []]
            targets += [str(b) for b in task.get("blocked_by") or []]
            # A self-edge is a malformed blocked_by, not a cycle; validate_blockers
            # says so far more usefully than a one-node trail would.
            edges[node] = [t for t in targets if t != node]

    WHITE, GREY, BLACK = 0, 1, 2
    colour = {node: WHITE for node in edges}

    def walk(node: str, trail: list[str]) -> list[str] | None:
        colour[node] = GREY
        trail.append(node)
        for target in edges.get(node, []):
            if target not in colour:
                continue
            if colour[target] == GREY:
                return trail[trail.index(target) :] + [target]
            if colour[target] == WHITE:
                found = walk(target, trail)
                if found:
                    return found
        trail.pop()
        colour[node] = BLACK
        return None

    for node in sorted(edges):
        if colour[node] == WHITE:
            found = walk(node, [])
            if found:
                return found
    return None


def topological_waves(by_id: dict[str, dict]) -> list[list[str]]:
    """Group tasks into waves; every task in a wave may run concurrently.

    Raises PlanError naming the tasks involved if the graph contains a cycle.
    """
    remaining = {task_id: set(task.get("depends_on") or []) for task_id, task in by_id.items()}
    waves: list[list[str]] = []
    resolved: set[str] = set()

    while remaining:
        ready = sorted(t for t, deps in remaining.items() if deps <= resolved)
        if not ready:
            raise PlanError("dependency cycle among: " + ", ".join(sorted(remaining)))
        waves.append(ready)
        resolved.update(ready)
        for task_id in ready:
            del remaining[task_id]

    return waves


def validate_status_consistency(
    by_id: dict[str, dict], registry: dict[str, dict[str, dict]]
) -> list[str]:
    problems = []
    for task_id, task in by_id.items():
        if task["status"] not in {"done", "in_progress"}:
            continue
        for dependency in task.get("depends_on") or []:
            if by_id[dependency]["status"] not in SATISFIED:
                problems.append(
                    f"task {task_id} is '{task['status']}' but depends on {dependency}, "
                    f"which is '{by_id[dependency]['status']}'"
                )
        for reference in unmet_blockers(task, registry):
            other_plan, blocker_id = reference.split("/")
            blocker = registry.get(other_plan, {}).get(blocker_id)
            state = blocker["status"] if blocker else "unresolvable"
            problems.append(
                f"task {task_id} is '{task['status']}' but is blocked_by {reference}, "
                f"which is '{state}'"
            )
    return problems


def ready_tasks(
    by_id: dict[str, dict], registry: dict[str, dict[str, dict]]
) -> tuple[list[str], list[tuple[str, list[str]]]]:
    """Split todo tasks into those that may start and those held cross-plan.

    A task whose only obstacle is a blocked_by is reported separately rather than
    dropped: "not yet, and here is what it waits on" is more useful to an agent
    than silence.
    """
    ready, waiting = [], []
    for task_id in sorted(by_id):
        task = by_id[task_id]
        if task["status"] != "todo":
            continue
        if any(by_id[d]["status"] not in SATISFIED for d in task.get("depends_on") or []):
            continue
        unmet = unmet_blockers(task, registry)
        if unmet:
            waiting.append((task_id, unmet))
        else:
            ready.append(task_id)
    return ready, waiting


def to_mermaid(plan: dict, by_id: dict[str, dict]) -> str:
    shape = {
        "done": '["{label}"]',
        "abandoned": '["{label}"]',
        "in_progress": '("{label}")',
        "blocked": '{{"{label}"}}',
        "todo": '["{label}"]',
    }
    lines = ["```mermaid", "graph TD"]
    for task_id, task in by_id.items():
        label = f"{task_id}: {task['title']}".replace('"', "'")
        lines.append("  " + task_id + shape[task["status"]].format(label=label))
    for task_id, task in by_id.items():
        for dependency in task.get("depends_on") or []:
            lines.append(f"  {dependency} --> {task_id}")
    for status, style in (
        ("done", "fill:#d4edda,stroke:#28a745"),
        ("in_progress", "fill:#fff3cd,stroke:#ffc107"),
        ("blocked", "fill:#f8d7da,stroke:#dc3545"),
    ):
        for task_id, task in by_id.items():
            if task["status"] == status:
                lines.append(f"  style {task_id} {style}")
    lines.append("```")
    return "\n".join(lines)


def process(path: Path, args: argparse.Namespace, registry: dict[str, dict[str, dict]]) -> bool:
    try:
        plan = load_plan(path)
        by_id = validate_tasks(plan["tasks"])
        validate_blockers(plan["plan"], by_id, registry)
        waves = topological_waves(by_id)
    except (PlanError, yaml.YAMLError) as error:
        print(f"{display(path)}: {error}")
        return False

    problems = validate_status_consistency(by_id, registry)
    if problems:
        print(f"{display(path)}: inconsistent status:")
        for problem in problems:
            print(f"  {problem}")
        return False

    if args.mermaid:
        print(to_mermaid(plan, by_id))
        return True

    if args.waves:
        print(f"{plan['plan']}: {plan['title']}")
        for index, wave in enumerate(waves, start=1):
            print(f"\n  wave {index} ({len(wave)} task(s), may run concurrently):")
            for task_id in wave:
                task = by_id[task_id]
                unmet = unmet_blockers(task, registry)
                held = f"   waits on {', '.join(unmet)}" if unmet else ""
                print(f"    [{task['status']:11s}] {task_id}  {task['title']}{held}")
        return True

    if args.next:
        ready, waiting = ready_tasks(by_id, registry)
        if ready:
            print(f"{plan['plan']}: ready to start")
            for task_id in ready:
                print(f"  {task_id}  {by_id[task_id]['title']}")
        else:
            unfinished = [t for t, v in by_id.items() if v["status"] not in SATISFIED]
            print("no task is ready" + (" -- plan complete" if not unfinished else ""))
        if waiting:
            print(f"{plan['plan']}: held by another plan")
            for task_id, unmet in waiting:
                print(f"  {task_id}  {by_id[task_id]['title']}")
                print(f"      waits on {', '.join(unmet)}")
        return True

    done = sum(1 for t in by_id.values() if t["status"] in SATISFIED)
    print(f"{display(path)}: OK -- {len(by_id)} tasks, {len(waves)} wave(s), {done}/{len(by_id)} complete")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plans", nargs="*", type=Path, help="plan files (default: tasks/*.yaml)")
    parser.add_argument("--next", action="store_true", help="list tasks ready to start")
    parser.add_argument("--waves", action="store_true", help="show concurrent execution waves")
    parser.add_argument("--mermaid", action="store_true", help="emit a Mermaid graph")
    args = parser.parse_args()

    paths = args.plans or sorted(p for p in TASKS_DIR.glob("*.yaml") if not p.name.startswith("_"))
    if not paths:
        print("no task plans found under tasks/")
        return 0

    # blocked_by resolves against every plan in the tree, not just the ones named
    # on the command line, so validating one file still checks its cross-plan
    # edges. Archived plans are included here and only here: they still have to
    # RESOLVE, but they are complete and do not need reporting.
    known = [p for p in TASKS_DIR.glob("*.yaml") if not p.name.startswith("_")]
    known += list(ARCHIVE_DIR.glob("*.yaml"))
    registry = load_registry(sorted(set(known + list(paths))))

    cycle = find_cross_plan_cycle(registry)
    if cycle:
        print("dependency cycle across plans: " + " -> ".join(cycle))
        return 1

    return 0 if all([process(path, args, registry) for path in paths]) else 1


if __name__ == "__main__":
    sys.exit(main())
