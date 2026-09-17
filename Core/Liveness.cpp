#include "pch.h"

#include "Liveness.h"

namespace Neuron
{

Liveness::Liveness(const LivenessSettings& _settings, std::uint32_t _tick) noexcept
  : m_settings(_settings),
    m_lastHeard(_tick),
    m_lastSent(_tick)
{
}

void Liveness::Heard(std::uint32_t _tick) noexcept
{
  if (_tick > m_lastHeard)
  {
    m_lastHeard = _tick;
  }
}

void Liveness::Sent(std::uint32_t _tick) noexcept
{
  if (_tick > m_lastSent)
  {
    m_lastSent = _tick;
  }
}

bool Liveness::HeartbeatDue(std::uint32_t _tick) const noexcept
{
  return _tick >= m_lastSent && _tick - m_lastSent >= m_settings.heartbeatTicks;
}

LivenessState Liveness::Advance(std::uint32_t _tick) noexcept
{
  if (m_state == LivenessState::Lost)
  {
    return m_state;
  }
  const std::uint32_t silence = _tick >= m_lastHeard ? _tick - m_lastHeard : 0;
  if (silence < m_settings.timeoutTicks)
  {
    m_state = LivenessState::Alive;
  }
  else if (silence < m_settings.timeoutTicks + m_settings.graceTicks)
  {
    m_state = LivenessState::Suspect;
  }
  else
  {
    m_state = LivenessState::Lost;
  }
  return m_state;
}

} // namespace Neuron
