# The self-test fixtures of `Build/CheckProjectFiles.py`

`python3 Build/CheckProjectFiles.py --self-test` runs the checker over this directory as if it
were a repository root and expects every rule to fire exactly once, where `SELF_TEST_EXPECTED`
in the script says. Nothing here is built: CI's test discovery skips `Build/Fixtures`, and no
real solution lists these projects. Every project file is in the ADR-001 shape except for the
one thing its directory exists to break, so that a rule firing anywhere else is a defect in the
checker rather than in the fixture.

| Fixture | What it breaks | Rule |
|---|---|---|
| `Clean/` | Nothing. A library in the ADR-001 shape, on which no rule may fire. | (none) |
| `Fixture.slnx` | Lists `Phantom/Phantom.vcxproj`, which does not exist. | `solution-missing` |
| `Orphan/` | A `.vcxproj` on disk that the solution does not list. Nothing else is read from it. | `solution-unlisted` |
| `Elsewhere/Moved.vcxproj` | A clean project in a directory not named for it. | `solution-directory` |
| `Shape/` | A third `ProjectConfiguration`, `Debug\|Win32`. | `platform` |
| | `LanguageStandard` is `stdcpp17` in the unconditional group. | `setting` |
| | The Release group adds an include directory Debug does not have. | `alignment` |
| | `..\Elsewhere` on the include path, which is not `$(SolutionDir)<AnotherProject>`. | `include-directory` |
| | `NOMINMAX` among the preprocessor definitions. | `macro-family` |
| `Registry/` | `Stray.cpp` on disk and not in the `.vcxproj`. | `unregistered` |
| | `Ghost.h` in the `.vcxproj` and not on disk. | `missing` |
| | `Registry.cpp` in the `.vcxproj` and not in the `.filters`. | `filters` |
| | `Extra/Deep.h`, C++ in a subdirectory. | `subdirectory` |
| | `CompiledShaders\ShapeVS.h` listed as a header. | `compiled-shaders` |
| `Names/` | `bad_name.cpp`, not PascalCase. | `file-name` |
| | `class IThing` in `Names.h`. | `type-affix` |
| | `m_colour` in `Names.h`. | `spelling` |
| `.clang-tidy` | The `HeaderFilterRegex` alternation omits `Names`. | `tidy-regex` |
| `Tests/EmptyTests/` | A suite with no `TEST_METHOD` and no `SuiteSmoke.cpp`. | `suite-empty` |
| `Tests/StaleTests/` | `SuiteSmoke.cpp` beside a file with a real `TEST_METHOD`. | `suite-stale` |

Adding a rule means three edits in one change: the rule in the script, its line in the script's
docstring, and a fixture here with its row in this table and its entry in `SELF_TEST_EXPECTED`.
The self-test fails when any of the three is missing.
