#!/usr/bin/env python3
"""Check the build shape, the project registries and the naming rules the compiler cannot (AGENTS.md §1-§3, §6; ADR-001).

    python3 Build/CheckProjectFiles.py                # the repository; exit 1 lists every finding
    python3 Build/CheckProjectFiles.py --self-test    # the broken fixtures under Build/Fixtures/ProjectFiles, each rule once
    python3 Build/CheckProjectFiles.py --root <dir>   # another tree in the same shape

A finding is one line, `<file>:<line>: <rule>: <message>`, and any finding fails the run. The rules:

  solution-missing    the solution lists a project file that does not exist
  solution-unlisted   a .vcxproj in the tree is not in the solution (so CI would never build it)
  solution-directory  a project is not in a flat directory of its own name (Tests/<Name>Tests for a suite)
  platform            a configuration outside Debug/Release on x64/ARM64, or a condition naming one
  setting             a setting ADR-001 fixes is absent or has another value, in any configuration or platform
  alignment           Debug and Release differ, within a platform, outside the set AGENTS.md §3 enumerates
  include-directory   an include directory that is not $(SolutionDir)<AnotherProject>
  macro-family        a project defines a macro of the Windows family Core/WindowsHeader.h owns
  unregistered        a .cpp or .h in the project directory that the .vcxproj does not list
  missing             a listed file that does not exist
  filters             the .vcxproj and its .filters disagree, or the .filters is absent
  subdirectory        a C++ file in a subdirectory, where clang-tidy's header filter never looks
  compiled-shaders    CompiledShaders\\ listed as source; it is build output
  file-name           R7: a file that is not PascalCase.cpp or .h (the wizard's names excepted)
  shadow              a header named like a C runtime or SDK header, which an angled include of that name then finds first
  type-affix          R2: a class, struct or enum defined with a prefix or suffix
  spelling            R11: an identifier in the other half of a spelling family
  sdk-macro           an identifier spelled like a Windows SDK macro, which the preprocessor rewrites wherever <windows.h> is in scope
  tidy-regex          .clang-tidy's HeaderFilterRegex does not name exactly the projects the solution lists
  suite-empty         a *Tests project with no TEST_METHOD and no SuiteSmoke.cpp (vstest passes an empty suite)
  suite-stale         SuiteSmoke.cpp beside real tests; it is deleted when the first real test lands
  layering-unknown    a project the ADR-001 table in this script (BUILT_ON) has no row for; an edge is a decision
  edge-reference      a ProjectReference to a project ADR-001 does not build this one on
  edge-directory      an include directory naming a project ADR-001 does not build this one on
  edge-include        a quoted include resolving into a project ADR-001 does not build this one on
  platform-header     a platform header included from Content, Sim, Net or Replica, which stay portable

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
# TWO PLATFORMS SINCE 2026-09-19 (ADR-001, owner). The owner's machine is a Snapdragon X, which ran
# the x64 build under Prism emulation; ARM64 is a native build of the same tree. A project therefore
# has FOUR slices rather than two, and this file checks each of them, because a setting that is
# right for one instruction set is not automatically right for the other - which is exactly what
# went wrong when Visual Studio first wrote the ARM64 configurations and replaced x64's AVX2 with
# ARMv8.7 in the group both platforms read.
PLATFORMS = ("x64", "ARM64")
SLICES = tuple(f"{configuration}|{platform}" for platform in PLATFORMS for configuration in CONFIGURATIONS)
SKIPPED_DIRECTORIES = {".git", ".vs", "x64", "ARM64"}
FIXTURES = Path("Build") / "Fixtures" / "ProjectFiles"
# Vendored, and the one exception to R14 (owner, 2026-09-17): neither named nor read by any rule here.
VENDORED = {"Client/d3dx12.h"}
SHADER_DIRECTORY = "Shaders"
COMPILED_SHADER_DIRECTORY = "CompiledShaders"
CPP_SUFFIXES = {".cpp", ".h"}
UNUSED_CPP_SUFFIXES = {".hpp", ".hh", ".hxx", ".cc", ".cxx", ".c", ".inl", ".inc", ".ipp"}
WIZARD_FILE_NAMES = {"pch.h", "pch.cpp", "framework.h", "targetver.h", "Resource.h"}
# Every project directory is on the include path of the projects built on it, ahead of the SDK, and MSVC
# searches /I directories for an angled include too, case-insensitively: Core/Assert.h was what DirectXMath's
# <assert.h> found (2026-09-17). So no header is named like one of the C runtime's or like an SDK header the
# tree reaches for (AGENTS.md §3). The runtime's list is the UCRT's and vcruntime's public headers.
RUNTIME_HEADER_NAMES = {
    "assert.h", "complex.h", "conio.h", "crtdbg.h", "ctype.h", "direct.h", "dos.h", "errno.h", "fcntl.h", "fenv.h", "float.h",
    "fpieee.h", "inttypes.h", "io.h", "iso646.h", "limits.h", "locale.h", "malloc.h", "math.h", "mbctype.h", "mbstring.h",
    "memory.h", "minmax.h", "new.h", "process.h", "safeint.h", "search.h", "setjmp.h", "share.h", "signal.h", "stdalign.h",
    "stdarg.h", "stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h", "stdnoreturn.h", "string.h", "tchar.h", "tgmath.h",
    "time.h", "uchar.h", "wchar.h", "wctype.h", "intrin.h", "excpt.h", "eh.h", "typeinfo.h", "vadefs.h", "sal.h",
    "windows.h", "windef.h", "winbase.h", "winuser.h", "winnt.h", "winerror.h", "unknwn.h", "objbase.h", "shellapi.h",
    "d3d12.h", "d3d12sdklayers.h", "d3dcommon.h", "d3dcompiler.h", "directxmath.h", "dxgi.h", "dxgi1_6.h", "dxgiformat.h",
    "dxgicommon.h", "winsock2.h", "ws2tcpip.h", "xaudio2.h", "x3daudio.h",
}
FILE_NAME_RE = re.compile(r"^[A-Z][A-Za-z0-9]*\.(cpp|h)$")

# ADR-001's settings table. Every project states these explicitly, and both configurations read them.
FIXED_PROPERTIES = {
    "PlatformToolset": "v145",
    "CharacterSet": "Unicode",
    "PreferredToolArchitecture": "x64",
    "WindowsTargetPlatformVersion": "10.0",
    # $(Platform) AND NOT A LITERAL x64: an ARM64 build that wrote into x64\ would overwrite the
    # other platform's binaries with ones that cannot run on it, and the first symptom would be a
    # test run failing in a way that looks like a code fault.
    "OutDir": "$(SolutionDir)$(Platform)\\$(Configuration)\\",
    "IntDir": "$(SolutionDir)$(Platform)\\$(Configuration)\\Intermediate\\$(ProjectName)\\",
}
FIXED_COMPILE = {
    "WarningLevel": "Level4",
    "TreatWarningAsError": "true",
    "SDLCheck": "true",
    "ConformanceMode": "true",
    "LanguageStandard": "stdcpplatest",
    "FloatingPointModel": "Precise",
    "ExceptionHandling": "Sync",
    "MultiProcessorCompilation": "true",
    "PrecompiledHeader": "Use",
    "PrecompiledHeaderFile": "pch.h",
}
# What ADR-001 fixes PER PLATFORM, because the value is an instruction set and an instruction set
# belongs to an architecture. It is the only setting that varies this way; everything above is the
# same on both, and everything below varies by configuration instead.
PLATFORM_COMPILE = {
    "x64": {"EnableEnhancedInstructionSet": "AdvancedVectorExtensions2"},
    "ARM64": {"EnableEnhancedInstructionSet": "CPUExtensionRequirementsARMv87"},
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

# ADR-001's table, the one place the edges are written: what each project is built on, which is the whole
# of what it may reference, put on its include path or include with a quoted include. The sets are
# already closed (Net lists Sim's Content and Core), and a suite is built on its library and what that
# is built on: a test reaches no further up than the code it covers. Edges point down only; two
# libraries at one level share what is below them, never each other. A new edge is a superseding ADR
# and a row here, in that order.
BUILT_ON = {
    "Core": set(),
    "Content": {"Core"},
    "Sim": {"Core", "Content"},
    "Net": {"Core", "Content", "Sim"},
    "Replica": {"Core", "Content", "Net", "Sim"},
    "Client": {"Core", "Content"},
    "FrontierCommander": {"Core", "Content", "Sim", "Net", "Replica", "Client"},
    "FrontierHost": {"Core", "Content", "Sim", "Net"},
}
# The portable libraries include no platform header (ADR-001): Core may, in named files through
# WindowsHeader.h, and Client, the executables and the suites may.
PORTABLE = {"Content", "Sim", "Net", "Replica"}
PLATFORM_HEADER_RE = re.compile(
    r"^(windows\.h|WindowsHeader\.h|d3d12[A-Za-z0-9_]*\.h|d3dx12\.h|d3dcompiler\.h|dxgi[A-Za-z0-9_]*\.h|winsock2\.h|ws2tcpip\.h|xaudio2[A-Za-z0-9_]*\.h|winrt/.*|wrl/.*)$",
    re.I,
)
INCLUDE_LINE_RE = re.compile(r"^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]", re.M)

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
# The SDK's macros spelled like ordinary words, with the header that defines each. <windows.h> is in
# scope on the whole Client side and in every test suite, and the preprocessor rewrites these names
# before the compiler sees them: `near` and `far` expand to nothing, so `const XMVECTOR near = ...`
# lost its name, once (2026-09-17). A portable layer only finds out when a test includes it.
SDK_MACRO_NAMES = {
    "near": "minwindef.h",
    "far": "minwindef.h",
    "pascal": "minwindef.h",
    "cdecl": "minwindef.h",
    "NEAR": "minwindef.h",
    "FAR": "minwindef.h",
    "PASCAL": "minwindef.h",
    "CDECL": "minwindef.h",
    "CONST": "minwindef.h",
    "IN": "minwindef.h",
    "OUT": "minwindef.h",
    "OPTIONAL": "minwindef.h",
    "VOID": "winnt.h",
    "DELETE": "winnt.h",
    "IGNORE": "winbase.h",
    "interface": "combaseapi.h",
    "PURE": "combaseapi.h",
    "small": "rpcndr.h",
    "hyper": "rpcndr.h",
}

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
PLATFORM_CONDITION_RE = re.compile(r"^\s*'\$\(Platform\)'\s*==\s*'([^']*)'\s*$")
KNOWN = ", ".join(SLICES)


def configurations_of(condition: str | None, project: Project, where: str) -> list[str]:
    """The SLICES a group applies to - "<Configuration>|<Platform>" - or every one when it has no
    condition. A group conditioned on the configuration alone applies to that configuration on both
    platforms, and one conditioned on the platform alone to both configurations of that platform."""
    if condition is None:
        return list(SLICES)
    platform_only = PLATFORM_CONDITION_RE.match(condition)
    if platform_only:
        platform = platform_only.group(1)
        if platform not in PLATFORMS:
            project.findings.append(Finding("platform", project.relative, f"{where}: condition names platform {platform}; only {', '.join(PLATFORMS)} exist"))
            return []
        return [f"{configuration}|{platform}" for configuration in CONFIGURATIONS]
    match = CONDITION_RE.match(condition)
    if not match:
        project.findings.append(Finding("platform", project.relative, f"{where}: condition not understood: {condition.strip()}"))
        return []
    configuration, platform = match.group(2), match.group(3)
    if configuration not in CONFIGURATIONS or (platform is not None and platform not in PLATFORMS):
        project.findings.append(
            Finding("platform", project.relative, f"{where}: condition names {configuration}|{platform or '*'}; only {KNOWN} exist")
        )
        return []
    if platform is not None:
        return [f"{configuration}|{platform}"]
    return [f"{configuration}|{each}" for each in PLATFORMS]


def resolve_metadata(previous: str | None, value: str, name: str) -> str:
    """`_DEBUG;%(PreprocessorDefinitions)` appends to what an earlier group set, as MSBuild does."""
    reference = f"%({name})"
    if reference in value and previous is not None:
        return value.replace(reference, previous)
    return value


def parse_project(root: Path, file: Path) -> Project:
    relative = file.relative_to(root).as_posix()
    project = Project(file.stem, file, relative, file.parent, {slice_: Configuration() for slice_ in SLICES}, [], [], [])
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
                    # A CONDITION ON THE ELEMENT IS REFUSED RATHER THAN MISREAD. This file models a
                    # condition on the GROUP and nothing finer, so a conditioned element would be
                    # attributed to every slice the group covers - and a setting that is really on
                    # one platform would be reported, or excused, on all four. Visual Studio wrote
                    # exactly one of these when it added the ARM64 configurations (2026-09-19).
                    if element.get("Condition") is not None:
                        project.findings.append(
                            Finding("platform", project.relative, f"ItemDefinitionGroup: {kind}.{name} carries its own Condition; put it on the group, which is what this check reads")
                        )
                        continue
                    for configuration in targets:
                        table = project.configurations[configuration].metadata.setdefault(kind, {})
                        table[name] = resolve_metadata(table.get(name), text_of(element), name)
    return project


def check_shape(project: Project) -> None:
    findings = project.findings
    expected_configurations = set(SLICES)
    actual_configurations = set(project.project_configurations)
    for extra in sorted(actual_configurations - expected_configurations):
        findings.append(Finding("platform", project.relative, f"configuration {extra}; only {KNOWN} exist"))
    for absent in sorted(expected_configurations - actual_configurations):
        findings.append(Finding("platform", project.relative, f"configuration {absent} is not declared"))

    def require(table_of, name: str, expected: str, configurations: tuple[str, ...] = SLICES) -> None:
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
        both = tuple(f"{configuration}|{platform}" for platform in PLATFORMS)
        for name, expected in CONFIGURATION_PROPERTIES[configuration].items():
            require(property_of, name, expected, both)
        for name, expected in CONFIGURATION_COMPILE[configuration].items():
            require(Configuration.compile, name, expected, both)
        for name, expected in CONFIGURATION_LINK[configuration].items():
            require(Configuration.link, name, expected, both)
    for platform in PLATFORMS:
        both = tuple(f"{configuration}|{platform}" for configuration in CONFIGURATIONS)
        for name, expected in PLATFORM_COMPILE[platform].items():
            require(Configuration.compile, name, expected, both)

    kind = project.configurations[SLICES[0]].properties.get("ConfigurationType")
    subtype = project.configurations[SLICES[0]].properties.get("ProjectSubType")
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

    # Alignment: outside the enumerated set, the two configurations read identically. WITHIN A
    # PLATFORM, because the comparison is about what Debug and Release differ in and the platforms
    # differ in their own right; comparing across them would report the instruction set as a
    # misalignment on every project.
    for platform in PLATFORMS:
        check_alignment(project, platform)


def check_alignment(project: Project, platform: str) -> None:
    findings = project.findings
    debug, release = project.configurations[f"Debug|{platform}"], project.configurations[f"Release|{platform}"]
    allowed_properties = set(CONFIGURATION_PROPERTIES["Debug"]) | set(CONFIGURATION_PROPERTIES["Release"])
    for name in sorted(set(debug.properties) | set(release.properties)):
        if name not in allowed_properties and debug.properties.get(name) != release.properties.get(name):
            findings.append(Finding("alignment", project.relative, f"{name} differs on {platform}: Debug '{debug.properties.get(name)}', Release '{release.properties.get(name)}'"))
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
                    Finding("alignment", project.relative, f"{kind_name}.{name} differs on {platform}: Debug '{debug_table.get(name)}', Release '{release_table.get(name)}'")
                )

    # Definitions: the configuration's own macro and nothing else may differ, and none of the family.
    definitions = {
        c: [d for d in (project.configurations[f"{c}|{platform}"].compile("PreprocessorDefinitions") or "").split(";") if d]
        for c in CONFIGURATIONS
    }
    for configuration in CONFIGURATIONS:
        own = CONFIGURATION_DEFINITION[configuration]
        if own not in definitions[configuration]:
            findings.append(Finding("setting", project.relative, f"{configuration}|{platform} does not define {own}"))
        other = CONFIGURATION_DEFINITION["Release" if configuration == "Debug" else "Debug"]
        if other in definitions[configuration]:
            findings.append(Finding("setting", project.relative, f"{configuration}|{platform} defines {other}"))
    stripped = {c: [d for d in definitions[c] if d not in CONFIGURATION_DEFINITION.values()] for c in CONFIGURATIONS}
    if stripped["Debug"] != stripped["Release"]:
        findings.append(Finding("alignment", project.relative, f"PreprocessorDefinitions differ beyond _DEBUG/NDEBUG on {platform}: Debug {stripped['Debug']}, Release {stripped['Release']}"))
    for macro in sorted({d for c in CONFIGURATIONS for d in definitions[c]} & MACRO_FAMILY):
        findings.append(Finding("macro-family", project.relative, f"defines {macro}; Core/WindowsHeader.h owns the family and a /D of it is C4005 under /WX"))


def check_include_directories(project: Project, project_names: set[str]) -> None:
    seen: set[str] = set()
    for configuration in SLICES:
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
    """R2, R11 and the SDK's macro names over one .cpp or .h."""
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
        if identifier in SDK_MACRO_NAMES:
            reported.add(identifier)
            project.findings.append(
                Finding("sdk-macro", relative, f"identifier '{identifier}' is a macro of the Windows SDK ({SDK_MACRO_NAMES[identifier]}); the preprocessor rewrites it wherever <windows.h> is in scope (AGENTS.md §3)", line_of(code, match.start()))
            )
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
        if path.suffix == ".h" and path.name.lower() in RUNTIME_HEADER_NAMES:
            findings.append(
                Finding("shadow", relative, f"named like the C runtime's or the SDK's <{path.name.lower()}>; this directory is on the include path ahead of the SDK, so an angled include of that name finds this file first (AGENTS.md §3)")
            )
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


