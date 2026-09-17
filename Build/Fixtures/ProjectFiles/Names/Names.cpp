#include "pch.h"

#include "Names.h"

namespace Fixture
{

std::uint32_t NamesValue(const Names& _item)
{
  return static_cast<std::uint32_t>(sizeof _item);
}

} // namespace Fixture
