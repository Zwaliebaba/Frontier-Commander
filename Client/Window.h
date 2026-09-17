#pragma once

#include "WindowsHeader.h"

#include <cstdint>

namespace Neuron
{
class InputQueue;
}

namespace Neuron
{

/// The game's one window (ADR-004): borderless, covering the primary monitor, and owning Escape
/// and Alt+F4 as the only ways out, since a WS_POPUP window has no close box. The window procedure
/// records the close request and the client size and enqueues every input message as an event
/// into the attached queue (TechnicalDesign.md §6.5), and does nothing else with any of them: the
/// frame derives, routes and reads them, once, after the one pump.
class Window
{
public:
  Window();
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  /// Drains the message queue: once per frame and nowhere else. False once the user asked to leave.
  [[nodiscard]] bool Pump();

  [[nodiscard]] HWND Handle() const noexcept
  {
    return m_handle;
  }
  [[nodiscard]] std::uint32_t ClientWidth() const noexcept
  {
    return m_clientWidth;
  }
  [[nodiscard]] std::uint32_t ClientHeight() const noexcept
  {
    return m_clientHeight;
  }

  /// True once after the client area changed size, and cleared by the call.
  [[nodiscard]] bool TakeResized() noexcept;

  /// Where the procedure enqueues input from now on; null detaches. The mouse is registered for
  /// Raw Input at the same time, and the queue outlives the window or is detached first.
  void AttachInput(InputQueue* _queue);

private:
  static LRESULT CALLBACK Procedure(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam);
  LRESULT OnMessage(UINT _message, WPARAM _wParam, LPARAM _lParam);
  void ReadClientSize();
  void Enqueue(const struct InputEvent& _event) noexcept;

  InputQueue* m_input = nullptr;
  HWND m_handle = nullptr;
  HINSTANCE m_instance = nullptr;
  std::uint32_t m_clientWidth = 0;
  std::uint32_t m_clientHeight = 0;
  std::uint32_t m_buttonsDown = 0;
  bool m_closeRequested = false;
  bool m_resized = false;
};

} // namespace Neuron
