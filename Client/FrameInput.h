#pragma once

#include "InputEvent.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Neuron
{

/// What a frame saw, derived from the events by DeriveFrameInput (TechnicalDesign.md §6.5): the
/// held state carries over, the edges and the travel are the frame's. One edge per control per
/// frame is the rule that keeps a press and release inside one frame visible: the press lands this
/// frame and the release the next, in order, because everything behind a deferred event waits too.
struct FrameInput
{
  std::array<bool, KEY_COUNT> keysHeld{};
  std::array<std::int8_t, KEY_COUNT> keyEdges{}; ///< +1 pressed this frame, -1 released, 0 neither
  std::array<bool, MOUSE_BUTTON_COUNT> buttonsHeld{};
  std::array<std::int8_t, MOUSE_BUTTON_COUNT> buttonEdges{};
  std::int32_t mouseX = 0; ///< The last absolute position, in client pixels
  std::int32_t mouseY = 0;
  std::int32_t mouseVelocityX = 0; ///< The absolute position's change over the frame, which stops at the screen's edge
  std::int32_t mouseVelocityY = 0;
  std::int32_t relativeX = 0; ///< The frame's raw travel, summed over every relative packet
  std::int32_t relativeY = 0;
  std::int32_t wheelDetents = 0;   ///< Whole detents this frame, away from the user positive
  std::int32_t wheelRemainder = 0; ///< The partial detent carried into the next frame
  bool relativeSeen = false;       ///< A relative packet has arrived at least once, so the raw path is live
};

/// Advances _state by one frame from the front of _events and returns how many events it
/// consumed; the caller erases exactly that many and keeps the rest for the next frame.
/// Consumption stops at the first event that would put a second edge on a control this frame.
/// Pure: no window, no Windows API, no allocation, which is what lets FrameInputTests pin it.
[[nodiscard]] std::size_t DeriveFrameInput(std::span<const InputEvent> _events, FrameInput& _state) noexcept;

struct MouseTravel
{
  std::int32_t x;
  std::int32_t y;
};

/// The frame's mouse travel for aiming: the raw path once a relative packet has ever arrived, the
/// absolute position's velocity until then, so that a device which never sends one (an absolute
/// tablet, some remote desktops) keeps the aim it always had rather than none.
[[nodiscard]] MouseTravel TravelOf(const FrameInput& _state) noexcept;

} // namespace Neuron
