#pragma once

#include <cstdint>
#include <span>
#include <vector>

// One commander's fog of war (TechnicalDesign.md §4.6; GameDesign.md §3): a viewer count and a
// state per cell. The host computes it and it is also the replication filter, so it is simulation
// state - in the hash, in the snapshot - rather than a thing the client works out for itself.
//
// EXPLORED IS NOT DERIVABLE FROM THE COUNT. A cell stays explored after its last viewer leaves, so
// the explored map is history and the count is the present; the two are stored side by side
// because neither can be computed from the other.
//
// THE COUNT IS A uint16 RATHER THAN THE BYTE THE TASK SPECIFIED, and the reason is the army caps.
// Allied seats share vision (GameDesign.md §2), an alliance may be all eight seats, and a seat may
// field 200 devices and 300 structures (§4), so 4,000 viewers can share one grid. A sight radius is
// 12 to 40 cells, so several hundred of them can hold one cell in view at a chokepoint, and a byte
// that saturated there could never be decremented back to zero: the cell would stay lit for the
// rest of the match. A fog that wrongly reveals is the worst failure this system has, and the
// second byte a cell costs buys it away outright. 65,535 is beyond 8 x 500 viewers, so it cannot
// be reached at all.

namespace Frontier
{

/// The two bits of fog a cell carries. The order is the snapshot's and the hash's.
enum class FogState : std::uint8_t
{
  Unexplored,
  Explored,
  Visible
};

inline constexpr std::uint8_t FOG_STATE_COUNT = 3;

class FogGrid
{
public:
  /// Sizes the grid for a landscape and clears every cell to unexplored and unseen.
  void Resize(std::uint32_t _cellsPerSide);

  /// Takes a grid read from a snapshot. False, with nothing changed, when the two arrays do not
  /// agree with each other or with the side.
  [[nodiscard]] bool Restore(std::uint32_t _cellsPerSide, std::vector<std::uint16_t> _viewers, std::vector<FogState> _state);

  [[nodiscard]] bool Empty() const noexcept
  {
    return m_state.empty();
  }

  [[nodiscard]] std::uint32_t CellsPerSide() const noexcept
  {
    return m_cellsPerSide;
  }

  [[nodiscard]] std::size_t Count() const noexcept
  {
    return m_state.size();
  }

  [[nodiscard]] bool Inside(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
  {
    return _cellX < m_cellsPerSide && _cellY < m_cellsPerSide;
  }

  [[nodiscard]] FogState StateAt(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept;
  [[nodiscard]] std::uint16_t ViewersAt(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept;

  [[nodiscard]] bool Visible(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
  {
    return StateAt(_cellX, _cellY) == FogState::Visible;
  }

  [[nodiscard]] bool Explored(std::uint32_t _cellX, std::uint32_t _cellY) const noexcept
  {
    return StateAt(_cellX, _cellY) != FogState::Unexplored;
  }

  /// One more viewer holds this cell: it becomes visible, and explored for ever.
  void AddViewer(std::uint32_t _cellX, std::uint32_t _cellY) noexcept;
  /// One fewer: at zero the cell falls back to explored, never to unexplored.
  void RemoveViewer(std::uint32_t _cellX, std::uint32_t _cellY) noexcept;

  [[nodiscard]] std::span<const std::uint16_t> Viewers() const noexcept
  {
    return m_viewers;
  }

  [[nodiscard]] std::span<const FogState> States() const noexcept
  {
    return m_state;
  }

  [[nodiscard]] bool operator==(const FogGrid&) const noexcept = default;

private:
  std::uint32_t m_cellsPerSide = 0;
  std::vector<std::uint16_t> m_viewers; ///< Cell-row-major, as the state is
  std::vector<FogState> m_state;
};

} // namespace Frontier
