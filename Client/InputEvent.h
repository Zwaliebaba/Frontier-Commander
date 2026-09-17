#pragma once

#include <cstddef>
#include <cstdint>

// The event stream input arrives as (TechnicalDesign.md §6.5). The rules are the Species
// repository's tasks/input-native-events.yaml (T5 to T9) and the files it names, NeuronClient/
// InputEvents.h, InputRouter.h and Input.h, written fresh there in 2026-08 and written fresh again
// here in this tree's shape; nothing in it descends from the Darwinia-derived code.

namespace Neuron
{

/// One window message, decoded, by kind.
enum class InputEventKind : std::uint8_t
{
  KeyDown,
  KeyUp,
  Character,
  MouseButtonDown,
  MouseButtonUp,
  MouseMove,
  MouseRawMove,
  Wheel,
  FocusLost
};

enum class MouseButton : std::uint8_t
{
  Left,
  Right,
  Middle
};

/// Virtual-key codes are one byte, which is what makes a key an index.
inline constexpr std::size_t KEY_COUNT = 256;
inline constexpr std::size_t MOUSE_BUTTON_COUNT = 3;
/// WHEEL_DELTA, spelled here so that the derivation compiles and tests without windows.h.
inline constexpr std::int32_t WHEEL_DELTA_PER_DETENT = 120;

/// A flat record rather than a variant: a queue of them is plain memory, and the fields a kind
/// does not use are zero.
struct InputEvent
{
  InputEventKind kind = InputEventKind::FocusLost;
  std::uint8_t key = 0;                   ///< KeyDown, KeyUp: the virtual-key code
  bool repeat = false;                    ///< KeyDown: Windows auto-repeating a held key, which is not an edge
  bool systemKey = false;                 ///< KeyDown, KeyUp: WM_SYSKEYDOWN and WM_SYSKEYUP, Alt held or Alt itself
  MouseButton button = MouseButton::Left; ///< MouseButtonDown, MouseButtonUp
  std::uint32_t character = 0;            ///< Character: the UTF-16 code unit WM_CHAR decoded for the keyboard layout
  std::int32_t x = 0;                     ///< MouseMove: client pixels, absolute. MouseRawMove: a relative step in device counts
  std::int32_t y = 0;
  std::int32_t wheelDelta = 0; ///< Wheel: the raw delta in WHEEL_DELTA units, fractions and all
};

[[nodiscard]] constexpr std::size_t IndexOf(MouseButton _button) noexcept
{
  return static_cast<std::size_t>(_button);
}

} // namespace Neuron