def permitted_edges(name: str) -> set[str] | None:
    """What ADR-001 builds the project on, or None when the table has no row for it."""
    if name in BUILT_ON:
        return BUILT_ON[name]
    if name.endswith("Tests") and name[: -len("Tests")] in BUILT_ON:
        library = name[: -len("Tests")]
        return {library} | BUILT_ON[library]
    return None


def check_layering(root: Path, projects: list[Project]) -> None:
    """The edges of ADR-001 (BUILT_ON) over references, include directories and quoted includes; the platform rule."""
    by_name = {project.name: project for project in projects}
    directories = {project.name: project.directory.resolve() for project in projects}
    owners: dict[str, list[str]] = {}
    for project in projects:
        for path in sorted(project.directory.iterdir()):
            if path.is_file() and path.suffix == ".h":
                owners.setdefault(path.name, []).append(project.name)

    for project in projects:
        permitted = permitted_edges(project.name)
        if permitted is None:
            project.findings.append(Finding("layering-unknown", project.relative, "not in the ADR-001 table (BUILT_ON in Build/CheckProjectFiles.py); an edge is a decision, so the row comes with the ADR"))
            continue
        edge = lambda other: f"the edge it would need is {project.name} -> {other}, and ADR-001 builds {project.name} on {{{', '.join(sorted(permitted)) or 'nothing'}}}"  # noqa: E731

        for item in project.items:
            if item.kind == "ProjectReference":
                other = Path(item.include.replace("\\", "/")).stem
                if other not in permitted:
                    project.findings.append(Finding("edge-reference", project.relative, f"references {other}; {edge(other)}"))
        seen: set[str] = set()
        for configuration in SLICES:
            for entry in (project.configurations[configuration].compile("AdditionalIncludeDirectories") or "").split(";"):
                entry = entry.strip()
                if entry.startswith("$(SolutionDir)") and entry not in seen:
                    seen.add(entry)
                    other = entry[len("$(SolutionDir)"):]
                    if other in by_name and other != project.name and other not in permitted:
                        project.findings.append(Finding("edge-directory", project.relative, f"include directory '{entry}'; {edge(other)}"))

        for path in sorted(project.directory.iterdir()):
            if not path.is_file() or path.suffix not in CPP_SUFFIXES or path.relative_to(root).as_posix() in VENDORED:
                continue
            relative = path.relative_to(root).as_posix()
            text = path.read_text(encoding="utf-8-sig", errors="replace")
            for match in INCLUDE_LINE_RE.finditer(text):
                include = match.group(1).replace("\\", "/")
                line = line_of(text, match.start())
                if project.name in PORTABLE and PLATFORM_HEADER_RE.match(include):
                    project.findings.append(Finding("platform-header", relative, f"includes {include}; {project.name} is portable and includes no platform header (ADR-001)", line))
                if match.group(0).rstrip().endswith(">"):
                    continue  # an angled include is the SDK's or the standard library's
                if "/" in include:
                    resolved = (path.parent / include).resolve()
                    owner = next((name for name, directory in directories.items() if resolved.parent == directory), None)
                    if owner is not None and owner != project.name and owner not in permitted:
                        project.findings.append(Finding("edge-include", relative, f"includes {include}, which is {owner}'s; {edge(owner)}", line))
                    continue
                if (project.directory / include).exists():
                    continue  # its own, which the compiler finds first
                candidates = owners.get(include, [])
                if candidates and not any(candidate in permitted for candidate in candidates):
                    owner = candidates[0]
                    project.findings.append(Finding("edge-include", relative, f"includes {include}, which is {owner}'s; {edge(owner)}", line))


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
    check_layering(root, projects)
    for project in projects:
        findings.extend(project.findings)
    findings.extend(check_tidy_regex(root, project_names))
    return findings, len(projects)


