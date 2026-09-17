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

struct Replica
{
  std::uint32_t m_colour; // R11: the other half of the family; a comment may say colour, an identifier may not.
  std::uint32_t near;     // The SDK's macro: wherever <windows.h> is in scope this member has no name.
};

} // namespace Fixture
