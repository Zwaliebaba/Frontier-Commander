#include "pch.h"

#include "Client.h"
#include "Replica.h" // edge-include, sideways: the two libraries share what is below them, never each other.

namespace Fixture
{

std::uint32_t ClientValue(const Client& _item)
{
  return _item.value;
}

} // namespace Fixture
