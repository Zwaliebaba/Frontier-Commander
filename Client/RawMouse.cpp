#include "pch.h"

#include "RawMouse.h"

#include "Log.h"

#include <bit>
#include <string>

namespace Neuron
{

bool RegisterRawMouse(HWND _window)
{
  // Usage page 1, usage 2 is the generic desktop mouse. No flags, so raw input follows the
  // keyboard focus and the game hears nothing while it is in the background.
  RAWINPUTDEVICE mouse{};
  mouse.usUsagePage = 0x01;
  mouse.usUsage = 0x02;
  mouse.dwFlags = 0;
  mouse.hwndTarget = _window;
  if (RegisterRawInputDevices(&mouse, 1, sizeof mouse) == 0)
  {
    Log::Write(LogLevel::Warning, "raw mouse: registration failed (" + std::to_string(GetLastError()) + "); aiming from WM_MOUSEMOVE");
    return false;
  }
  return true;
}

bool ReadRawMouseMove(LPARAM _packet, std::int32_t& _deltaX, std::int32_t& _deltaY) noexcept
{
  RAWINPUT raw{};
  UINT size = sizeof raw;
  // The message carries the packet's handle as an integer, and the handle is that integer.
  const HRAWINPUT handle = std::bit_cast<HRAWINPUT>(_packet);
  if (GetRawInputData(handle, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
  {
    return false;
  }
  if (raw.header.dwType != RIM_TYPEMOUSE || (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
  {
    return false;
  }
  _deltaX = raw.data.mouse.lLastX;
  _deltaY = raw.data.mouse.lLastY;
  return _deltaX != 0 || _deltaY != 0;
}

} // namespace Neuron
