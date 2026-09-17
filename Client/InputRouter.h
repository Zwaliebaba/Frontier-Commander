#pragma once

#include "FrameInput.h"
#include "InputEvent.h"
#include "InputSubscription.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace Neuron
{

/// What a sink did with an event. Consumed means acted on, and the event reaches nothing further
/// down; Ignored means the next sink, and finally the frame's view, still may.
enum class InputDisposition : std::uint8_t
{
  Ignored,
  Consumed
};

/// Something offered the frame's events: the interface first, then the game's sinks. Answer
/// Consumed only for an event the sink acted on; answering it for one it merely looked at deletes
/// that input from the game.
class InputSink
{
public:
  InputSink() = default;
  virtual ~InputSink() = default;
  InputSink(const InputSink&) = delete;
  InputSink& operator=(const InputSink&) = delete;

  [[nodiscard]] virtual InputDisposition OnInputEvent(const InputEvent& _event) = 0;
};

/// The one ordering rule of input (TechnicalDesign.md §6.5): each event is offered to the sinks in
/// the order they were added, the interface first, and the first to consume it ends the offer. A
/// consumed key is masked for its whole hold, its release included, so a letter typed into a text
/// field never also fires the binding it carries; a consumed button is masked for that edge only,
/// because a click is an edge and the interface still polls the held button under it. The router
/// also keeps the subscriptions, which fire once per frame over the masked view, so a control the
/// interface consumed reaches no subscriber either. The simulation never subscribes: its input
/// becomes orders through Net, nowhere else.
class InputRouter
{
public:
  void AddSink(InputSink* _sink);
  void RemoveSink(InputSink* _sink);

  /// Offers the events a frame consumed, in order, and records what the sinks took for Mask.
  void Dispatch(std::span<const InputEvent> _events);
  /// Blanks in _view the controls a sink consumed, then forgets the keys whose release the mask
  /// covered this frame; once per frame, after Dispatch and before anything reads the view.
  void Mask(FrameInput& _view) noexcept;
  [[nodiscard]] bool IsKeyMasked(std::uint8_t _key) const noexcept
  {
    return m_maskedKeys[_key];
  }

  /// Calls _handler once each frame the trigger fires in the masked view, until the handle is
  /// destroyed. A handler may cancel its own subscription or another's, and may subscribe, which
  /// does not fire until the next frame. A null handler is refused and the handle is inactive.
  [[nodiscard]] InputSubscription Subscribe(InputTrigger _trigger, std::function<void()> _handler);
  /// The handle's door; nothing else needs it.
  void Unsubscribe(std::uint32_t _id) noexcept;
  [[nodiscard]] std::size_t SubscriptionCount() const noexcept;
  /// Once per frame, after Mask: which subscriptions fire is decided before any handler runs.
  void FireSubscriptions(const FrameInput& _view);

private:
  struct Subscription
  {
    std::uint32_t id;
    InputTrigger trigger;
    std::function<void()> handler;
    bool canceled;
  };

  void Compact();

  std::vector<InputSink*> m_sinks;
  std::array<bool, KEY_COUNT> m_maskedKeys{};
  std::array<bool, KEY_COUNT> m_releasedKeys{};
  std::array<bool, MOUSE_BUTTON_COUNT> m_maskedButtonEdges{};
  std::int32_t m_lastKeyDown = -1;
  std::vector<Subscription> m_subscriptions;
  std::uint32_t m_nextSubscriptionId = 1;
};

} // namespace Neuron
