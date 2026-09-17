#pragma once

#include "WindowsHeader.h"

#include <cstdint>

namespace Neuron
{

/// Registers the mouse for Raw Input on the window: relative steps as the device reports them,
/// unclamped by the screen's edge and without the pointer acceleration Windows applies on the way
/// to WM_MOUSEMOVE (TechnicalDesign.md §6.5). False, logged, when the registration fails; the
/// absolute path then serves the aim, as it does until the first relative packet arrives.
[[nodiscard]] bool RegisterRawMouse(HWND _window);

/// Reads the packet a WM_INPUT message names; true with a relative step that moved. Packets from
/// other devices and from a mouse reporting absolute coordinates are refused, which is what keeps
/// the fallback rule honest: a device that never sends a relative packet never switches the aim.
[[nodiscard]] bool ReadRawMouseMove(LPARAM _packet, std::int32_t& _deltaX, std::int32_t& _deltaY) noexcept;

} // namespace Neuron
