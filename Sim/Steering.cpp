#include "pch.h"

#include "Steering.h"

#include "Assertion.h"
#include "BinaryAngle.h"

#include <algorithm>
#include <vector>

namespace Frontier
{

namespace
{

/// How many directions a pile of exactly coincident devices spreads along.
constexpr std::uint32_t TIE_BREAK_DIRECTIONS = 8;

/// Enough to keep the key of a cell positive for a device that has been pushed off the landscape;
/// the largest landscape is 1,024 cells a side.
constexpr std::int32_t BUCKET_BIAS = 1 << 12;

[[nodiscard]] constexpr std::int32_t CellOf(std::int32_t _subunits) noexcept
{
  return _subunits >> Neuron::SUBUNITS_PER_CELL_SHIFT; // Arithmetic, so a position left of nought floors
}

[[nodiscard]] constexpr std::uint64_t BucketKey(std::int32_t _cellX, std::int32_t _cellZ) noexcept
{
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(_cellZ + BUCKET_BIAS)) << 32) |
         static_cast<std::uint32_t>(_cellX + BUCKET_BIAS);
}

/// One device in its bucket. Sorted by the key and then by the index, so that the order the pass
/// walks a bucket in is the order the devices were given in and not the order a sort happened to
/// leave them in (AGENTS.md R16).
struct Bucketed
{
  std::uint64_t key;
  std::uint32_t index;
};

} // namespace

void Separate(std::span<const SteeredDevice> _devices, std::span<SeparationPush> _pushes)
{
  FRONTIER_ASSERT(_pushes.size() == _devices.size());
  for (SeparationPush& push : _pushes)
  {
    push = {0, 0};
  }
  if (_devices.size() < 2 || _pushes.size() != _devices.size())
  {
    return;
  }

  std::vector<Bucketed> sorted;
  sorted.reserve(_devices.size());
  for (std::size_t index = 0; index < _devices.size(); ++index)
  {
    sorted.push_back({BucketKey(CellOf(_devices[index].x), CellOf(_devices[index].z)), static_cast<std::uint32_t>(index)});
  }
  std::sort(sorted.begin(), sorted.end(), [](const Bucketed& _left, const Bucketed& _right)
            { return _left.key != _right.key ? _left.key < _right.key : _left.index < _right.index; });

  constexpr std::int64_t RADIUS = SEPARATION_RADIUS_SUBUNITS;
  constexpr std::int64_t STEP = SEPARATION_STEP_SUBUNITS;

  for (std::size_t index = 0; index < _devices.size(); ++index)
  {
    const SteeredDevice& me = _devices[index];
    std::int64_t pushX = 0;
    std::int64_t pushZ = 0;

    for (std::int32_t cellZ = CellOf(me.z) - 1; cellZ <= CellOf(me.z) + 1; ++cellZ)
    {
      for (std::int32_t cellX = CellOf(me.x) - 1; cellX <= CellOf(me.x) + 1; ++cellX)
      {
        const std::uint64_t key = BucketKey(cellX, cellZ);
        const auto first =
          std::lower_bound(sorted.begin(), sorted.end(), key, [](const Bucketed& _entry, std::uint64_t _key) { return _entry.key < _key; });
        const auto last =
          std::upper_bound(sorted.begin(), sorted.end(), key, [](std::uint64_t _key, const Bucketed& _entry) { return _key < _entry.key; });
        for (auto entry = first; entry != last; ++entry)
        {
          if (entry->index == index)
          {
            continue;
          }
          const SteeredDevice& other = _devices[entry->index];
          const std::int32_t awayX = me.x - other.x;
          const std::int32_t awayZ = me.z - other.z;
          if (Neuron::LengthSquared(awayX, awayZ) >= RADIUS * RADIUS)
          {
            continue;
          }
          if (awayX == 0 && awayZ == 0)
          {
            // Exactly on top of one another, so there is no direction to move apart along: the
            // higher id steps and the lower stands, in one of eight directions taken from the id.
            if (me.id.value <= other.id.value)
            {
              continue;
            }
            const auto angle =
              static_cast<Neuron::BinaryAngle>((me.id.value % TIE_BREAK_DIRECTIONS) * (Neuron::FULL_TURN / TIE_BREAK_DIRECTIONS));
            pushX += Neuron::MulShift(SEPARATION_STEP_SUBUNITS, Neuron::Sin(angle), 16);
            pushZ += Neuron::MulShift(SEPARATION_STEP_SUBUNITS, Neuron::Cos(angle), 16);
            continue;
          }
          // Away from the neighbour and a quarter turn round it, which is (x, z) -> (x - z, z + x).
          // Both terms are bounded by the radius, so neither the turn nor the sum can overflow.
          const std::int32_t steerX = awayX - awayZ;
          const std::int32_t steerZ = awayZ + awayX;
          const auto distance = static_cast<std::int64_t>(Neuron::Length(awayX, awayZ));
          const auto steerLength = static_cast<std::int32_t>(Neuron::Length(steerX, steerZ));
          if (steerLength == 0)
          {
            continue;
          }
          // Full strength when they touch, nothing at the radius, so a device at the edge of the
          // radius is not jerked and one that is half inside another is pushed hard.
          const auto magnitude = static_cast<std::int32_t>(STEP * (RADIUS - distance) / RADIUS);
          pushX += Neuron::MulDiv(steerX, magnitude, steerLength);
          pushZ += Neuron::MulDiv(steerZ, magnitude, steerLength);
        }
      }
    }

    // However many neighbours crowd it, one tick moves a device by at most one step.
    const std::int64_t lengthSquared = pushX * pushX + pushZ * pushZ;
    if (lengthSquared > STEP * STEP)
    {
      const auto length = static_cast<std::int64_t>(Neuron::Sqrt(static_cast<std::uint64_t>(lengthSquared)));
      pushX = pushX * STEP / length;
      pushZ = pushZ * STEP / length;
    }
    _pushes[index] = {static_cast<std::int32_t>(pushX), static_cast<std::int32_t>(pushZ)};
  }
}

} // namespace Frontier
