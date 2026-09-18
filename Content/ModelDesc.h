#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// A model as data (TechnicalDesign.md §8): positions, per-triangle colours, markers and fragments.
// A device is three models drawn as a tree — a chassis with a drive at each MarkerDrive* and a
// module at a MarkerMount* — so the model count is chassis plus drives plus modules rather than
// their product. Positions are subunits, as every other distance in Content is; the importer
// under Tools\ converts from whatever the authoring tool wrote.

namespace Frontier
{

inline constexpr std::uint32_t MODEL_DESC_VERSION = 1;

struct ModelVertex
{
  std::int32_t x;
  std::int32_t y;
  std::int32_t z;

  [[nodiscard]] constexpr bool operator==(const ModelVertex&) const noexcept = default;
};

/// One triangle: three vertex indices and the flat colour it is drawn in. A colour whose alpha is
/// zero is a team-colour slot, drawn neither lit nor fogged (ADR-005).
struct ModelTriangle
{
  std::uint16_t a;
  std::uint16_t b;
  std::uint16_t c;
  std::array<std::uint8_t, 4> color;
  std::uint16_t fragment; ///< Which fragment this triangle belongs to

  [[nodiscard]] constexpr bool operator==(const ModelTriangle&) const noexcept = default;
};

/// A named point on a model, with an orientation, where another model or an effect is attached:
/// MarkerDrive*, MarkerMount*, MarkerMuzzle.
struct ModelMarker
{
  std::string name;
  ModelVertex position;
  std::uint16_t headingBinaryAngle; ///< The binary angle of Core/BinaryAngle.h
  std::uint16_t pitchBinaryAngle;

  [[nodiscard]] bool operator==(const ModelMarker&) const noexcept = default;
};

/// A piece that comes away when the thing is destroyed, as the Species models carried them.
struct ModelFragment
{
  std::string name;
  ModelVertex center;

  [[nodiscard]] bool operator==(const ModelFragment&) const noexcept = default;
};

struct ModelDesc
{
  std::uint32_t version;
  std::string id;
  std::vector<ModelVertex> vertices;
  std::vector<ModelTriangle> triangles;
  std::vector<ModelMarker> markers;
  std::vector<ModelFragment> fragments;

  [[nodiscard]] bool operator==(const ModelDesc&) const noexcept = default;
};

} // namespace Frontier
