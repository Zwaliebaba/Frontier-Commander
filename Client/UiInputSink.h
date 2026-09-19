#pragma once

#include "InputEvent.h"
#include "InputRouter.h"
#include "ScaleMode.h"
#include "UiPanel.h"

#include <cstdint>
#include <vector>

// The interface's place in the input order (TechnicalDesign.md §6.5; Design/Interface.md §4): the
// router's FIRST sink, so that a click on a panel never also reaches selection or the camera.
//
// WHY A SINK AND NOT A CHECK IN THE GAME LOOP. Client/InputRouter.h already has the rule - each
// event goes to the sinks in order and the first to consume it ends the offer - and it masks the
// frame's polled view and the subscriptions to match. An interface that tested "is the pointer over
// a panel?" in the game's own code would have to repeat that masking in every place that reads
// input, and the places it forgot would be the bugs.
//
// IT OWNS THE ONE CLIENT-TO-AUTHORED CONVERSION IN THE INPUT PATH. Events carry client pixels
// (Client/InputEvent.h) and every rectangle in the interface is authored (§4), so the sink holds
// the current ScaledRectangle and runs AuthoredFromClient once per event. A pointer in the
// letterbox is over nothing at all and is not consumed.

namespace Neuron
{

class UiInputSink : public InputSink
{
public:
  /// The panels this sink offers events to, in the order they are added. The LAST one added is
  /// asked first, so a modal panel - the pause menu, the match-end overlay (§2) - added over the
  /// others takes the click that lands on it without anything having to reorder the list.
  void AddPanel(UiPanel* _panel);
  void RemovePanel(UiPanel* _panel);
  void Clear() noexcept;

  /// Where the scene target lands in the client area this frame (Client/ScaleMode.h). Set once a
  /// frame, before the events are dispatched; until it is set, nothing is over anything.
  void SetFit(const ScaledRectangle& _fit) noexcept
  {
    m_fit = _fit;
  }

  [[nodiscard]] InputDisposition OnInputEvent(const InputEvent& _event) override;

  /// What the last acted-on event did, for the frame to read after Dispatch. Cleared by Take.
  [[nodiscard]] UiEventResult Take() noexcept;

  /// Where the pointer was, in authored pixels, and whether it was over the frame at all. The
  /// cursor of §4 is drawn here, and picking is refused when it is over a panel.
  [[nodiscard]] bool PointerAuthored(AuthoredPosition& _outPosition) const noexcept;

  /// True when the pointer is over any visible panel: what §5 refuses a picking ray for.
  [[nodiscard]] bool PointerOverPanel() const noexcept;

private:
  [[nodiscard]] bool ToAuthored(std::int32_t _clientX, std::int32_t _clientY, AuthoredPosition& _out) const noexcept;

  std::vector<UiPanel*> m_panels;
  ScaledRectangle m_fit{};
  AuthoredPosition m_pointer{};
  bool m_pointerInside = false;
  UiEventResult m_pending{};
};

} // namespace Neuron
