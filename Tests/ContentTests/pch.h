#pragma once

// The layer under test, then the framework. "pch.h" resolves to this file rather than Content/pch.h
// because the compiler searches the including file's own directory first (AGENTS.md §3). The
// framework header pulls in <windows.h>, so the macro family goes first through the one header
// that owns it (AGENTS.md §4); that include is the test's, never Content's.
#include "../../Content/pch.h"

#include "WindowsHeader.h"

#include <CppUnitTest.h>
