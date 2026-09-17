# The self-test fixtures of `Build/CheckProjectFiles.py`

`python3 Build/CheckProjectFiles.py --self-test` runs the checker over this directory as if it
were a repository root and expects every rule to fire exactly once, where `SELF_TEST_EXPECTED`
in the script says. Nothing here is built: CI's test discovery skips `Build/Fixtures`, and no
real solution lists these projects. The projects carry the real projects' names because the
layering rules read ADR-001's table (`BUILT_ON` in the script) by name, and every project file
is in the ADR-001 shape except for the one thing its directory exists to break, so that a rule
firing anywhere else is a defect in the checker rather than in the fixture.

| Fixture | What it breaks | Rule |
|---|---|---|
| `Core/` | Nothing. A library in the ADR-001 shape, on which no rule may fire. | (none) |
| `Fixture.slnx` | Lists `FrontierHost/FrontierHost.vcxproj`, which does not exist. | `solution-missing` |
| `Orphan/` | A `.vcxproj` on disk that the solution does not list. Nothing else is read from it. | `solution-unlisted` |
| `Elsewhere/Moved.vcxproj` | A clean project in a directory not named for it. | `solution-directory` |
| | A name the ADR-001 table has no row for. | `layering-unknown` |
| `Content/` | A third `ProjectConfiguration`, `Debug\|Win32`. | `platform` |
| | `LanguageStandard` is `stdcpp17` in the unconditional group. | `setting` |
| | The Release group sets `DiagnosticsFormat`, which Debug does not. | `alignment` |
| | `..\Elsewhere` on the include path, which is not `$(SolutionDir)<AnotherProject>`. | `include-directory` |
| | `NOMINMAX` among the preprocessor definitions. | `macro-family` |
| `Net/` | `Stray.cpp` on disk and not in the `.vcxproj`. | `unregistered` |
| | `Ghost.h` in the `.vcxproj` and not on disk. | `missing` |
| | `Net.cpp` in the `.vcxproj` and not in the `.filters`. | `filters` |
| | `Extra/Deep.h`, C++ in a subdirectory. | `subdirectory` |
| | `CompiledShaders\ShapeVS.h` listed as a header. | `compiled-shaders` |
| `Replica/` | `bad_name.cpp`, not PascalCase. | `file-name` |
| | `class IThing` in `Replica.h`. | `type-affix` |
| | `m_colour` in `Replica.h`. | `spelling` |
| `Sim/` | A `ProjectReference` to `Net`, which is built on `Sim` and not the reverse. | `edge-reference` |
| | `$(SolutionDir)Net` on the include path. | `edge-directory` |
| | `#include "Net.h"` in `Sim.cpp`, the upward include. | `edge-include` |
| | `#include "WindowsHeader.h"` in `Sim.cpp`; `Sim` stays portable. | `platform-header` |
| `Client/` | `#include "Replica.h"` in `Client.cpp`, the sideways include: the two share `Core` and `Content`, never each other. | `edge-include` |
| `.clang-tidy` | The `HeaderFilterRegex` alternation omits `Replica`. | `tidy-regex` |
| `Tests/CoreTests/` | A suite with no `TEST_METHOD` and no `SuiteSmoke.cpp`. | `suite-empty` |
| `Tests/ContentTests/` | `SuiteSmoke.cpp` beside a file with a real `TEST_METHOD`. | `suite-stale` |

Adding a rule means three edits in one change: the rule in the script, its line in the script's
docstring, and a fixture here with its row in this table and its entry in `SELF_TEST_EXPECTED`.
The self-test fails when any of the three is missing.
