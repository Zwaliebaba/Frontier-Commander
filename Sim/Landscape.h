#pragma once

#include "HeightDelta.h"
#include "StateHash.h"

#include "LandscapeDefinition.h"

#include <cstdint>
#include <span>
#include <vector>

// The heightfield as simulation state (TechnicalDesign.md §4.4): int16 samples every 16 world
// units generated from the definition, the per-cell grids pathing, visibility and placement read,
// and the list of height deltas applied over the base. A snapshot carries the definition and the
// deltas rather than the samples, and the hash covers the same, because they determine every
// sample and hashing 33 MB of Frontier samples a tick would not fit the budget.

namespace Frontier
{

class Landscape
{
public:
  /// What a cell holds, derived from its 5x5 samples: the steepest step between two adjacent
  /// samples as a percentage of the spacing (the tool's slope percent), a water bit in the flags,
  /// and an obstruction byte the structures of M1 write.
  struct Cell
  {
    std::uint16_t slopePercent;
    std::uint8_t flags;
    std::uint8_t obstruction;
    /// The tallest of the cell's 5x5 samples, in whole world units. It is what the line of sight
    /// of S9 measures against, so that a one-cell ridge blocks rather than being stepped over, and
    /// it is cached here rather than scanned per query: a visibility refresh asks this question
    /// about twenty thousand cells per viewer, and scanning 25 samples each time cost 140 ms a
    /// tick against a 50 ms budget when it was measured (m1-vertical-slice/S9).
    std::int16_t highestSample;

    [[nodiscard]] constexpr bool operator==(const Cell&) const noexcept = default;
  };

  static constexpr std::uint8_t CELL_WATER = 0x1; ///< The lowest of the cell's samples is below sea level

  /// Generates the base from the definition and derives every cell; false, with nothing changed,
  /// for a definition the generator refuses.
  [[nodiscard]] bool Create(const LandscapeDefinition& _definition);

  /// Create, then the deltas in order: what a snapshot holds.
  [[nodiscard]] bool Restore(const LandscapeDefinition& _definition, const std::vector<HeightDelta>& _deltas);

  [[nodiscard]] bool Created() const noexcept
  {
    return !m_heights.empty();
  }

  [[nodiscard]] const LandscapeDefinition& Definition() const noexcept
  {
    return m_definition;
  }

  [[nodiscard]] std::uint32_t CellsPerSide() const noexcept
  {
    return m_cellsPerSide;
  }

  [[nodiscard]] std::uint32_t SamplesPerSide() const noexcept
  {
    return m_samplesPerSide;
  }

  [[nodiscard]] std::span<const std::int16_t> Heights() const noexcept
  {
    return m_heights;
  }

  [[nodiscard]] std::int16_t HeightAt(std::uint32_t _sampleX, std::uint32_t _sampleY) const noexcept;

  [[nodiscard]] std::span<const Cell> Cells() const noexcept
  {
    return m_cells;
  }

  [[nodiscard]] const Cell& CellAt(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept;

  /// Replaces the rectangle's heights, re-derives the cells it touches and records the delta;
  /// false, with nothing changed, for a rectangle outside the landscape or with the wrong count.
  [[nodiscard]] bool ApplyDelta(const HeightDelta& _delta);

  [[nodiscard]] const std::vector<HeightDelta>& Deltas() const noexcept
  {
    return m_deltas;
  }

  void SetObstruction(std::uint32_t _cellX, std::uint32_t _cellY, std::uint8_t _obstruction) noexcept;

  /// The definition and the deltas, in order: what determines every sample.
  void AddToHash(StateHash& _hash) const noexcept;

private:
  void DeriveCells(std::uint32_t _cellX0, std::uint32_t _cellY0, std::uint32_t _cellX1, std::uint32_t _cellY1) noexcept;

  LandscapeDefinition m_definition{};
  std::uint32_t m_cellsPerSide = 0;
  std::uint32_t m_samplesPerSide = 0;
  std::vector<std::int16_t> m_heights;
  std::vector<Cell> m_cells;
  std::vector<HeightDelta> m_deltas;
};

} // namespace Frontier
