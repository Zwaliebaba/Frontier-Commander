#!/usr/bin/env python3
"""Check, or fix, the formatting of every C++ file in the tree with clang-format (AGENTS.md §4, §6).

    python3 Build/CheckFormat.py                                   # check; exit 1 lists the offenders
    python3 Build/CheckFormat.py --fix                             # rewrite the offenders in place
    python3 Build/CheckFormat.py --clang-format clang-format-18    # the binary CI's Linux job uses

Every .cpp and .h under the repository root is checked against the root .clang-format, except
build output (x64/, CompiledShaders/), the .git and .vs directories, and Client/d3dx12.h, which
is a vendored third-party file and is never reformatted (AGENTS.md R14). A tree with no C++ in
it passes: the gate exists before the code does, and it starts gating on the first file.

Exit codes: 0 every file is formatted (or there is nothing to check); 1 at least one file would
change, each named on its own line; 2 clang-format could not be run.

The version matters. CI pins clang-format 18 on the Linux job because 18 and 22 break a long
argument list in different places; this script prints the version it used and warns when it is
not 18, and the Linux job is the answer that counts when a local run disagrees.

Runs on Linux (python3) and Windows (python) with the standard library alone.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import os
import subprocess
import sys
from pathlib import Path

PINNED_MAJOR = 18
EXTENSIONS = {".cpp", ".h"}
SKIPPED_DIRECTORIES = {".git", ".vs", "x64", "CompiledShaders"}
SKIPPED_FILES = {"Client/d3dx12.h"}


def repository_root() -> Path:
    return Path(__file__).resolve().parent.parent


def collect(root: Path) -> list[Path]:
    """Every C++ file under root, in a stable order, minus what is never checked."""
    files: list[Path] = []
    for directory, subdirectories, names in os.walk(root):
        subdirectories[:] = sorted(d for d in subdirectories if d not in SKIPPED_DIRECTORIES)
        for name in sorted(names):
            path = Path(directory) / name
            if path.suffix not in EXTENSIONS:
                continue
            if path.relative_to(root).as_posix() in SKIPPED_FILES:
                continue
            files.append(path)
    return files


def clang_format_version(binary: str) -> str | None:
    try:
        completed = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False)
    except OSError:
        return None
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def major_of(version_line: str) -> int | None:
    for token in version_line.replace("-", " ").split():
        if token[0].isdigit():
            try:
                return int(token.split(".")[0])
            except ValueError:
                return None
    return None


def would_change(binary: str, path: Path) -> tuple[Path, bool, str]:
    """Format to stdout and compare bytes, so that line endings are compared too."""
    completed = subprocess.run([binary, "--style=file", str(path)], capture_output=True, check=False)
    if completed.returncode != 0:
        return path, True, completed.stderr.decode("utf-8", errors="replace").strip()
    return path, completed.stdout != path.read_bytes(), ""


def fix(binary: str, path: Path) -> tuple[Path, str]:
    completed = subprocess.run([binary, "--style=file", "-i", str(path)], capture_output=True, check=False)
    return path, completed.stderr.decode("utf-8", errors="replace").strip() if completed.returncode != 0 else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clang-format", default="clang-format", help="the clang-format binary (default: clang-format on PATH)")
    parser.add_argument("--fix", action="store_true", help="rewrite the files that would change")
    parser.add_argument("--root", type=Path, default=None, help="the tree to check (default: the repository root)")
    args = parser.parse_args()

    root = (args.root or repository_root()).resolve()
    version = clang_format_version(args.clang_format)
    if version is None:
        print(f"CheckFormat: cannot run '{args.clang_format}'. Install clang-format {PINNED_MAJOR} or pass --clang-format <binary>.")
        return 2
    print(f"CheckFormat: {version}")
    major = major_of(version)
    if major != PINNED_MAJOR:
        print(f"CheckFormat: warning: CI pins clang-format {PINNED_MAJOR}; version {major} may break lines differently, and the Linux job is the answer that counts.")

    files = collect(root)
    if not files:
        print(f"CheckFormat: no C++ files under {root}; nothing to check.")
        return 0

    errors: list[str] = []
    offenders: list[Path] = []
    with concurrent.futures.ThreadPoolExecutor() as pool:
        for path, changes, error in pool.map(lambda p: would_change(args.clang_format, p), files):
            if error:
                errors.append(f"{path.relative_to(root).as_posix()}: {error}")
            elif changes:
                offenders.append(path)

    for error in errors:
        print(f"CheckFormat: error: {error}")
    if errors:
        return 2

    if not offenders:
        print(f"CheckFormat: {len(files)} file(s) checked, all formatted.")
        return 0

    if args.fix:
        with concurrent.futures.ThreadPoolExecutor() as pool:
            for path, error in pool.map(lambda p: fix(args.clang_format, p), offenders):
                if error:
                    print(f"CheckFormat: error: {path.relative_to(root).as_posix()}: {error}")
                    return 2
                print(f"CheckFormat: fixed {path.relative_to(root).as_posix()}")
        print(f"CheckFormat: {len(files)} file(s) checked, {len(offenders)} rewritten.")
        return 0

    print(f"CheckFormat: {len(files)} file(s) checked, {len(offenders)} would change:")
    for path in offenders:
        print(f"  {path.relative_to(root).as_posix()}")
    print("CheckFormat: run with --fix to rewrite them.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
