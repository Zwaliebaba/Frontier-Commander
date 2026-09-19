# ADR-001 — The solution and project layout

**Status:** Accepted
**Date:** 2026-09-17
**Owner:** the author, on the layout `Design/TechnicalDesign.md` §2 decided with the owner (2026-09-17)

## Context

`AGENTS.md` §2 leaves the concrete layout — the solution, the projects and the edges between them — to be settled when the first project is created, and to be recorded there and in an ADR at that point. `TechnicalDesign.md` §2 had already decided the eight projects, their kinds, their edges and the two namespaces with the owner. This ADR fixes what that section leaves to the first project: the file layout, the settings every project carries, the test-project shape, and three placements the implementation plan needed (`ImplementationPlan.md` §6). It is written with the first project, `Core`, and its suite; `m0-foundation/T2` adds the other seven to the same pattern.

## Decision

**The solution** is `FrontierCommander.slnx` at the repository root, in the XML solution format, with two platforms: `x64` and `ARM64`. CI finds it by extension, so there is exactly one.

**ARM64 was added on 2026-09-19** (owner). The machine the game is developed and played on is a Snapdragon X, which had been running the x64 build under Prism emulation — `m0-foundation/T22` measured it there. A native build is the point of a second platform, and a configuration nothing builds is a configuration that rots, so `.github/workflows/build.yml` carries an ARM64 leg that compiles the tree and runs every suite on a `windows-11-arm` runner. That leg builds and tests and no more: clang-tidy reads the same sources whatever the target, the content tables are bytes, and the capture is WARP's, so only the x64 job runs them. What the ARM64 leg alone can answer is whether the tree compiles and its tests pass on the other architecture, which is where a portability fault in the fixed-point arithmetic or in a record's layout would surface.

**The projects**, each in a flat directory of its own name at the root (`AGENTS.md` §2), with `Tests/<Name>Tests` for every static library:

| Project | Kind | Namespace | Built on (the only permitted `ProjectReference`s and quoted includes) |
|---|---|---|---|
| `Core` | static library | `Neuron` | nothing |
| `Content` | static library | `Frontier` | `Core` |
| `Sim` | static library | `Frontier` | `Core`, `Content` |
| `Net` | static library | `Frontier` | `Core`, `Content`, `Sim` |
| `Replica` | static library | `Frontier` | `Core`, `Content`, `Net`, `Sim` |
| `Client` | static library | `Neuron` | `Core`, `Content` |
| `FrontierCommander` | executable, Windows subsystem | `Frontier` | all six libraries |
| `FrontierHost` | executable, console | `Frontier` | `Core`, `Content`, `Sim`, `Net` |
| `Tests/<Name>Tests` | DLL on the Microsoft Native Unit Test Framework | `<Name>Tests` | `<Name>` and what it is built on |

Edges point downward only; a library never references one beside it (`Client` and `Replica` share `Core` and `Content`, never each other). `Build/CheckProjectFiles.py` holds the tree to this table (`m0-foundation/T6`).

**Platform headers.** `Sim`, `Content`, `Net` and `Replica` include no Windows, Direct3D, socket, WinRT or XAudio2 header, and their `pch.h` includes the standard library and `Core` only. `Core` may include platform headers in named files (the transport, the paths, the assertion reporter), through `Core/WindowsHeader.h`, which is the one header that defines the Windows macro family (`AGENTS.md` §4) before `<windows.h>`. `Client` and the executables may include platform headers, through that same header.

**Three placements** the plan needed and `TechnicalDesign.md` §2 did not say:

