#pragma once

#include "UiLayout.h"

#include <cstdint>
#include <string>
#include <vector>

// The seven things a panel can hold (Design/Interface.md §3; m1-vertical-slice/K3): a label, a
// button, a toggle, a list, a progress bar, an icon and a text field. That is the whole set M1
// needs, and a toolkit with an eighth kind nothing uses is a toolkit with an eighth kind to keep
// working.
//
// ONE STRUCT, NOT SEVEN TYPES AND A VARIANT. A widget is a rectangle, a kind, and the few numbers
// that kind reads; the fields another kind does not use are zero, exactly as Client/InputEvent.h's
// flat event record does it and for the same reason. The panels of M1 are fixed and authored
// (§2 - "nothing moves"), so the thing this saves is not allocation but the machinery a hierarchy
// would need to pay for: a vtable, a downcast at every hit test, and a second place to look when a
// panel draws.
//
// THE TOOLKIT EMITS NO ORDER. A widget reports what happened to it and stops there; turning that
// into an OrderMessage is the game's (§6), and it is what keeps this layer free of the game
// (AGENTS.md R9).

namespace Neuron
{

enum class UiWidgetKind : std::uint8_t
{
  Label,       ///< Text, and nothing to click
  Button,      ///< Acts on the release, inside the rectangle it was pressed in
  Toggle,      ///< A button that stays down
  List,        ///< Rows of rowHeightPixels; a click selects one
  ProgressBar, ///< value out of maximum, drawn from the left; never animated (§3)
  Icon,        ///< One 32x32 cell of GameData\Textures\Icons.dds
  TextField    ///< Takes characters while it holds the focus
};

inline constexpr std::uint8_t UI_WIDGET_KIND_COUNT = 7;

/// What happened to a widget this event, for whoever owns the panel to act on.
enum class UiAction : std::uint8_t
{
  None,
  Pressed,    ///< A button was clicked, or a toggle or list row was, and the two below say how
  Toggled,    ///< A toggle changed state; the widget's `on` is the new one
  Selected,   ///< A list row was chosen; the widget's `value` is which
  TextEdited, ///< A text field took or lost a character; the widget's `text` is the new one
  TextSubmitted
};

/// How a progress bar picks its fill. Design/Interface.md §3 gives three bar colours for two
/// different jobs - `barBuild` for "construction and production progress", `barHealth` and
/// `barHealthLow` for a health reading - and a bar holding only a value and a maximum cannot tell
/// which job it is doing. This says, in the one place the widget is built.
enum class UiBarStyle : std::uint8_t
{
  Build, ///< Always barBuild: a construction or production bar has no low state, it has a length
  Health ///< barHealthLow under a quarter, barHealth at or above it
};

/// A widget, as a panel holds it.
///
/// `id` IS THE OWNER'S HANDLE AND NOT AN INDEX. A panel's widgets are added once and never move,
/// but a caller that read an index would be reading a number this file chose; an id the caller
/// chose is one it can switch on without a table mapping the two. Zero means "no widget", which is
/// what a UiEventResult carries when nothing was hit.
struct UiWidget
{
  UiWidgetKind kind = UiWidgetKind::Label;
  UiRect rect;
  std::uint32_t id = 0;
  std::string text;
  bool enabled = true;
  bool on = false;                         ///< Toggle: down. Button: held, while the pointer is on it
  std::int32_t value = 0;                  ///< ProgressBar: the value. List: the selected row. Icon: the cell
  std::int32_t maximum = 0;                ///< ProgressBar: the maximum. List: how many rows there are
  std::int32_t rowHeightPixels = 0;        ///< List
  std::uint32_t textLimit = 0;             ///< TextField: the most characters it takes, 0 for no limit
  UiBarStyle barStyle = UiBarStyle::Build; ///< ProgressBar
  /// List: one caption a row. `maximum` stays the authority on how many rows the list HAS, because
  /// that is what the hit test and the row rectangles are built on; this is what they are called,
  /// and a row with no caption here simply draws none. A list whose owner draws its own rows leaves
  /// it empty.
  std::vector<std::string> rows;
};

/// Whether this kind takes a click at all. A label, a bar and an icon are readouts: they are drawn
/// over and never under the pointer, so a click on one falls through to the panel, which consumes
/// it anyway (§4: "Right click on a panel - nothing; the panel consumes it").
[[nodiscard]] constexpr bool Interactive(UiWidgetKind _kind) noexcept
{
  return _kind == UiWidgetKind::Button || _kind == UiWidgetKind::Toggle || _kind == UiWidgetKind::List || _kind == UiWidgetKind::TextField;
}

/// Which row of a list rect is at _y, or -1 for none. Free rather than a member, because a panel
/// drawing a list wants the same arithmetic the hit test uses and neither owns the other.
[[nodiscard]] std::int32_t ListRowAt(const UiWidget& _list, std::int32_t _y) noexcept;

/// The rectangle of one row of a list, for drawing. Empty for a row the list does not have.
[[nodiscard]] UiRect ListRowRect(const UiWidget& _list, std::int32_t _row) noexcept;

} // namespace Neuron
