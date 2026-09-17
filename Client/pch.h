#pragma once

// The precompiled header of Client: the standard library. A Client file that needs Windows
// includes Core's WindowsHeader.h itself, the one owner of the Windows macro family
// (AGENTS.md §4; ADR-001).
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

#include "Assert.h"
