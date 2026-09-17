#pragma once

#include <cstdint>

namespace Neuron
{

class InputRouter;

enum class InputEdge : std::uint8_t
{
  Pressed,
  Released
};

/// What a subscriber waits for: a key's edge, as the frame's view reports it after the router has
/// masked what the interface consumed. Bindings from a control name to a key are M1's; a subscriber
/// in M0 names the key.
struct InputTrigger
{
  std::uint8_t key;
  InputEdge edge;
};

/// The subscription's handle (TechnicalDesign.md §6.5): move-only, because a subscription is a
/// piece of ownership, and copying it would leave two objects each believing they must cancel it.
/// Destroying the handle cancels the subscription, which is what makes an object that subscribes
/// in its constructor safe to destroy without telling anybody. A handle never outlives its router.
class InputSubscription
{
public:
  InputSubscription() noexcept = default;
  InputSubscription(InputRouter* _owner, std::uint32_t _id) noexcept;
  ~InputSubscription();
  InputSubscription(const InputSubscription&) = delete;
  InputSubscription& operator=(const InputSubscription&) = delete;
  InputSubscription(InputSubscription&& _other) noexcept;
  InputSubscription& operator=(InputSubscription&& _other) noexcept;

  /// Cancels early; the destructor does the same, and a second call does nothing.
  void Reset() noexcept;
  [[nodiscard]] bool IsActive() const noexcept
  {
    return m_owner != nullptr;
  }

private:
  InputRouter* m_owner = nullptr;
  std::uint32_t m_id = 0;
};

} // namespace Neuron