# What the fixtures under Build/Fixtures/ProjectFiles must produce, and nothing else: every rule, once.
SELF_TEST_EXPECTED = [
    ("solution-missing", "Fixture.slnx"),
    ("solution-unlisted", "Orphan/Orphan.vcxproj"),
    ("solution-directory", "Elsewhere/Moved.vcxproj"),
    ("platform", "Content/Content.vcxproj"),
    ("setting", "Content/Content.vcxproj"),
    ("alignment", "Content/Content.vcxproj"),
    ("include-directory", "Content/Content.vcxproj"),
    ("macro-family", "Content/Content.vcxproj"),
    ("unregistered", "Net/Stray.cpp"),
    ("missing", "Net/Net.vcxproj"),
    ("filters", "Net/Net.vcxproj.filters"),
    ("subdirectory", "Net/Extra/Deep.h"),
    ("compiled-shaders", "Net/Net.vcxproj"),
    ("file-name", "Replica/bad_name.cpp"),
    ("shadow", "Replica/Math.h"),
    ("type-affix", "Replica/Replica.h"),
    ("spelling", "Replica/Replica.h"),
    ("sdk-macro", "Replica/Replica.h"),
    ("tidy-regex", ".clang-tidy"),
    ("suite-empty", "Tests/CoreTests/CoreTests.vcxproj"),
    ("suite-stale", "Tests/ContentTests/ContentTests.vcxproj"),
    ("layering-unknown", "Elsewhere/Moved.vcxproj"),
    ("edge-reference", "Sim/Sim.vcxproj"),
    ("edge-directory", "Sim/Sim.vcxproj"),
    ("edge-include", "Sim/Sim.cpp"),
    ("edge-include", "Client/Client.cpp"),
    ("platform-header", "Sim/Sim.cpp"),
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
