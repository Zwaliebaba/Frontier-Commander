#include "pch.h"

#include "Window.h"

#include "InputEvent.h"
#include "InputQueue.h"
#include "Log.h"
#include "RawMouse.h"

#include <cstdint>
#include <string>

namespace Neuron
{

namespace
{

constexpr const wchar_t* CLASS_NAME = L"Neuron.Window";
constexpr const wchar_t* INSTANCE_PROPERTY = L"Neuron.Window.Instance";

// Bit 30 of a key message's lParam: the key was already down, so Windows is auto-repeating it.
constexpr LPARAM PREVIOUS_KEY_STATE = 0x40000000;

[[nodiscard]] std::int32_t LowSigned(LPARAM _packed) noexcept
{
  return static_cast<std::int16_t>(_packed & 0xFFFF);
}

[[nodiscard]] std::int32_t HighSigned(LPARAM _packed) noexcept
{
  return static_cast<std::int16_t>((_packed >> 16) & 0xFFFF);
}

[[nodiscard]] Neuron::InputEvent KeyEvent(Neuron::InputEventKind _kind, WPARAM _key, LPARAM _lParam, bool _systemKey) noexcept
{
  Neuron::InputEvent event;
  event.kind = _kind;
  event.key = static_cast<std::uint8_t>(_key);
  event.repeat = (_lParam & PREVIOUS_KEY_STATE) != 0;
  event.systemKey = _systemKey;
  return event;
}

[[nodiscard]] Neuron::InputEvent ButtonEvent(Neuron::InputEventKind _kind, Neuron::MouseButton _button) noexcept
{
  Neuron::InputEvent event;
  event.kind = _kind;
  event.button = _button;
  return event;
}

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

void Window::SetTitle(const wchar_t* _title) noexcept
{
  if (m_handle != nullptr)
  {
    SetWindowTextW(m_handle, _title);
  }
}

bool Window::TakeResized() noexcept
{
  const bool resized = m_resized;
  m_resized = false;
  return resized;
}

void Window::AttachInput(InputQueue* _queue)
{
  m_input = _queue;
  if (_queue != nullptr && !RegisterRawMouse(m_handle))
  {
    Log::Write(LogLevel::Warning, "window: no Raw Input; the aim follows the absolute mouse position");
  }
}

void Window::Enqueue(const InputEvent& _event) noexcept
{
  if (m_input != nullptr)
  {
    m_input->Push(_event);
  }
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
  // ENQUEUE ONLY (TechnicalDesign.md §6.5): a message becomes a record, and the frame decides what
  // it meant, so that two messages about one key in one frame both survive. Escape and Alt+F4 are
  // the exceptions, answered here so that they work with the frame loop stalled.
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (_wParam < KEY_COUNT)
    {
      Enqueue(KeyEvent(InputEventKind::KeyDown, _wParam, _lParam, _message == WM_SYSKEYDOWN));
    }
    if ((_message == WM_KEYDOWN && _wParam == static_cast<WPARAM>(VK_ESCAPE)) ||
        (_message == WM_SYSKEYDOWN && _wParam == static_cast<WPARAM>(VK_F4)))
    {
      // A popup has no system menu, so the default procedure would not turn Alt+F4 into WM_CLOSE.
      m_closeRequested = true;
    }
    return 0;
  case WM_KEYUP:
  case WM_SYSKEYUP:
    if (_wParam < KEY_COUNT)
    {
      Enqueue(KeyEvent(InputEventKind::KeyUp, _wParam, _lParam, _message == WM_SYSKEYUP));
    }
    return 0;
  case WM_CHAR:
  {
    // The character for the keyboard layout, dead keys applied: a virtual-key code is not a
    // character on any layout but the US one.
    InputEvent character;
    character.kind = InputEventKind::Character;
    character.character = static_cast<std::uint32_t>(_wParam);
    Enqueue(character);
    return 0;
  }
  case WM_SYSCHAR:
    // Swallowed: Alt+key is never text here, and the default handling of a WM_SYSCHAR with no menu
    // to open is the system beep, once per keystroke.
    return 0;
  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN:
  {
    const MouseButton button = _message == WM_LBUTTONDOWN   ? MouseButton::Left
                               : _message == WM_RBUTTONDOWN ? MouseButton::Right
                                                            : MouseButton::Middle;
    Enqueue(ButtonEvent(InputEventKind::MouseButtonDown, button));
    // Captured while any button is down, so that a release outside the window still arrives.
    if (m_buttonsDown == 0)
    {
      SetCapture(m_handle);
    }
    ++m_buttonsDown;
    return 0;
  }
  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
  {
    const MouseButton button = _message == WM_LBUTTONUP   ? MouseButton::Left
                               : _message == WM_RBUTTONUP ? MouseButton::Right
                                                          : MouseButton::Middle;
    Enqueue(ButtonEvent(InputEventKind::MouseButtonUp, button));
    if (m_buttonsDown > 0)
    {
      --m_buttonsDown;
      if (m_buttonsDown == 0)
      {
        ReleaseCapture();
      }
    }
    return 0;
  }
  case WM_MOUSEMOVE:
  {
    InputEvent move;
    move.kind = InputEventKind::MouseMove;
    move.x = LowSigned(_lParam);
    move.y = HighSigned(_lParam);
    Enqueue(move);
    return 0;
  }
  case WM_MOUSEWHEEL:
  {
    // The raw delta; a high-resolution wheel sends fractions of a detent, and the frame adds them up.
    InputEvent wheel;
    wheel.kind = InputEventKind::Wheel;
    wheel.wheelDelta = static_cast<std::int16_t>((_wParam >> 16) & 0xFFFF);
    Enqueue(wheel);
    return 0;
  }
  case WM_INPUT:
  {
    InputEvent move;
    move.kind = InputEventKind::MouseRawMove;
    if (ReadRawMouseMove(_lParam, move.x, move.y))
    {
      Enqueue(move);
    }
    // Passed on: the system needs to see WM_INPUT to clean the packet up after us.
    break;
  }
  case WM_KILLFOCUS:
  {
    // Windows sends no release for anything held when focus goes; the frame makes them.
    InputEvent focusLost;
    focusLost.kind = InputEventKind::FocusLost;
    Enqueue(focusLost);
    m_buttonsDown = 0;
    return 0;
  }
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
