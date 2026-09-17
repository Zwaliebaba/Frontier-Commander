#include "pch.h"

#include "InputSubscription.h"

#include "InputRouter.h"

namespace Neuron
{

InputSubscription::InputSubscription(InputRouter* _owner, std::uint32_t _id) noexcept
  : m_owner(_owner),
    m_id(_id)
{
}

InputSubscription::~InputSubscription()
{
  Reset();
}

InputSubscription::InputSubscription(InputSubscription&& _other) noexcept
  : m_owner(_other.m_owner),
    m_id(_other.m_id)
{
  _other.m_owner = nullptr;
  _other.m_id = 0;
}

InputSubscription& InputSubscription::operator=(InputSubscription&& _other) noexcept
{
  if (this != &_other)
  {
    // The subscription this handle already held goes first; overwriting it would leak a handler
    // nothing can cancel.
    Reset();
    m_owner = _other.m_owner;
    m_id = _other.m_id;
    _other.m_owner = nullptr;
    _other.m_id = 0;
  }
  return *this;
}

void InputSubscription::Reset() noexcept
{
  if (m_owner != nullptr)
  {
    m_owner->Unsubscribe(m_id);
  }
  m_owner = nullptr;
  m_id = 0;
}

} // namespace Neuron
