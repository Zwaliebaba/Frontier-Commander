#pragma once

#include "WindowsHeader.h"

#include <cstdint>

namespace Neuron
{

/// The game's one window (ADR-004): borderless, covering the primary monitor, and owning Escape
/// and Alt+F4 as the only ways out, since a WS_POPUP window has no close box. The window procedure
/// records the close request and the client size and does nothing else; from m0-foundation/T19 it
/// enqueues input events the same way, and never acts on them.
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

private:
  static LRESULT CALLBACK Procedure(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam);
  LRESULT OnMessage(UINT _message, WPARAM _wParam, LPARAM _lParam);
  void ReadClientSize();

  HWND m_handle = nullptr;
  HINSTANCE m_instance = nullptr;
  std::uint32_t m_clientWidth = 0;
  std::uint32_t m_clientHeight = 0;
  bool m_closeRequested = false;
  bool m_resized = false;
};

} // namespace Neuron
