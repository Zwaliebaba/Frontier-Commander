#!/usr/bin/env python3
"""Run the pinned clang-tidy over every hand-written translation unit the solution builds (AGENTS.md §1, §6).

    python Build\\RunClangTidy.py                        # every translation unit, in parallel; exit 1 on any finding
    python Build\\RunClangTidy.py Core\\Json.cpp           # the named files only (a file you just wrote, before you push)
    python Build\\RunClangTidy.py --project Core         # one project
    python Build\\RunClangTidy.py --dry-run              # print the commands and run nothing

Needs a Developer PowerShell: INCLUDE is what makes the CRT and the Windows SDK visible to clang's
MSVC driver, and the script stops with one line when it is not set. clang-tidy itself comes from
pip, at the version .github/workflows/build.yml pins (`python -m pip install clang-tidy==<that>`),
so that a local run and the gate are the same binary; the script prints the version it found and
warns when it is another one.

For every ClCompile item of every .vcxproj the solution lists (CompiledShaders/ and the vendored
Client/d3dx12.h excepted), clang-tidy runs through clang's MSVC driver with the switches the
project files fix (ADR-001): /std:c++latest, /EHsc, /permissive-, /arch:AVX2, /fp:precise, the
Unicode character set as /DUNICODE /D_UNICODE, the Debug configuration's preprocessor definitions
(so /D_DEBUG) and its include directories, all read from the .vcxproj rather than repeated here,
and the repository's .clang-tidy. A *Tests project also gets the directory of CppUnitTest.h,
which its project file takes from Microsoft.Cpp.UnitTest.props rather than naming.

clang-tidy's output is printed unchanged, translation unit by translation unit in solution order,
and any finding fails the run: WarningsAsErrors in .clang-tidy makes every finding an error, and
a translation unit that does not compile is a failure too.

Exit codes: 0 clean; 1 at least one finding; 2 the run could not start (no INCLUDE, no clang-tidy,
no solution, no CppUnitTest.h for a suite).
"""
from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import time
from pathlib import Path

from CheckProjectFiles import COMPILED_SHADER_DIRECTORY, VENDORED, find_solution, parse_project, repository_root, solution_projects

FIXED_SWITCHES = ["--driver-mode=cl", "/std:c++latest", "/EHsc", "/permissive-", "/arch:AVX2", "/fp:precise", "/DUNICODE", "/D_UNICODE"]
WORKFLOW = Path(".github") / "workflows" / "build.yml"
PIN_RE = re.compile(r"^\s*CLANG_TIDY_VERSION:\s*(\S+)\s*$", re.M)
VERSION_RE = re.compile(r"LLVM version (\d+(?:\.\d+)*)")


def pinned_version(root: Path) -> str | None:
    workflow = root / WORKFLOW
    if not workflow.exists():
        return None
    match = PIN_RE.search(workflow.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def clang_tidy_version(binary: str) -> str | None:
    try:
        completed = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False)
    except OSError:
        return None
    if completed.returncode != 0:
        return None
    match = VERSION_RE.search(completed.stdout)
    return match.group(1) if match else completed.stdout.strip().splitlines()[0]


def framework_include_directory() -> Path | None:
    """Where CppUnitTest.h is: under VCINSTALLDIR\\Auxiliary\\VS, or already on INCLUDE."""
    for directory in os.environ.get("INCLUDE", "").split(os.pathsep):
        if directory and (Path(directory) / "CppUnitTest.h").exists():
            return Path(directory)
    install = os.environ.get("VCINSTALLDIR")
    if install:
        auxiliary = Path(install) / "Auxiliary" / "VS"
        if auxiliary.is_dir():
            for header in sorted(auxiliary.rglob("CppUnitTest.h")):
                return header.parent
    return None


def translation_units(root: Path, selected_projects: set[str] | None, selected_files: set[Path] | None, framework: Path | None):
    """(project name, file, arguments after `--`) for every translation unit to lint, in solution order."""
    solution, problem = find_solution(root)
    if solution is None:
        print(f"RunClangTidy: {problem}")
        raise SystemExit(2)
    units = []
    needs_framework = False
    for path in solution_projects(solution):
        file = root / path
        if not file.exists():
            continue
        project = parse_project(root, file)
        if selected_projects is not None and project.name not in selected_projects:
            continue
        debug = project.configurations["Debug"]
        definitions = [d for d in (debug.compile("PreprocessorDefinitions") or "").split(";") if d and not d.startswith("%(")]
        if "_DEBUG" not in definitions:
            definitions.append("_DEBUG")
        includes = []
        for entry in (debug.compile("AdditionalIncludeDirectories") or "").split(";"):
            entry = entry.strip()
            if entry.startswith("$(SolutionDir)"):
                includes.append(root / entry[len("$(SolutionDir)"):])
        if project.is_suite:
            needs_framework = True
            if framework is not None:
                includes.append(framework)
        arguments = FIXED_SWITCHES + [f"/D{d}" for d in definitions] + [f"/I{directory}" for directory in includes]
        for include in project.listed("ClCompile"):
            if include.startswith(COMPILED_SHADER_DIRECTORY + "/"):
                continue
            source = project.directory / include
            if source.relative_to(root).as_posix() in VENDORED:
                continue
            if selected_files is not None and source.resolve() not in selected_files:
                continue
            units.append((project.name, source, arguments))
    return units, needs_framework


