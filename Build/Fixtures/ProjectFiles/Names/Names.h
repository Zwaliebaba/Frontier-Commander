#pragma once

#include <cstdint>

namespace Fixture
{

// R2: the I prefix.
class IThing
{
public:
  virtual ~IThing() = default;
};

struct Names
{
  std::uint32_t m_colour; // R11: the other half of the family; a comment may say colour, an identifier may not.
};

} // namespace Fixture
