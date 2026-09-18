#include "pch.h"

#include "StateHash.h"

namespace Frontier
{

void StateHash::AddSeat(const Seat& _seat) noexcept
{
  Add(_seat.kind);
  Add(_seat.alliance);
  Add(_seat.powerHundredths);
  Add(_seat.stockpileCapHundredths);
  AddSpan(std::span<const std::uint32_t>(_seat.researchComplete));
  Add(static_cast<std::uint32_t>(_seat.researchActive.size()));
  for (const ResearchProgress& progress : _seat.researchActive)
  {
    Add(progress.item);
    Add(progress.remainingTicks);
  }
  Add(static_cast<std::uint32_t>(_seat.designs.size()));
  for (const DeviceDesign& design : _seat.designs)
  {
    Add(design.chassis);
    Add(design.drive);
    AddSpan(std::span<const std::uint32_t>(design.modules));
    Add(design.moduleCount);
  }
  Add(_seat.deviceCount);
  Add(_seat.deviceCap);
  Add(_seat.structureCount);
  Add(_seat.structureCap);
  AddSpan(std::span<const std::uint8_t>(_seat.fogViewers));
  AddSpan(std::span<const FogState>(_seat.fogState));
  Add(static_cast<std::uint32_t>(_seat.ghosts.size()));
  for (const Ghost& ghost : _seat.ghosts)
  {
    AddObjectId(ghost.structure);
    Add(ghost.seat);
    Add(ghost.design);
    Add(ghost.cellX);
    Add(ghost.cellY);
    Add(ghost.seenTick);
  }
  Add(static_cast<std::uint32_t>(_seat.rejections.size()));
  for (const OrderRejection& rejection : _seat.rejections)
  {
    Add(rejection.kind);
    Add(rejection.reason);
  }
  AddBool(_seat.defeated);
  AddBool(_seat.surrendered);
}

void StateHash::AddOrder(const Order& _order) noexcept
{
  Add(_order.tick);
  AddSpan(std::span<const std::int32_t>(_order.operands));
  Add(_order.seat);
  Add(_order.kind);
}

void StateHash::AddObjectId(ObjectId _id) noexcept
{
  Add(_id.value);
  Add(_id.kind);
}

void StateHash::AddDevice(ObjectId _id, const Device& _device) noexcept
{
  AddObjectId(_id);
  Add(_device.seat);
  Add(_device.design);
  Add(_device.x);
  Add(_device.y);
  Add(_device.z);
  Add(_device.facing);
  Add(_device.hitPoints);
  Add(_device.experience);
  Add(_device.primaryOrder);
  AddObjectId(_device.target);
  Add(_device.destinationX);
  Add(_device.destinationZ);
  Add(_device.fire);
  Add(_device.range);
  Add(_device.retreat);
  Add(_device.movement);
  Add(_device.group);
  AddSpan(std::span<const std::uint32_t>(_device.reloadTicks));
}

void StateHash::AddStructure(ObjectId _id, const Structure& _structure) noexcept
{
  AddObjectId(_id);
  Add(_structure.seat);
  Add(_structure.design);
  Add(_structure.cellX);
  Add(_structure.cellY);
  Add(_structure.y);
  Add(_structure.state);
  Add(_structure.hitPoints);
  Add(_structure.buildProgressHundredths);
  AddSpan(std::span<const std::uint32_t>(_structure.modules));
  Add(_structure.moduleCount);
  AddObjectId(_structure.working);
  Add(_structure.workRemainingTicks);
}

void StateHash::AddProjectile(ObjectId _id, const Projectile& _projectile) noexcept
{
  AddObjectId(_id);
  Add(_projectile.seat);
  AddObjectId(_projectile.shooter);
  Add(_projectile.module);
  Add(_projectile.x);
  Add(_projectile.y);
  Add(_projectile.z);
  Add(_projectile.impactX);
  Add(_projectile.impactY);
  Add(_projectile.impactZ);
  Add(_projectile.ticksToImpact);
}

void StateHash::AddFeature(ObjectId _id, const Feature& _feature) noexcept
{
  AddObjectId(_id);
  Add(_feature.design);
  Add(_feature.cellX);
  Add(_feature.cellY);
  Add(_feature.y);
  Add(_feature.facing);
}

void StateHash::AddWreck(ObjectId _id, const Wreck& _wreck) noexcept
{
  AddObjectId(_id);
  Add(_wreck.seat);
  AddObjectId(_wreck.origin);
  Add(_wreck.design);
  Add(_wreck.x);
  Add(_wreck.y);
  Add(_wreck.z);
  Add(_wreck.facing);
  Add(_wreck.decayTicks);
}

} // namespace Frontier
