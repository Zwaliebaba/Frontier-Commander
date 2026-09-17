#pragma once

// The precompiled header of Core: the standard library and nothing else. A Core file that needs
// Windows includes WindowsHeader.h itself, so that the translation units that do not -- the
// arithmetic, the containers, the readers -- compile against nothing but the C++ standard
// (ADR-001). Include order is load-bearing (AGENTS.md §4): pch.h first, then WindowsHeader.h
// where needed, then project headers, then the standard library.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
