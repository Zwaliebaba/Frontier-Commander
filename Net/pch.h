#pragma once

// The precompiled header of Net: the standard library and Core, and never a platform header
// (ADR-001). Net includes no Windows, Direct3D, socket or WinRT header anywhere; the
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
