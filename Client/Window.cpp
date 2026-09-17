#include "pch.h"

#include "Window.h"

#include "Log.h"

#include <string>

namespace Neuron
{

namespace
{

constexpr const wchar_t* CLASS_NAME = L"Neuron.Window";
constexpr const wchar_t* INSTANCE_PROPERTY = L"Neuron.Window.Instance";

} // namespace

Window::Window()
{
  // Per-monitor DPI awareness before any window exists, so that a monitor reports its physical
  // pixels: a 4K desktop at 150% is 3840 wide, not a virtualised 2560, and the integer-multiple
  // case of AGENTS.md §5 can happen at all.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  m_instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof windowClass;
  windowClass.lpfnWndProc = &Window::Procedure;
  windowClass.hInstance = m_instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = CLASS_NAME;
  winrt::check_bool(RegisterClassExW(&windowClass) != 0);
  // The primary monitor's extent in physical pixels; its origin is (0, 0) by definition.
  const int width = GetSystemMetrics(SM_CXSCREEN);
  const int height = GetSystemMetrics(SM_CYSCREEN);
  m_handle = CreateWindowExW(0, CLASS_NAME, L"Frontier Commander", WS_POPUP, 0, 0, width, height, nullptr, nullptr, m_instance, nullptr);
  if (m_handle == nullptr)
  {
    const HRESULT error = HRESULT_FROM_WIN32(GetLastError());
    UnregisterClassW(CLASS_NAME, m_instance);
    winrt::throw_hresult(error);
  }
  // The instance travels with the window as a property rather than through the creation
  // parameters, so that the procedure never turns an integer message parameter into a pointer.
  if (SetPropW(m_handle, INSTANCE_PROPERTY, this) == 0)
  {
    const HRESULT error = HRESULT_FROM_WIN32(GetLastError());
    DestroyWindow(m_handle);
    UnregisterClassW(CLASS_NAME, m_instance);
    winrt::throw_hresult(error);
  }
  ReadClientSize();
  m_resized = false;
  ShowWindow(m_handle, SW_SHOW);
  Log::Write(LogLevel::Info,
             "window: " + std::to_string(m_clientWidth) + "x" + std::to_string(m_clientHeight) + " client pixels over the primary monitor");
}

Window::~Window()
{
  if (m_handle != nullptr)
  {
    RemovePropW(m_handle, INSTANCE_PROPERTY);
    DestroyWindow(m_handle);
  }
  UnregisterClassW(CLASS_NAME, m_instance);
}

bool Window::Pump()
{
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0)
  {
    if (message.message == WM_QUIT)
    {
      m_closeRequested = true;
      break;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return !m_closeRequested;
}

bool Window::TakeResized() noexcept
{
  const bool resized = m_resized;
  m_resized = false;
  return resized;
}

LRESULT CALLBACK Window::Procedure(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  // Null until the constructor has attached the instance, so the messages of creation itself go to
  // the default procedure; the client size is read once the window exists.
  Window* window = static_cast<Window*>(GetPropW(_window, INSTANCE_PROPERTY));
  if (window == nullptr)
  {
    return DefWindowProcW(_window, _message, _wParam, _lParam);
  }
  return window->OnMessage(_message, _wParam, _lParam);
}

LRESULT Window::OnMessage(UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  switch (_message)
  {
  case WM_KEYDOWN:
    if (_wParam == static_cast<WPARAM>(VK_ESCAPE))
    {
      m_closeRequested = true;
      return 0;
    }
    break;
  case WM_SYSKEYDOWN:
    // Alt+F4, which the default procedure would turn into WM_CLOSE only for a window with a system
    // menu; a popup has none, so the game answers it itself.
    if (_wParam == static_cast<WPARAM>(VK_F4))
    {
      m_closeRequested = true;
      return 0;
    }
    break;
  case WM_CLOSE:
    m_closeRequested = true;
    return 0;
  case WM_SIZE:
    ReadClientSize();
    return 0;
  case WM_ERASEBKGND:
    // The swap chain owns every pixel (AGENTS.md §4); there is no background to erase.
    return 1;
  case WM_PAINT:
    ValidateRect(m_handle, nullptr);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(m_handle, _message, _wParam, _lParam);
}

void Window::ReadClientSize()
{
  RECT client{};
  if (GetClientRect(m_handle, &client) == 0)
  {
    return;
  }
  const std::uint32_t width = static_cast<std::uint32_t>(client.right - client.left);
  const std::uint32_t height = static_cast<std::uint32_t>(client.bottom - client.top);
  if (width != m_clientWidth || height != m_clientHeight)
  {
    m_clientWidth = width;
    m_clientHeight = height;
    m_resized = true;
  }
}

} // namespace Neuron
