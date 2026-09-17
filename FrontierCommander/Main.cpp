#include "pch.h"

#include "WindowsHeader.h"

#include <shellapi.h>

#include "App.h"

#include <string>
#include <vector>

// The game's entry point: the command line into LaunchOptions, then App::Run, whose exit code is
// the process's. A Windows-subsystem executable has no console, so a bad command line is reported
// through the exit code and the debugger output alone.
int WINAPI wWinMain(HINSTANCE /*_instance*/, HINSTANCE /*_previousInstance*/, PWSTR /*_commandLine*/, int /*_showCommand*/)
{
  int count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (arguments == nullptr)
  {
    return Frontier::EXIT_FAILED;
  }
  const std::vector<std::wstring> copied(arguments, arguments + count);
  LocalFree(static_cast<HLOCAL>(arguments));
  Frontier::LaunchOptions options;
  if (!Frontier::ParseCommandLine(copied, options))
  {
    OutputDebugStringW(L"usage: FrontierCommander [--warp] [--capture <frames> <directory>]\n");
    return Frontier::EXIT_FAILED;
  }
  Frontier::App app(options);
  return app.Run();
}