- The render-view and height-view aggregates live in `Core`, in the engine namespace, so that `Replica` produces them and `Client` consumes them without an edge between the two (`TechnicalDesign.md` §6.3).
- `LandscapeDefinition` is a `Content` aggregate from M0, with its loader arriving in M1, because `Sim` reads `Content` and a type that started in `Sim` would have to move.
- `FRONTIER_ASSERT` and `FRONTIER_VERIFY` live in `Core/Assertion.h`, reporting through `Core/Assertion.cpp`, so that every project above `Core` asserts the same way. (Named `Assert.h` until 2026-09-17, when DirectXMath's `<assert.h>` found it first: a project directory sits on the include path ahead of the SDK, and MSVC matches the name case-insensitively, so no header is named like a C runtime or SDK header, which `Build/CheckProjectFiles.py` refuses since that day.)

**Every project file is written by hand** from the MSBuild schema, not by a wizard, in one shape: the settings that are not about optimisation sit in unconditional property and item-definition groups, so they are identical in Debug and Release by construction, and the conditional groups hold only what `AGENTS.md` §3 enumerates. The settings, stated explicitly in every project (`AGENTS.md` R16 says a default is not a decision):

| Setting | Value |
|---|---|
| `PlatformToolset` | `v145` |
| `LanguageStandard` | `stdcpplatest` |
| `ConformanceMode` | `true` |
| `WarningLevel`, `TreatWarningAsError` | `Level4`, `true` |
| `FloatingPointModel` | `Precise` |
| `EnableEnhancedInstructionSet` | **per platform**: `AdvancedVectorExtensions2` on `x64`, `CPUExtensionRequirementsARMv87` on `ARM64` |
| `ExceptionHandling` | `Sync` |
| `CharacterSet` | `Unicode` |
| `SDLCheck`, `MultiProcessorCompilation` | `true`, `true` |
| Precompiled header | `Use` of `pch.h`, created by `pch.cpp` |
| `OutDir` | `$(SolutionDir)$(Platform)\$(Configuration)\` |
| `IntDir` | `$(SolutionDir)$(Platform)\$(Configuration)\Intermediate\$(ProjectName)\` |
| `PreferredToolArchitecture` | `x64` |
| `WindowsTargetPlatformVersion` | `10.0` (the newest installed SDK) |
| Debug only | `Optimization` `Disabled`, `RuntimeLibrary` `MultiThreadedDebugDLL`, `_DEBUG`, `UseDebugLibraries`, `LinkIncremental` |
| Release only | `Optimization` `MaxSpeed`, `FunctionLevelLinking`, `IntrinsicFunctions`, `RuntimeLibrary` `MultiThreadedDLL`, `NDEBUG`, `WholeProgramOptimization`, `EnableCOMDATFolding`, `OptimizeReferences` |

**The instruction set is the one setting that belongs to the platform**, and it sits in an item-definition group conditioned on `$(Platform)` rather than in the unconditional one. This is not decoration: when Visual Studio first wrote the ARM64 configurations it put `CPUExtensionRequirementsARMv87` in the group both platforms read, which silently took AVX2 off the x64 build that CI gates on, and `Build/CheckProjectFiles.py` is what caught it. A condition on the ELEMENT rather than on the group is refused for the same reason — the checker models a condition on the group and nothing finer, so a conditioned element would be attributed to every slice of the group and a setting really present on one platform would be reported, or excused, on all four.

No project defines any of the Windows macro family. Include directories name other projects only, as `$(SolutionDir)<Project>`; a project's own directory is never listed (`AGENTS.md` §3).

**A test project** is a `DynamicLibrary` with `ProjectSubType` `NativeUnitTestProject`, which is what makes `Microsoft.Cpp.UnitTest.props` put `CppUnitTest.h` and the framework's import library on the paths and marks the DLL as a test container. The wizard's `$(VCInstallDir)UnitTest\include` is not used: it does not exist in Visual Studio 2026. Its `pch.h` includes the library's `pch.h`, then `WindowsHeader.h`, then `<CppUnitTest.h>`, because the framework pulls in `<windows.h>` and the macro family has to be set first. Every suite carries `SuiteSmoke.cpp` until its first real test (`AGENTS.md` §3).

## Consequences

- Adding a project means: a directory, a `.vcxproj` and `.filters` in this shape, a row in the table above, an entry in `.clang-tidy`'s `HeaderFilterRegex`, and an entry in the solution — and `m0-foundation/T4` fails the build when one of those is missing.
- An edge not in the table cannot be added by a `ProjectReference` or an include without changing this ADR by a superseding one; "for now" edges are what the layering check exists to refuse.
- The XML solution format needs MSBuild 17.12 or newer, which the pinned toolset's Visual Studio carries; an older MSBuild cannot build the tree, which is accepted.
- Setting every option explicitly makes the project files longer than a wizard's; that is the price of a Release that cannot drift from Debug unnoticed.

## Measurements

None: nothing here is a figure. What this ADR asserts about the build — that the projects build under these settings, that the suite runs — is asserted by CI on the commit that lands it, and that run is the measurement.
