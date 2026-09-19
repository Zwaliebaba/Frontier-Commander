#include "pch.h"

#include "ModelComposer.h"

#include "BinaryAngle.h"

#include <cmath>
#include <string_view>

namespace Frontier
{

namespace
{

/// The marker names of Content/ModelDesc.h. A marker is matched by PREFIX and not by equality,
/// because a chassis with four wheels carries MarkerDrive1 through MarkerDrive4 and the number is
/// the Species convention rather than anything this game reads: what matters is how many there are
/// and where, and both come from the list itself.
constexpr std::string_view MARKER_DRIVE = "MarkerDrive";
constexpr std::string_view MARKER_MOUNT = "MarkerMount";
constexpr std::string_view MARKER_MUZZLE = "MarkerMuzzle";

[[nodiscard]] bool IsMarker(const ModelMarker& _marker, std::string_view _prefix) noexcept
{
  return std::string_view(_marker.name).substr(0, _prefix.size()) == _prefix;
}

/// A row's authored draw factor as the render path takes it. Hundredths, because Content is integer
/// throughout; a row that says zero or less is drawn at its native size rather than as nothing,
/// which would be an object that vanishes for a reason nobody could see.
[[nodiscard]] float ScaleOf(std::int32_t _hundredths) noexcept
{
  return _hundredths > 0 ? static_cast<float>(_hundredths) / 100.0f : 1.0f;
}

} // namespace

Pose PlacedAt(const Pose& _parent, const ModelVertex& _offsetSubunits, std::uint16_t _headingBinaryAngle) noexcept
{
  // The offset is authored in the parent's own space, so it turns with the parent before it is
  // added. Sine and cosine of the parent's heading, once: a device has a handful of markers and the
  // pair is the same for all of them.
  const float sine = std::sin(_parent.headingRadians);
  const float cosine = std::cos(_parent.headingRadians);
  const float x = Neuron::WorldUnitsOfSubunits(_offsetSubunits.x);
  const float y = Neuron::WorldUnitsOfSubunits(_offsetSubunits.y);
  const float z = Neuron::WorldUnitsOfSubunits(_offsetSubunits.z);
  // THIS IS Client/Shaders/GeometryVS.hlsl's Turn, WRITTEN AGAIN, and it has to be: the chassis's
  // own vertices are turned by that function and a marker on the chassis has to land where they do.
  // Heading zero looks along +z and grows toward +x, which is what the marker importer wrote and
  // what the simulation's binary angle counts in; a sign the other way round here would mirror
  // every drive and every mount about the device and look, at a glance, like a different model.
  // Y is up and a heading does not turn it.
  return Pose{_parent.x + x * cosine + z * sine, _parent.y + y, _parent.z - x * sine + z * cosine,
              _parent.headingRadians + Neuron::RadiansOfBinaryAngle(_headingBinaryAngle)};
}

ModelComposer::ModelComposer(const ContentTree& _content)
  : m_content(&_content)
{
  const auto resolve = [this](std::string_view _modelId, std::int32_t _scaleHundredths)
  {
    Resolved resolved;
    resolved.scale = ScaleOf(_scaleHundredths);
    const ModelDesc* model = m_content->FindModel(_modelId);
    if (model == nullptr)
    {
      ++m_unresolvedRows;
      return resolved;
    }
    // The index into ContentTree::models, which IS the model id a RenderInstance carries: pointer
    // arithmetic over the vector the tree holds, because FindModel gives back a pointer into it.
    resolved.model = static_cast<std::uint32_t>(model - m_content->models.data());
    return resolved;
  };

  m_chassis.reserve(_content.components.chassis.size());
  for (const ChassisDesc& row : _content.components.chassis)
  {
    m_chassis.push_back(resolve(row.model, row.modelScaleHundredths));
  }
  m_drives.reserve(_content.components.drives.size());
  for (const DriveDesc& row : _content.components.drives)
  {
    m_drives.push_back(resolve(row.model, row.modelScaleHundredths));
  }
  m_modules.reserve(_content.components.modules.size());
  for (const ModuleDesc& row : _content.components.modules)
  {
    m_modules.push_back(resolve(row.model, row.modelScaleHundredths));
  }
  m_structures.reserve(_content.structures.structures.size());
  for (const StructureDesc& row : _content.structures.structures)
  {
    m_structures.push_back(resolve(row.model, row.modelScaleHundredths));
  }
  m_structureModules.reserve(_content.structures.modules.size());
  for (const StructureModuleDesc& row : _content.structures.modules)
  {
    m_structureModules.push_back(resolve(row.model, row.modelScaleHundredths));
  }
}

std::uint32_t ModelComposer::ChassisModel(std::uint32_t _row) const noexcept
{
  return _row < m_chassis.size() ? m_chassis[_row].model : NO_MODEL;
}

std::uint32_t ModelComposer::DriveModel(std::uint32_t _row) const noexcept
{
  return _row < m_drives.size() ? m_drives[_row].model : NO_MODEL;
}

std::uint32_t ModelComposer::ModuleModel(std::uint32_t _row) const noexcept
{
  return _row < m_modules.size() ? m_modules[_row].model : NO_MODEL;
}

std::uint32_t ModelComposer::StructureModel(std::uint32_t _row) const noexcept
{
  return _row < m_structures.size() ? m_structures[_row].model : NO_MODEL;
}

std::uint32_t ModelComposer::StructureModuleModel(std::uint32_t _row) const noexcept
{
  return _row < m_structureModules.size() ? m_structureModules[_row].model : NO_MODEL;
}

void ModelComposer::ComposeSingle(std::uint32_t _modelIndex, float _scale, const Pose& _pose, const ObjectAppearance& _appearance,
                                  Neuron::RenderInstanceKind _kind, std::vector<Neuron::RenderInstance>& _outInstances) const
{
  if (_modelIndex == NO_MODEL)
  {
    return;
  }
  Neuron::RenderInstance instance{};
  instance.modelId = _modelIndex;
  instance.x = _pose.x;
  instance.y = _pose.y;
  instance.z = _pose.z;
  instance.headingRadians = _pose.headingRadians;
  instance.kind = _kind;
  instance.colorIndex = _appearance.colorIndex;
  instance.rankBadge = _appearance.rankBadge;
  instance.selected = _appearance.selected;
  instance.scale = _scale;
  _outInstances.push_back(instance);
}

void ModelComposer::ComposeDevice(const DesignState& _design, const Pose& _pose, const ObjectAppearance& _appearance,
                                  std::vector<Neuron::RenderInstance>& _outInstances, std::vector<ComposedMuzzle>* _outMuzzles) const
{
  const std::uint32_t chassisRow = _design.chassis;
  if (chassisRow >= m_chassis.size())
  {
    return; // A design naming a row the content does not have draws nothing at all.
  }
  const Resolved& chassis = m_chassis[chassisRow];
  ComposeSingle(chassis.model, chassis.scale, _pose, _appearance, Neuron::RenderInstanceKind::Object, _outInstances);
  if (chassis.model == NO_MODEL)
  {
    // No chassis model means no markers, and therefore nowhere to hang a drive or a module: the
    // parts are not drawn at the device's origin in a heap, they are not drawn.
    return;
  }

  const ModelDesc& chassisModel = m_content->models[chassis.model];
  const Resolved* drive = _design.drive < m_drives.size() ? &m_drives[_design.drive] : nullptr;
  std::uint32_t mount = 0;
  for (const ModelMarker& marker : chassisModel.markers)
  {
    if (IsMarker(marker, MARKER_DRIVE))
    {
      if (drive != nullptr)
      {
        ComposeSingle(drive->model, drive->scale, PlacedAt(_pose, marker.position, marker.headingBinaryAngle), _appearance,
                      Neuron::RenderInstanceKind::Object, _outInstances);
      }
      continue;
    }
    if (!IsMarker(marker, MARKER_MOUNT))
    {
      continue;
    }
    // THE MOUNTS ARE TAKEN IN THE ORDER THE MODEL LISTS THEM, and the design's modules in the order
    // it lists those; the nth module goes on the nth mount. A design with fewer modules than the
    // chassis has mounts leaves the rest empty, which is what an unfilled slot looks like.
    const std::uint32_t slot = mount++;
    if (slot >= _design.moduleCount || slot >= _design.modules.size())
    {
      continue;
    }
    const std::uint32_t moduleRow = _design.modules[slot];
    if (moduleRow >= m_modules.size())
    {
      continue;
    }
    const Resolved& module = m_modules[moduleRow];
    const Pose mounted = PlacedAt(_pose, marker.position, marker.headingBinaryAngle);
    ComposeSingle(module.model, module.scale, mounted, _appearance, Neuron::RenderInstanceKind::Object, _outInstances);
    if (_outMuzzles == nullptr || module.model == NO_MODEL)
    {
      continue;
    }
    for (const ModelMarker& muzzle : m_content->models[module.model].markers)
    {
      if (!IsMarker(muzzle, MARKER_MUZZLE))
      {
        continue;
      }
      // Carried forward through the module's transform, which was itself carried through the
      // device's: a muzzle is two frames deep and that is the whole reason this is not a position
      // the caller could have worked out from the device alone.
      const Pose atMuzzle = PlacedAt(mounted, muzzle.position, muzzle.headingBinaryAngle);
      _outMuzzles->push_back(ComposedMuzzle{moduleRow, atMuzzle.x, atMuzzle.y, atMuzzle.z});
    }
  }
}

void ModelComposer::ComposeStructure(const StructureState& _structure, const Pose& _pose, const ObjectAppearance& _appearance,
                                     std::vector<Neuron::RenderInstance>& _outInstances) const
{
  const std::uint32_t row = _structure.design;
  if (row >= m_structures.size())
  {
    return;
  }
  const Resolved& structure = m_structures[row];
  const std::size_t first = _outInstances.size();
  ComposeSingle(structure.model, structure.scale, _pose, _appearance, Neuron::RenderInstanceKind::Object, _outInstances);
  if (structure.model != NO_MODEL)
  {
    const ModelDesc& model = m_content->models[structure.model];
    std::uint32_t mount = 0;
    for (const ModelMarker& marker : model.markers)
    {
      if (!IsMarker(marker, MARKER_MOUNT))
      {
        continue;
      }
      const std::uint32_t slot = mount++;
      if (slot >= _structure.moduleCount || slot >= _structure.modules.size())
      {
        continue;
      }
      const std::uint32_t moduleRow = _structure.modules[slot];
      if (moduleRow >= m_structureModules.size())
      {
        continue;
      }
      const Resolved& module = m_structureModules[moduleRow];
      ComposeSingle(module.model, module.scale, PlacedAt(_pose, marker.position, marker.headingBinaryAngle), _appearance,
                    Neuron::RenderInstanceKind::Object, _outInstances);
    }
  }
  // THE BUILD PROGRESS REACHES EVERY PART, set after the fact rather than passed down: a structure
  // half built is half built including its modules, and threading the number through three
  // functions that otherwise do not care about it would be three chances to forget one.
  for (std::size_t index = first; index < _outInstances.size(); ++index)
  {
    _outInstances[index].buildPercent = _structure.buildPercent;
  }
}

} // namespace Frontier
