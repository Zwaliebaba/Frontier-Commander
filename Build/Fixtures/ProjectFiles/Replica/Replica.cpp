#include "pch.h"

#include "Replica.h"

namespace Fixture
{

std::uint32_t ReplicaValue(const Replica& _item)
{
  return static_cast<std::uint32_t>(sizeof _item);
}

} // namespace Fixture
