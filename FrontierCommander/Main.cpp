#include "pch.h"

#include "WindowsHeader.h"

// The game's entry point. Nothing yet: the window, the device and the loop arrive with
// m0-foundation/T18 and T20. A Windows-subsystem executable has no console, so there is nothing
// to print and nothing to do but return.
int WINAPI wWinMain(HINSTANCE /*_instance*/, HINSTANCE /*_previousInstance*/, PWSTR /*_commandLine*/, int /*_showCommand*/)
{
  return 0;
}
