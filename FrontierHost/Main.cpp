#include "pch.h"

#include <cstdio>
#include <cstring>

namespace
{

constexpr const char* VERSION_LINE = "FrontierHost 0.0 (m0-foundation)";

} // namespace

// The headless host's entry point. --version prints one line and returns 0 so that a script can
// tell the executable runs; hosting a match arrives with m1-vertical-slice/N2 and M3, and
// --validate with m1-vertical-slice/C1.
int main(int _argumentCount, char** _arguments)
{
  for (int index = 1; index < _argumentCount; ++index)
  {
    if (std::strcmp(_arguments[index], "--version") == 0)
    {
      std::puts(VERSION_LINE);
      return 0;
    }
  }
  std::puts("usage: FrontierHost --version");
  return 2;
}
