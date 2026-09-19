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

/// The last refusal a client has been told about (Design/Interface.md §6). The sequence counts this
/// seat's refusals rather than naming a tick, so the client compares it for inequality and never for
/// order; 0 means none yet, which is why the counter skips 0 when it wraps.
struct RejectionLatch
{
  std::uint16_t sequence = 0;
  OrderKind kind = OrderKind::Move;
  RejectReason reason = RejectReason::Accepted;
};

/// What one client has been told, between publishes: its history, the fog it has, and where its
/// sequence numbers have got to. The Host holds one of these a client.
struct ClientView
{
  std::uint8_t seat = 0;
  std::uint32_t nextSequence = 1; ///< NO_BASELINE is 0, so a frame's sequence starts at 1
  std::uint32_t acknowledgedSequence = NO_BASELINE;
  ClientHistory history;
  /// The fog this client is KNOWN TO HOLD, cell by cell: the grid as of `foggedThrough`, which is
  /// the newest frame it has acknowledged and whose runs have been folded in. O(cells) a client,
  /// which ADR-008 already names as the price of a per-commander fog grid.
  ///
  /// ACKNOWLEDGED AND NOT SENT, WHICH IS THE WHOLE POINT. Net/Client.cpp drops any delta whose
  /// baseline is not exactly the frame it last applied, which happens whenever a publish outruns an
  /// acknowledgement - the ordinary case at a 10 Hz publish rate, not a lossy-link one. Every other
  /// field survives that, because a frame is the difference between what the client can see NOW and
  /// the baseline it acknowledged, so the next frame carries the same difference again. The fog
  /// used to advance this grid on every ENCODE, so a dropped frame took its runs with it and the
  /// cells in them were never sent again: the commander's map kept whatever it had and diverged for
  /// the rest of the match (m1-vertical-slice/G1a - the first capture of a real match came back
  /// 100% black). Advancing it only on acknowledgement gives the fog the same semantics as
  /// everything else in the frame.
  std::vector<FogState> fog;
  /// The sequence `fog` is the state as of, or NO_BASELINE when the client holds nothing.
  std::uint32_t foggedThrough = NO_BASELINE;
  /// What this client was last told about a refused order, and what it will be told again until
  /// another one is refused. Host::Advance folds the seat's rejections into it EVERY TICK; the
  /// simulation clears them at the start of every stage 1 and a publish is due on even ticks only,
  /// so a refusal judged on an odd tick would never reach anyone if this were read at publish time.
  ///
  /// It lives on the view rather than beside the simulation's seat because a refusal is reported to
  /// the commander who sent the order: a seat playing under AI has nobody to tell, and a client that
  /// joins has no business being handed a refusal from before it arrived. Both fall out of the
  /// latch starting empty with the view.
  RejectionLatch rejection;
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
[[nodiscard]] SeatState WireSeat(std::uint8_t _seat, const Seat& _record, const RejectionLatch& _rejection);
[[nodiscard]] DesignState WireDesign(std::uint8_t _seat, std::uint32_t _index, const DeviceDesign& _design);

/// The change record between two states of one device, or nothing when nothing a client can see
/// about it moved. Public for the same reason.
[[nodiscard]] bool ChangeOf(const DeviceState& _baseline, const DeviceState& _now, DeviceChange& _out);

} // namespace Frontier
