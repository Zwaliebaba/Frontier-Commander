#include "pch.h"

#include "WindowsHeader.h"

#include "Assert.h"

#include <cstdio>

namespace Neuron
{

void ReportAssertionFailure(const char* _expression, const char* _file, int _line) noexcept
{
  char message[1024];
  std::snprintf(message, sizeof message, "%s(%d): assertion failed: %s\n", _file, _line, _expression);
  std::fputs(message, stderr);
  ::OutputDebugStringA(message);
}

} // namespace Neuron
