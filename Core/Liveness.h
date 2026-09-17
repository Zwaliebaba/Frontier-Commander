#pragma once

#include <cstdint>

namespace Neuron
{

/// The liveness machine's settings, in ticks of whatever clock drives it (the host's tick).
struct LivenessSettings
{
  std::uint32_t heartbeatTicks; ///< Send a heartbeat when nothing else has gone out for this long
  std::uint32_t timeoutTicks;   ///< Silence this long makes the peer suspect
  std::uint32_t graceTicks;     ///< Suspect this much longer makes the peer lost, for good
};

enum class LivenessState : std::uint8_t
{
  Alive,
  Suspect,
  Lost
};

/// A pure state machine over ticks (TechnicalDesign.md §3, step 4 of the host loop) with no clock
/// of its own: the caller tells it when something was heard, when something was sent, and what
/// the tick is, and it answers whether a heartbeat is due and whether the peer is alive, suspect
/// or lost. Lost is sticky: a peer that answers after the grace has run out has to reconnect.
class Liveness
{
public:
  Liveness(const LivenessSettings& _settings, std::uint32_t _tick) noexcept;

  /// A datagram from the peer arrived at _tick.
  void Heard(std::uint32_t _tick) noexcept;

  /// A datagram went to the peer at _tick, a heartbeat or anything else.
  void Sent(std::uint32_t _tick) noexcept;

  /// Whether nothing has gone out for heartbeatTicks or more.
  [[nodiscard]] bool HeartbeatDue(std::uint32_t _tick) const noexcept;

  /// Re-evaluates the state at _tick and returns it.
  LivenessState Advance(std::uint32_t _tick) noexcept;

  [[nodiscard]] LivenessState State() const noexcept
  {
    return m_state;
  }

  [[nodiscard]] std::uint32_t LastHeard() const noexcept
  {
    return m_lastHeard;
  }

  [[nodiscard]] std::uint32_t LastSent() const noexcept
  {
    return m_lastSent;
  }

private:
  LivenessSettings m_settings;
  std::uint32_t m_lastHeard;
  std::uint32_t m_lastSent;
  LivenessState m_state = LivenessState::Alive;
};

} // namespace Neuron
