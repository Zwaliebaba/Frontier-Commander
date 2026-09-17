#pragma once

// The one header that owns the Windows macro family (AGENTS.md §4). Include this, never
// <windows.h>, and define none of these anywhere else: two owners of one macro is C4005, and /WX
// makes that fatal. The project files deliberately define none of them either, and
// Build/CheckProjectFiles.py checks that they do not.
//
// NOGDI means GDI is genuinely gone: the swap chain owns every pixel, and there is no case in
// this game where a GDI call is the right answer.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP

#include <windows.h>
