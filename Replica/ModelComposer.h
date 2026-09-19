#pragma once

#include "Interpolation.h"

#include "ContentTree.h"
#include "Records.h"
#include "RenderView.h"

#include <cstdint>
#include <vector>

// A device or a structure turned into the models that draw it (m1-vertical-slice/R2;
// Content/ModelDesc.h; TechnicalDesign.md §6.3).
//
// A DEVICE IS A TREE OF THREE MODEL KINDS AND THE COUNT IS A SUM. The chassis model carries a
// MarkerDrive for each place a drive goes and a MarkerMount for each module; a drive model is drawn
// at every MarkerDrive and a module model at every MarkerMount. So a design with two modules is
// three models drawn, and NOT a model authored for every combination of chassis, drive and modules
// - which for the catalogue GameDesign.md §8 describes would be a product nobody could author or
// ship. It is the reason the markers exist.
//
// THE ROW-TO-MODEL LOOKUP IS DONE ONCE, AT CONSTRUCTION. A component row names its model by string
// id and ContentTree::FindModel is a linear search over every model in the game, so resolving one
// per part per object per frame is a string search per handful of triangles. Every row is resolved
// here into an index into ContentTree::models - which is the id a RenderInstance carries, because
// Client/ModelBuffers.h is built from that same vector in that same order.
//
// IT COMPOSES AND DOES NOT DECIDE. Which objects are drawn at all is the interest set's, already
// settled by the host; where they are is Interpolation's; whether one is selected is picking's.
// What is here is the marker arithmetic and nothing else.

namespace Frontier
{

/// What every part of one object shares: its commander, its rank and whether it is selected.
struct ObjectAppearance
{
  std::uint8_t colorIndex = 0;
  std::uint8_t rankBadge = 0;
  bool selected = false;
};

/// Where a module's shot leaves, in world units: its model's MarkerMuzzle carried through the
/// module's own transform and then the device's, which is what "carrying the MarkerMuzzle forward"
/// means. The shot effects of TechnicalDesign.md §6.2 start here rather than at the device's origin,
/// which is the difference between a beam leaving the barrel and one leaving the floor.
struct ComposedMuzzle
{
  std::uint32_t moduleRow = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

class ModelComposer
{
public:
  /// A row whose model the content tree does not hold. Nothing is drawn for it, and the count says
  /// how many there were, so a mistyped model id is a number a test can assert on rather than an
  /// object that is silently invisible.
  static constexpr std::uint32_t NO_MODEL = 0xFFFFFFFFu;

  /// _content outlives the composer; every row's model is resolved here and never again.
  explicit ModelComposer(const ContentTree& _content);

  /// The instances of one device, appended to _outInstances: the chassis, a drive at each
  /// MarkerDrive and a module at each MarkerMount. When _outMuzzles is not null, one entry is
  /// appended for every mounted module whose model carries a MarkerMuzzle.
  void ComposeDevice(const DesignState& _design, const Pose& _pose, const ObjectAppearance& _appearance,
                     std::vector<Neuron::RenderInstance>& _outInstances, std::vector<ComposedMuzzle>* _outMuzzles = nullptr) const;

  /// The instances of one structure: its own model, and a structure module at each MarkerMount it
  /// has one for. _buildPercent is StructureState's, 0 to 100, and reaches every instance.
  void ComposeStructure(const StructureState& _structure, const Pose& _pose, const ObjectAppearance& _appearance,
                        std::vector<Neuron::RenderInstance>& _outInstances) const;

  /// One model at a pose, for the things that are not composed at all: a wreck, a feature, a
  /// projectile. _kind reaches the instance, because that is what picking and the minimap read.
  void ComposeSingle(std::uint32_t _modelIndex, float _scale, const Pose& _pose, const ObjectAppearance& _appearance,
                     Neuron::RenderInstanceKind _kind, std::vector<Neuron::RenderInstance>& _outInstances) const;

  /// The model index and draw scale a row resolved to, for the callers that place one themselves.
  [[nodiscard]] std::uint32_t ChassisModel(std::uint32_t _row) const noexcept;
  [[nodiscard]] std::uint32_t DriveModel(std::uint32_t _row) const noexcept;
  [[nodiscard]] std::uint32_t ModuleModel(std::uint32_t _row) const noexcept;
  [[nodiscard]] std::uint32_t StructureModel(std::uint32_t _row) const noexcept;
  [[nodiscard]] std::uint32_t StructureModuleModel(std::uint32_t _row) const noexcept;

  /// Rows whose model id names nothing in the tree. A content fault, counted rather than thrown:
  /// ContentValidator is what reports one with a file and a line, and a renderer that refused to
  /// start over a missing model would take the whole match down for one mistyped row.
  [[nodiscard]] std::uint32_t UnresolvedRows() const noexcept
  {
    return m_unresolvedRows;
  }

private:
  /// One row's resolved model and the factor it is drawn at.
  struct Resolved
  {
    std::uint32_t model = NO_MODEL;
    float scale = 1.0f;
  };

  const ContentTree* m_content;
  std::vector<Resolved> m_chassis;
  std::vector<Resolved> m_drives;
  std::vector<Resolved> m_modules;
  std::vector<Resolved> m_structures;
  std::vector<Resolved> m_structureModules;
  std::uint32_t m_unresolvedRows = 0;
};

/// The world position of a point authored in a model's own space, once that model has been placed
/// at _pose. Free, because the composer, the muzzle and any later effect want the same arithmetic
/// and none of them owns it.
///
/// THE MARKER'S PITCH IS NOT APPLIED, and that is a gap rather than a decision: ModelMarker carries
/// a pitchBinaryAngle and Core's RenderInstance has only a heading, so a part on a pitched marker is
/// drawn level. Every M1 marker is level - a drive sits on the ground and a mount faces out - so
/// nothing is wrong on screen yet, but the content already records something the render path throws
/// away. It is noted in R2's plan entry.
[[nodiscard]] Pose PlacedAt(const Pose& _parent, const ModelVertex& _offsetSubunits, std::uint16_t _headingBinaryAngle) noexcept;

} // namespace Frontier
