#!/usr/bin/env python3
"""Check the build shape, the project registries and the naming rules the compiler cannot (AGENTS.md §1-§3, §6; ADR-001).

    python3 Build/CheckProjectFiles.py                # the repository; exit 1 lists every finding
    python3 Build/CheckProjectFiles.py --self-test    # the broken fixtures under Build/Fixtures/ProjectFiles, each rule once
    python3 Build/CheckProjectFiles.py --root <dir>   # another tree in the same shape

A finding is one line, `<file>:<line>: <rule>: <message>`, and any finding fails the run. The rules:

  solution-missing    the solution lists a project file that does not exist
  solution-unlisted   a .vcxproj in the tree is not in the solution (so CI would never build it)
  solution-directory  a project is not in a flat directory of its own name (Tests/<Name>Tests for a suite)
  platform            a configuration other than Debug|x64 and Release|x64, or a condition naming one
  setting             a setting ADR-001 fixes is absent or has another value, in either configuration
  alignment           Debug and Release differ on a property outside the set AGENTS.md §3 enumerates
  include-directory   an include directory that is not $(SolutionDir)<AnotherProject>
  macro-family        a project defines a macro of the Windows family Core/WindowsHeader.h owns
  unregistered        a .cpp or .h in the project directory that the .vcxproj does not list
  missing             a listed file that does not exist
  filters             the .vcxproj and its .filters disagree, or the .filters is absent
  subdirectory        a C++ file in a subdirectory, where clang-tidy's header filter never looks
  compiled-shaders    CompiledShaders\\ listed as source; it is build output
  file-name           R7: a file that is not PascalCase.cpp or .h (the wizard's names excepted)
  type-affix          R2: a class, struct or enum defined with a prefix or suffix
  spelling            R11: an identifier in the other half of a spelling family
  tidy-regex          .clang-tidy's HeaderFilterRegex does not name exactly the projects the solution lists
  suite-empty         a *Tests project with no TEST_METHOD and no SuiteSmoke.cpp (vstest passes an empty suite)
  suite-stale         SuiteSmoke.cpp beside real tests; it is deleted when the first real test lands

Exit codes: 0 no finding; 1 at least one finding; 2 the tree could not be checked (no solution, or two).

Runs on Linux (python3) and Windows (python) with the standard library alone; the self-test is what
proves the rules fire, and it runs on either.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path

MSBUILD = "{http://schemas.microsoft.com/developer/msbuild/2003}"
CONFIGURATIONS = ("Debug", "Release")
PLATFORM = "x64"
SKIPPED_DIRECTORIES = {".git", ".vs", "x64"}
FIXTURES = Path("Build") / "Fixtures" / "ProjectFiles"
# Vendored, and the one exception to R14 (owner, 2026-09-17): neither named nor read by any rule here.
VENDORED = {"Client/d3dx12.h"}
SHADER_DIRECTORY = "Shaders"
COMPILED_SHADER_DIRECTORY = "CompiledShaders"
CPP_SUFFIXES = {".cpp", ".h"}
UNUSED_CPP_SUFFIXES = {".hpp", ".hh", ".hxx", ".cc", ".cxx", ".c", ".inl", ".inc", ".ipp"}
WIZARD_FILE_NAMES = {"pch.h", "pch.cpp", "framework.h", "targetver.h", "Resource.h"}
FILE_NAME_RE = re.compile(r"^[A-Z][A-Za-z0-9]*\.(cpp|h)$")

# ADR-001's settings table. Every project states these explicitly, and both configurations read them.
FIXED_PROPERTIES = {
    "PlatformToolset": "v145",
    "CharacterSet": "Unicode",
    "PreferredToolArchitecture": "x64",
    "WindowsTargetPlatformVersion": "10.0",
    "OutDir": "$(SolutionDir)x64\\$(Configuration)\\",
    "IntDir": "$(SolutionDir)x64\\$(Configuration)\\Intermediate\\$(ProjectName)\\",
}
FIXED_COMPILE = {
    "WarningLevel": "Level4",
    "TreatWarningAsError": "true",
    "SDLCheck": "true",
    "ConformanceMode": "true",
    "LanguageStandard": "stdcpplatest",
    "FloatingPointModel": "Precise",
    "EnableEnhancedInstructionSet": "AdvancedVectorExtensions2",
    "ExceptionHandling": "Sync",
    "MultiProcessorCompilation": "true",
    "PrecompiledHeader": "Use",
    "PrecompiledHeaderFile": "pch.h",
}
# What AGENTS.md §3 lets the two configurations differ in, and the value ADR-001 fixes for each.
CONFIGURATION_PROPERTIES = {
    "Debug": {"UseDebugLibraries": "true", "LinkIncremental": "true"},
    "Release": {"UseDebugLibraries": "false", "LinkIncremental": "false", "WholeProgramOptimization": "true"},
}
CONFIGURATION_COMPILE = {
    "Debug": {"Optimization": "Disabled", "RuntimeLibrary": "MultiThreadedDebugDLL"},
    "Release": {
        "Optimization": "MaxSpeed",
        "RuntimeLibrary": "MultiThreadedDLL",
        "FunctionLevelLinking": "true",
        "IntrinsicFunctions": "true",
    },
}
CONFIGURATION_LINK = {
    "Debug": {},
    "Release": {"EnableCOMDATFolding": "true", "OptimizeReferences": "true"},
}
CONFIGURATION_DEFINITION = {"Debug": "_DEBUG", "Release": "NDEBUG"}
# The Windows macro family Core/WindowsHeader.h owns (AGENTS.md §4): a /D of any of these is C4005 under /WX.
MACRO_FAMILY = {"NOMINMAX", "WIN32_LEAN_AND_MEAN", "NODRAWTEXT", "NOGDI", "NOBITMAP", "NOMCX", "NOSERVICE", "NOHELP"}

# R2: the affixes clang-tidy cannot see. A definition only; a forward declaration of an SDK interface
# (`struct ID3D12Device;`) is that interface's name, not ours.
TYPE_DEFINITION_RE = re.compile(
    r"\b(?:class|struct|enum(?:\s+(?:class|struct))?)\s+(?:alignas\s*\([^)]*\)\s*)?([A-Za-z_]\w*)\s*(?:final\s*)?(?::(?!:)|\{)"
)
TYPE_AFFIX_RE = re.compile(r"^[ICSE][A-Z]|(?:Base|Abstract|Impl|_t)$")

# R11: the other half of each family AGENTS.md lists, as the stem that catches every inflection.
SPELLING_STEMS = {
    "colour": "color",
    "initialis": "initializ",
    "serialis": "serializ",
    "normalis": "normaliz",
    "quantis": "quantiz",
    "synchronis": "synchroniz",
    "behaviour": "behavior",
    "neighbour": "neighbor",
    "centre": "center",
    "grey": "gray",
    "cancelled": "canceled",
    "cancelling": "canceling",
}
IDENTIFIER_RE = re.compile(r"[A-Za-z_]\w*")

TEST_METHOD_RE = re.compile(r"\bTEST_METHOD\s*\(")
SMOKE_FILE = "SuiteSmoke.cpp"

HEADER_FILTER_LINE_RE = re.compile(r"^HeaderFilterRegex:\s*'(.*)'\s*$", re.M)
HEADER_FILTER_SHAPE_RE = re.compile(r"^\(([^()]*)\)\[/\\\\\]\[A-Za-z0-9\]\+\\\.h\$$")
TESTS_ALTERNATIVE = "[A-Za-z0-9]+Tests"


@dataclass
class Finding:
    rule: str
    path: str
    message: str
    line: int = 0

    def __str__(self) -> str:
        where = f"{self.path}:{self.line}" if self.line else self.path
        return f"{where}: {self.rule}: {self.message}"


@dataclass
class Configuration:
    """The effective properties and item-definition metadata of one configuration."""

    properties: dict[str, str] = field(default_factory=dict)
    metadata: dict[str, dict[str, str]] = field(default_factory=dict)

    def compile(self, name: str) -> str | None:
        return self.metadata.get("ClCompile", {}).get(name)

    def link(self, name: str) -> str | None:
        return self.metadata.get("Link", {}).get(name)


@dataclass
class Item:
    kind: str
    include: str
    metadata: dict[str, str]


@dataclass
class Project:
    name: str
    file: Path  # absolute
    relative: str  # posix, from the root
    directory: Path
    configurations: dict[str, Configuration]
    items: list[Item]
    project_configurations: list[str]
    findings: list[Finding]

    @property
    def is_suite(self) -> bool:
        return self.name.endswith("Tests")

    def listed(self, kind: str) -> list[str]:
        return [item.include.replace("\\", "/") for item in self.items if item.kind == kind]


def repository_root() -> Path:
    return Path(__file__).resolve().parent.parent


def local(tag: str) -> str:
    return tag[len(MSBUILD):] if tag.startswith(MSBUILD) else tag


def text_of(element: ElementTree.Element) -> str:
    return (element.text or "").strip()


CONDITION_RE = re.compile(r"^\s*'\$\(Configuration\)(\|\$\(Platform\))?'\s*==\s*'([^'|]*)(?:\|([^']*))?'\s*$")


def configurations_of(condition: str | None, project: Project, where: str) -> list[str]:
    """The configurations a group applies to, or every one when it has no condition."""
    if condition is None:
        return list(CONFIGURATIONS)
    match = CONDITION_RE.match(condition)
    if not match:
        project.findings.append(Finding("platform", project.relative, f"{where}: condition not understood: {condition.strip()}"))
        return []
    configuration, platform = match.group(2), match.group(3)
    if configuration not in CONFIGURATIONS or (platform is not None and platform != PLATFORM):
        project.findings.append(
            Finding("platform", project.relative, f"{where}: condition names {configuration}|{platform or '*'}; only Debug|x64 and Release|x64 exist")
        )
        return []
    return [configuration]


def resolve_metadata(previous: str | None, value: str, name: str) -> str:
    """`_DEBUG;%(PreprocessorDefinitions)` appends to what an earlier group set, as MSBuild does."""
    reference = f"%({name})"
    if reference in value and previous is not None:
        return value.replace(reference, previous)
    return value


def parse_project(root: Path, file: Path) -> Project:
    relative = file.relative_to(root).as_posix()
    project = Project(file.stem, file, relative, file.parent, {c: Configuration() for c in CONFIGURATIONS}, [], [], [])
    try:
        tree = ElementTree.parse(file)
    except ElementTree.ParseError as error:
        print(f"CheckProjectFiles: {relative}: not well-formed XML: {error}")
        raise SystemExit(2)
    for group in tree.getroot():
        tag = local(group.tag)
        condition = group.get("Condition")
        if tag == "ItemGroup" and group.get("Label") == "ProjectConfigurations":
            project.project_configurations = [element.get("Include", "") for element in group if local(element.tag) == "ProjectConfiguration"]
        elif tag == "ItemGroup":
            for element in group:
                metadata = {local(child.tag): text_of(child) for child in element}
                project.items.append(Item(local(element.tag), element.get("Include", ""), metadata))
        elif tag == "PropertyGroup":
            targets = configurations_of(condition, project, "PropertyGroup")
            for element in group:
                for configuration in targets:
                    project.configurations[configuration].properties[local(element.tag)] = text_of(element)
        elif tag == "ItemDefinitionGroup":
            targets = configurations_of(condition, project, "ItemDefinitionGroup")
            for definition in group:
                kind = local(definition.tag)
                for element in definition:
                    name = local(element.tag)
                    for configuration in targets:
                        table = project.configurations[configuration].metadata.setdefault(kind, {})
                        table[name] = resolve_metadata(table.get(name), text_of(element), name)
    return project


def check_shape(project: Project) -> None:
    findings = project.findings
    expected_configurations = {f"{c}|{PLATFORM}" for c in CONFIGURATIONS}
    actual_configurations = set(project.project_configurations)
    for extra in sorted(actual_configurations - expected_configurations):
        findings.append(Finding("platform", project.relative, f"configuration {extra}; only Debug|x64 and Release|x64 exist"))
    for absent in sorted(expected_configurations - actual_configurations):
        findings.append(Finding("platform", project.relative, f"configuration {absent} is not declared"))

    def require(table_of, name: str, expected: str, configurations: tuple[str, ...] = CONFIGURATIONS) -> None:
        wrong = [c for c in configurations if table_of(project.configurations[c], name) != expected]
        if wrong:
            actual = table_of(project.configurations[wrong[0]], name)
            shown = "absent" if actual is None else f"'{actual}'"
            findings.append(Finding("setting", project.relative, f"{name} is {shown} in {', '.join(wrong)}; ADR-001 fixes '{expected}'"))

    def property_of(configuration: Configuration, name: str) -> str | None:
        return configuration.properties.get(name)

    for name, expected in FIXED_PROPERTIES.items():
        require(property_of, name, expected)
    for name, expected in FIXED_COMPILE.items():
        require(Configuration.compile, name, expected)
    for configuration in CONFIGURATIONS:
        for name, expected in CONFIGURATION_PROPERTIES[configuration].items():
            require(property_of, name, expected, (configuration,))
        for name, expected in CONFIGURATION_COMPILE[configuration].items():
            require(Configuration.compile, name, expected, (configuration,))
        for name, expected in CONFIGURATION_LINK[configuration].items():
            require(Configuration.link, name, expected, (configuration,))

    kind = project.configurations["Debug"].properties.get("ConfigurationType")
    subtype = project.configurations["Debug"].properties.get("ProjectSubType")
    if project.is_suite:
        if kind != "DynamicLibrary" or subtype != "NativeUnitTestProject":
            findings.append(
                Finding("setting", project.relative, "a *Tests project is a DynamicLibrary with ProjectSubType NativeUnitTestProject (ADR-001)")
            )
    elif kind not in ("StaticLibrary", "Application"):
        findings.append(Finding("setting", project.relative, f"ConfigurationType is '{kind or 'absent'}'; a library is StaticLibrary, an executable Application"))

    pch = [item for item in project.items if item.kind == "ClCompile" and item.include.replace("\\", "/") == "pch.cpp"]
    if not pch or pch[0].metadata.get("PrecompiledHeader") != "Create":
        findings.append(Finding("setting", project.relative, "pch.cpp is not listed with <PrecompiledHeader>Create</PrecompiledHeader>"))

    # Alignment: outside the enumerated set, the two configurations read identically.
    debug, release = project.configurations["Debug"], project.configurations["Release"]
    allowed_properties = set(CONFIGURATION_PROPERTIES["Debug"]) | set(CONFIGURATION_PROPERTIES["Release"])
    for name in sorted(set(debug.properties) | set(release.properties)):
        if name not in allowed_properties and debug.properties.get(name) != release.properties.get(name):
            findings.append(Finding("alignment", project.relative, f"{name} differs: Debug '{debug.properties.get(name)}', Release '{release.properties.get(name)}'"))
    allowed_metadata = {
        "ClCompile": set(CONFIGURATION_COMPILE["Debug"]) | set(CONFIGURATION_COMPILE["Release"]) | {"PreprocessorDefinitions"},
        "Link": set(CONFIGURATION_LINK["Debug"]) | set(CONFIGURATION_LINK["Release"]),
    }
    for kind_name in sorted(set(debug.metadata) | set(release.metadata)):
        debug_table, release_table = debug.metadata.get(kind_name, {}), release.metadata.get(kind_name, {})
        for name in sorted(set(debug_table) | set(release_table)):
            if name in allowed_metadata.get(kind_name, set()):
                continue
            if debug_table.get(name) != release_table.get(name):
                findings.append(
                    Finding("alignment", project.relative, f"{kind_name}.{name} differs: Debug '{debug_table.get(name)}', Release '{release_table.get(name)}'")
                )

    # Definitions: the configuration's own macro and nothing else may differ, and none of the family.
    definitions = {c: [d for d in (project.configurations[c].compile("PreprocessorDefinitions") or "").split(";") if d] for c in CONFIGURATIONS}
    for configuration in CONFIGURATIONS:
        own = CONFIGURATION_DEFINITION[configuration]
        if own not in definitions[configuration]:
            findings.append(Finding("setting", project.relative, f"{configuration} does not define {own}"))
        other = CONFIGURATION_DEFINITION["Release" if configuration == "Debug" else "Debug"]
        if other in definitions[configuration]:
            findings.append(Finding("setting", project.relative, f"{configuration} defines {other}"))
    stripped = {c: [d for d in definitions[c] if d not in CONFIGURATION_DEFINITION.values()] for c in CONFIGURATIONS}
    if stripped["Debug"] != stripped["Release"]:
        findings.append(Finding("alignment", project.relative, f"PreprocessorDefinitions differ beyond _DEBUG/NDEBUG: Debug {stripped['Debug']}, Release {stripped['Release']}"))
    for macro in sorted({d for c in CONFIGURATIONS for d in definitions[c]} & MACRO_FAMILY):
        findings.append(Finding("macro-family", project.relative, f"defines {macro}; Core/WindowsHeader.h owns the family and a /D of it is C4005 under /WX"))


def check_include_directories(project: Project, project_names: set[str]) -> None:
    seen: set[str] = set()
    for configuration in CONFIGURATIONS:
        for entry in (project.configurations[configuration].compile("AdditionalIncludeDirectories") or "").split(";"):
            entry = entry.strip()
            if not entry or entry == "%(AdditionalIncludeDirectories)" or entry in seen:
                continue
            seen.add(entry)
            other = entry[len("$(SolutionDir)"):] if entry.startswith("$(SolutionDir)") else None
            if other is None or other not in project_names or other == project.name:
                findings_message = "a project never lists its own directory" if other == project.name else "only $(SolutionDir)<AnotherProject> is listed (AGENTS.md §3)"
                project.findings.append(Finding("include-directory", project.relative, f"include directory '{entry}': {findings_message}"))


def strip_comments_and_literals(text: str) -> str:
    """The code with comments and string and character literals blanked, newlines kept, so that prose is never checked."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i : i + 2]
        if two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == '"' and i > 0 and text[i - 1] == "R":
            # R"delim( ... )delim"
            open_paren = text.find("(", i)
            delimiter = text[i + 1 : open_paren] if open_paren > 0 else ""
            close = f"){delimiter}\""
            j = text.find(close, open_paren)
            j = n if j < 0 else j + len(close)
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(" " * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def check_source(project: Project, file: Path, relative: str) -> None:
    """R2 and R11 over one .cpp or .h."""
    code = strip_comments_and_literals(file.read_text(encoding="utf-8-sig", errors="replace"))
    for match in TYPE_DEFINITION_RE.finditer(code):
        name = match.group(1)
        if TYPE_AFFIX_RE.search(name):
            project.findings.append(Finding("type-affix", relative, f"type '{name}' carries a prefix or suffix; PascalCase means the name and nothing else (R2)", line_of(code, match.start(1))))
    reported: set[str] = set()
    for match in IDENTIFIER_RE.finditer(code):
        identifier = match.group(0)
        if identifier in reported:
            continue
        lowered = identifier.lower()
        for stem, sdk in SPELLING_STEMS.items():
            if stem in lowered:
                reported.add(identifier)
                project.findings.append(Finding("spelling", relative, f"identifier '{identifier}' spells '{stem}'; the SDK's half of the family is '{sdk}' (R11)", line_of(code, match.start())))
                break


def check_registry(root: Path, project: Project) -> None:
    findings = project.findings
    directory = project.directory
    on_disk = {"ClCompile": set(), "ClInclude": set()}
    for path in sorted(directory.iterdir()):
        if path.is_dir():
            if path.name == COMPILED_SHADER_DIRECTORY:
                continue
            for inner_directory, _, names in os.walk(path):
                for name in sorted(names):
                    inner = Path(inner_directory) / name
                    if inner.suffix in CPP_SUFFIXES | UNUSED_CPP_SUFFIXES:
                        findings.append(
                            Finding("subdirectory", inner.relative_to(root).as_posix(), "C++ in a subdirectory; project directories are flat, and clang-tidy's header filter never looks here (AGENTS.md §2)")
                        )
            continue
        relative = path.relative_to(root).as_posix()
        if relative in VENDORED:
            on_disk["ClInclude" if path.suffix == ".h" else "ClCompile"].add(path.name)
            continue
        if path.suffix in UNUSED_CPP_SUFFIXES:
            findings.append(Finding("file-name", relative, f"'{path.suffix}' is not used; a file is .cpp or .h (R7)"))
            continue
        if path.suffix not in CPP_SUFFIXES:
            continue
        on_disk["ClInclude" if path.suffix == ".h" else "ClCompile"].add(path.name)
        if path.name not in WIZARD_FILE_NAMES and not FILE_NAME_RE.match(path.name):
            findings.append(Finding("file-name", relative, "a file is PascalCase, named for its primary type (R7)"))
        check_source(project, path, relative)

    listed = {"ClCompile": set(), "ClInclude": set()}
    for item in project.items:
        if item.kind not in listed:
            continue
        include = item.include.replace("\\", "/")
        if include.startswith(COMPILED_SHADER_DIRECTORY + "/"):
            findings.append(Finding("compiled-shaders", project.relative, f"lists {include}; CompiledShaders/ is build output and never source (AGENTS.md §2)"))
        elif "/" in include:
            findings.append(Finding("subdirectory", project.relative, f"lists {include}, a file outside the flat project directory"))
        else:
            listed[item.kind].add(include)
    for kind in ("ClCompile", "ClInclude"):
        for name in sorted(on_disk[kind] - listed[kind]):
            findings.append(Finding("unregistered", f"{project.directory.relative_to(root).as_posix()}/{name}", f"not listed in {project.file.name} as {kind}; it is not built"))
        for name in sorted(listed[kind] - on_disk[kind]):
            findings.append(Finding("missing", project.relative, f"lists {name} as {kind}, and no such file exists"))

    filters_file = project.file.with_name(project.file.name + ".filters")
    if not filters_file.exists():
        findings.append(Finding("filters", project.relative, "has no .filters file"))
        return
    try:
        filters_tree = ElementTree.parse(filters_file)
    except ElementTree.ParseError as error:
        findings.append(Finding("filters", filters_file.relative_to(root).as_posix(), f"not well-formed XML: {error}"))
        return
    in_filters = {
        element.get("Include", "").replace("\\", "/")
        for group in filters_tree.getroot()
        if local(group.tag) == "ItemGroup"
        for element in group
        if local(element.tag) in ("ClCompile", "ClInclude")
    }
    in_project = {item.include.replace("\\", "/") for item in project.items if item.kind in ("ClCompile", "ClInclude")}
    filters_relative = filters_file.relative_to(root).as_posix()
    for name in sorted(in_project - in_filters):
        findings.append(Finding("filters", filters_relative, f"{name} is in the .vcxproj and not here"))
    for name in sorted(in_filters - in_project):
        findings.append(Finding("filters", filters_relative, f"{name} is here and not in the .vcxproj"))


def check_suite(project: Project) -> None:
    if not project.is_suite:
        return
    real_tests = 0
    smoke_listed = False
    for name in project.listed("ClCompile"):
        if "/" in name:
            continue
        file = project.directory / name
        if not file.exists():
            continue
        if name == SMOKE_FILE:
            smoke_listed = True
            continue
        real_tests += len(TEST_METHOD_RE.findall(file.read_text(encoding="utf-8-sig", errors="replace")))
    if not smoke_listed and real_tests == 0:
        project.findings.append(Finding("suite-empty", project.relative, "no TEST_METHOD and no SuiteSmoke.cpp; vstest passes an empty suite (AGENTS.md §3)"))
    if smoke_listed and real_tests > 0:
        project.findings.append(Finding("suite-stale", project.relative, "SuiteSmoke.cpp beside real tests; delete it, the suite has its first test (AGENTS.md §3)"))


def check_tidy_regex(root: Path, project_names: set[str]) -> list[Finding]:
    tidy = root / ".clang-tidy"
    relative = ".clang-tidy"
    if not tidy.exists():
        return [Finding("tidy-regex", relative, "absent; clang-tidy checks nothing without it")]
    match = HEADER_FILTER_LINE_RE.search(tidy.read_text(encoding="utf-8-sig"))
    if not match:
        return [Finding("tidy-regex", relative, "no HeaderFilterRegex line")]
    shape = HEADER_FILTER_SHAPE_RE.match(match.group(1))
    if not shape:
        return [Finding("tidy-regex", relative, f"HeaderFilterRegex is '{match.group(1)}'; expected '(<Project>|...|{TESTS_ALTERNATIVE})[/\\\\][A-Za-z0-9]+\\.h$'")]
    alternatives = shape.group(1).split("|")
    expected = {name for name in project_names if not name.endswith("Tests")}
    listed = set(alternatives) - {TESTS_ALTERNATIVE}
    problems = []
    if TESTS_ALTERNATIVE not in alternatives:
        problems.append(f"missing {TESTS_ALTERNATIVE}")
    if expected - listed:
        problems.append(f"missing {', '.join(sorted(expected - listed))}")
    if listed - expected:
        problems.append(f"names {', '.join(sorted(listed - expected))}, which the solution does not list")
    if problems:
        return [Finding("tidy-regex", relative, "HeaderFilterRegex " + "; ".join(problems) + " (a header in an unlisted project is silently unchecked)")]
    return []


def find_solution(root: Path) -> tuple[Path | None, str | None]:
    solutions = sorted(p for p in root.iterdir() if p.suffix in (".slnx", ".sln"))
    if not solutions:
        return None, "no solution at the root"
    if len(solutions) > 1:
        return None, "more than one solution at the root: " + ", ".join(p.name for p in solutions)
    if solutions[0].suffix != ".slnx":
        return None, f"{solutions[0].name}: only the XML solution format is checked (ADR-001)"
    return solutions[0], None


def solution_projects(solution: Path) -> list[str]:
    """The Path of every <Project> in the .slnx, in document order, with forward slashes."""
    tree = ElementTree.parse(solution)
    return [element.get("Path", "").replace("\\", "/") for element in tree.getroot().iter() if element.tag == "Project" and element.get("Path")]


def tree_projects(root: Path) -> list[str]:
    found: list[str] = []
    for directory, subdirectories, names in os.walk(root):
        relative_directory = Path(directory).relative_to(root)
        subdirectories[:] = sorted(d for d in subdirectories if d not in SKIPPED_DIRECTORIES and (relative_directory / d) != FIXTURES)
        for name in sorted(names):
            if name.endswith(".vcxproj"):
                found.append((relative_directory / name).as_posix())
    return found


def check_tree(root: Path) -> tuple[list[Finding], int]:
    """Every finding for the tree at root, and the number of projects checked; exits 2 when the tree cannot be checked."""
    solution, problem = find_solution(root)
    if solution is None:
        print(f"CheckProjectFiles: {problem}")
        raise SystemExit(2)

    findings: list[Finding] = []
    listed = solution_projects(solution)
    on_disk = tree_projects(root)
    for path in sorted(set(on_disk) - set(listed)):
        findings.append(Finding("solution-unlisted", path, f"not in {solution.name}; CI would never build it"))

    projects: list[Project] = []
    for path in listed:
        file = root / path
        if not file.exists():
            findings.append(Finding("solution-missing", solution.name, f"lists {path}, which does not exist"))
            continue
        project = parse_project(root, file)
        expected_directory = f"Tests/{project.name}" if project.is_suite else project.name
        actual_directory = file.parent.relative_to(root).as_posix()
        if actual_directory != expected_directory:
            project.findings.append(Finding("solution-directory", path, f"sits in {actual_directory}/; a project sits in {expected_directory}/ (ADR-001)"))
        projects.append(project)

    project_names = {project.name for project in projects}
    for project in projects:
        check_shape(project)
        check_include_directories(project, project_names)
        check_registry(root, project)
        check_suite(project)
        findings.extend(project.findings)
    findings.extend(check_tidy_regex(root, project_names))
    return findings, len(projects)


# What the fixtures under Build/Fixtures/ProjectFiles must produce, and nothing else: every rule, once.
SELF_TEST_EXPECTED = [
    ("solution-missing", "Fixture.slnx"),
    ("solution-unlisted", "Orphan/Orphan.vcxproj"),
    ("solution-directory", "Elsewhere/Moved.vcxproj"),
    ("platform", "Shape/Shape.vcxproj"),
    ("setting", "Shape/Shape.vcxproj"),
    ("alignment", "Shape/Shape.vcxproj"),
    ("include-directory", "Shape/Shape.vcxproj"),
    ("macro-family", "Shape/Shape.vcxproj"),
    ("unregistered", "Registry/Stray.cpp"),
    ("missing", "Registry/Registry.vcxproj"),
    ("filters", "Registry/Registry.vcxproj.filters"),
    ("subdirectory", "Registry/Extra/Deep.h"),
    ("compiled-shaders", "Registry/Registry.vcxproj"),
    ("file-name", "Names/bad_name.cpp"),
    ("type-affix", "Names/Names.h"),
    ("spelling", "Names/Names.h"),
    ("tidy-regex", ".clang-tidy"),
    ("suite-empty", "Tests/EmptyTests/EmptyTests.vcxproj"),
    ("suite-stale", "Tests/StaleTests/StaleTests.vcxproj"),
]


def self_test(root: Path) -> int:
    fixtures = root / FIXTURES
    findings, count = check_tree(fixtures)
    actual = sorted((f.rule, f.path) for f in findings)
    expected = sorted(SELF_TEST_EXPECTED)
    rules_in_doc = set(re.findall(r"^  ([a-z-]+) ", __doc__, re.M))
    problems: list[str] = []
    for item in expected:
        if item not in actual:
            problems.append(f"did not fire: {item[0]} on {item[1]}")
    for item in actual:
        if item not in expected:
            problems.append(f"fired unexpectedly: {item[0]} on {item[1]}")
    for rule in sorted(rules_in_doc - {rule for rule, _ in expected}):
        problems.append(f"rule {rule} has no fixture")
    for rule in sorted({rule for rule, _ in expected} - rules_in_doc):
        problems.append(f"rule {rule} is not documented at the top of this script")
    print(f"CheckProjectFiles: self-test over {fixtures.relative_to(root).as_posix()}: {count} project(s), {len(findings)} finding(s)")
    for finding in findings:
        print(f"  {finding}")
    if problems:
        print("CheckProjectFiles: self-test FAILED:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print(f"CheckProjectFiles: self-test passed; every one of the {len(expected)} rules fired exactly where expected.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=None, help="the tree to check (default: the repository root)")
    parser.add_argument("--self-test", action="store_true", help="check the broken fixtures and expect every rule to fire once")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # the findings quote AGENTS.md's section signs
    root = (args.root or repository_root()).resolve()
    if args.self_test:
        return self_test(root)
    findings, count = check_tree(root)
    for finding in findings:
        print(finding)
    if findings:
        print(f"CheckProjectFiles: {count} project(s) checked, {len(findings)} finding(s).")
        return 1
    print(f"CheckProjectFiles: {count} project(s) checked, no findings.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
