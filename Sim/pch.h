#pragma once

// The precompiled header of Sim: the standard library and Core, and never a platform header
// (ADR-001). Sim includes no Windows, Direct3D, socket or WinRT header anywhere; the
// layering check of Build/CheckProjectFiles.py refuses one.
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

#include "Assertion.h"
