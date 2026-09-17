#include "pch.h"

#include "WindowsHeader.h" // platform-header: Sim stays portable.

#include "Sim.h"
#include "Net.h" // edge-include, upward: Net is built on Sim, not the reverse.

namespace Fixture
{

std::uint32_t SimValue(const Sim& _item)
{
  return _item.value;
}

} // namespace Fixture