def run_one(binary: str, config: Path, file: Path, arguments: list[str]) -> tuple[int, str]:
    command = [binary, "--quiet", f"--config-file={config}", str(file), "--"] + arguments
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    output = completed.stdout + completed.stderr
    return completed.returncode, output


def quote(argument: str) -> str:
    return f'"{argument}"' if " " in argument else argument


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", type=Path, help="translation units to lint (default: every one the solution builds)")
    parser.add_argument("--clang-tidy", default="clang-tidy", help="the clang-tidy binary (default: clang-tidy on PATH)")
    parser.add_argument("--project", action="append", default=None, help="lint this project only; repeatable")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="translation units in flight at once")
    parser.add_argument("--dry-run", action="store_true", help="print the commands and run nothing")
    parser.add_argument("--root", type=Path, default=None, help="the tree to lint (default: the repository root)")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    root = (args.root or repository_root()).resolve()
    config = root / ".clang-tidy"
    if not config.exists():
        print("RunClangTidy: no .clang-tidy at the repository root; there is nothing to enforce.")
        return 2

    if not args.dry_run:
        if not os.environ.get("INCLUDE"):
            print("RunClangTidy: INCLUDE is not set. Run from a Developer PowerShell (VsDevCmd.bat -arch=amd64): it is what makes the CRT and the Windows SDK visible to clang's MSVC driver.")
            return 2
        version = clang_tidy_version(args.clang_tidy)
        if version is None:
            print(f"RunClangTidy: cannot run '{args.clang_tidy}'. Install the pinned version with `python -m pip install clang-tidy==<CLANG_TIDY_VERSION in {WORKFLOW.as_posix()}>`, or pass --clang-tidy <binary>.")
            return 2
        pinned = pinned_version(root)
        print(f"RunClangTidy: clang-tidy {version}" + (f" (the workflow pins {pinned})" if pinned else ""))
        if pinned and version != pinned:
            print(f"RunClangTidy: warning: CI runs {pinned}; a version change can report findings this one does not, or miss ones it reports, and the CI run is the answer that counts.")

    selected_files = {path.resolve() for path in args.files} if args.files else None
    framework = None if args.dry_run else framework_include_directory()
    units, needs_framework = translation_units(root, set(args.project) if args.project else None, selected_files, framework)
    if needs_framework and framework is None and not args.dry_run:
        print("RunClangTidy: CppUnitTest.h not found under VCINSTALLDIR\\Auxiliary\\VS or on INCLUDE; the *Tests projects cannot be linted. Run from a Developer PowerShell of a Visual Studio with the C++ unit test framework installed.")
        return 2
    if not units:
        print("RunClangTidy: no translation unit matched; nothing to lint.")
        return 0
    projects = {name for name, _, _ in units}
    print(f"RunClangTidy: {len(units)} translation unit(s) in {len(projects)} project(s), {args.jobs} at a time.")

    if args.dry_run:
        for _, file, arguments in units:
            print(" ".join(quote(a) for a in [args.clang_tidy, "--quiet", f"--config-file={config}", str(file), "--"] + arguments))
        return 0

    started = time.monotonic()
    failures = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(run_one, args.clang_tidy, config, file, arguments) for _, file, arguments in units]
        for (_, file, _), future in zip(units, futures):
            code, output = future.result()
            relative = file.relative_to(root).as_posix()
            if code != 0 or output.strip():
                print(f"--- {relative}" + ("" if code == 0 else f" (clang-tidy exit code {code})"))
                sys.stdout.write(output if output.endswith("\n") else output + "\n")
            if code != 0:
                failures += 1
    elapsed = time.monotonic() - started
    if failures:
        print(f"RunClangTidy: {failures} of {len(units)} translation unit(s) failed in {elapsed:.0f} s. Fix the code; do not narrow the filter (.clang-tidy).")
        return 1
    print(f"RunClangTidy: {len(units)} translation unit(s) clean in {elapsed:.0f} s.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:  # `--dry-run | head`
        sys.exit(0)
