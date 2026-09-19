#pragma once

#include "ClientHistory.h"
#include "Interest.h"
#include "Messages.h"

#include "Sim.h"

#include <cstdint>
#include <vector>

// Turning a simulation and an interest set into the frame one client gets (TechnicalDesign.md
// §5.3): what was created since the baseline whole, what changed as a mask and the changed fields,
// what left as a removal, the events of the interval, the runs of fog that changed, and the
// commander's own seat.
//
// THE ENCODER NEVER READS THE SIMULATION FOR AN OBJECT THE INTEREST SET DOES NOT NAME. That is not
// an optimisation: it is the shape that makes §5.2's guarantee testable, because the only way a
// record of an unseen object could reach a frame is through a set that named it.

namespace Frontier
{

/// What one client has been told, between publishes: its history, the fog it has, and where its
/// sequence numbers have got to. The Host holds one of these a client.
struct ClientView
{
  std::uint8_t seat = 0;
  std::uint32_t nextSequence = 1; ///< NO_BASELINE is 0, so a frame's sequence starts at 1
  std::uint32_t acknowledgedSequence = NO_BASELINE;
  ClientHistory history;
  /// The fog this client last had, cell by cell, so that a publish sends the runs that changed.
  /// O(cells) a client, which ADR-008 already names as the price of a per-commander fog grid.
  std::vector<FogState> fog;
  bool everSentFog = false;
};

/// Builds the frame for this publish and the record of what the client will hold if it applies it.
/// _baseline is the newest frame the client acknowledged, or null for a full frame.
void EncodeFrame(const Sim& _sim, const InterestSet& _interest, ClientView& _view, const FrameRecord* _baseline,
                 std::span<const Event> _events, Frame& _outFrame, FrameRecord& _outRecord);

/// The wire form of one device, structure, wreck or feature, as the encoder writes it. Public
/// because the interest test reads them back and compares them with the simulation.
[[nodiscard]] DeviceState WireDevice(std::uint32_t _id, const Device& _device);
[[nodiscard]] StructureState WireStructure(const ContentTree& _content, std::uint32_t _id, const Structure& _structure);
[[nodiscard]] StructureState WireGhost(const Ghost& _ghost);
[[nodiscard]] WreckState WireWreck(std::uint32_t _id, const Wreck& _wreck);
[[nodiscard]] FeatureState WireFeature(std::uint32_t _id, const Feature& _feature);
[[nodiscard]] SeatState WireSeat(std::uint8_t _seat, const Seat& _record);
[[nodiscard]] DesignState WireDesign(std::uint8_t _seat, std::uint32_t _index, const DeviceDesign& _design);

/// The change record between two states of one device, or nothing when nothing a client can see
/// about it moved. Public for the same reason.
[[nodiscard]] bool ChangeOf(const DeviceState& _baseline, const DeviceState& _now, DeviceChange& _out);

} // namespace Frontier
