#include "pch.h"

#include "ScaleMode.h"

namespace Neuron
{

ScaledRectangle FitAuthored(std::uint32_t _clientWidth, std::uint32_t _clientHeight, std::uint32_t _authoredWidth,
                            std::uint32_t _authoredHeight) noexcept
{
  if (_clientWidth == 0 || _clientHeight == 0 || _authoredWidth == 0 || _authoredHeight == 0)
  {
    return {ScaleMode::Bilinear, 0, 0, 0, 0, 0};
  }
  // The largest rectangle of the authored aspect inside the client area: the width binds when the
  // client is relatively narrower than the authored aspect, the height otherwise.
  const std::uint64_t clientWidth = _clientWidth;
  const std::uint64_t clientHeight = _clientHeight;
  const std::uint64_t authoredWidth = _authoredWidth;
  const std::uint64_t authoredHeight = _authoredHeight;
  std::uint64_t width = 0;
  std::uint64_t height = 0;
  if (clientWidth * authoredHeight <= clientHeight * authoredWidth)
  {
    width = clientWidth;
    height = clientWidth * authoredHeight / authoredWidth;
  }
  else
  {
    height = clientHeight;
    width = clientHeight * authoredWidth / authoredHeight;
  }
  ScaledRectangle fit{};
  fit.width = static_cast<std::uint32_t>(width);
  fit.height = static_cast<std::uint32_t>(height);
  fit.x = static_cast<std::int32_t>((clientWidth - width) / 2);
  fit.y = static_cast<std::int32_t>((clientHeight - height) / 2);
  if (width == authoredWidth && height == authoredHeight)
  {
    fit.mode = ScaleMode::Exact;
    fit.factor = 1;
  }
  else if (width % authoredWidth == 0 && height % authoredHeight == 0 && width / authoredWidth == height / authoredHeight)
  {
    fit.mode = ScaleMode::Integer;
    fit.factor = static_cast<std::uint32_t>(width / authoredWidth);
  }
  else
  {
    fit.mode = ScaleMode::Bilinear;
    fit.factor = 0;
  }
  return fit;
}

bool AuthoredFromClient(const ScaledRectangle& _fit, std::int32_t _clientX, std::int32_t _clientY, std::uint32_t _authoredWidth,
                        std::uint32_t _authoredHeight, AuthoredPosition& _outAuthored) noexcept
{
  if (_fit.width == 0 || _fit.height == 0 || _authoredWidth == 0 || _authoredHeight == 0)
  {
    return false;
  }
  const std::int64_t insideX = static_cast<std::int64_t>(_clientX) - _fit.x;
  const std::int64_t insideY = static_cast<std::int64_t>(_clientY) - _fit.y;
  if (insideX < 0 || insideY < 0 || insideX >= _fit.width || insideY >= _fit.height)
  {
    return false; // The letterbox, or off the window entirely.
  }
  // Integer arithmetic, as FitAuthored is, so that the inverse is the same on every machine. The
  // multiply comes first: dividing by the fit's size and then scaling would floor twice and put a
  // pointer near the right-hand edge of a bilinear fit one authored pixel short of where it is.
  _outAuthored.x = static_cast<std::int32_t>(insideX * _authoredWidth / _fit.width);
  _outAuthored.y = static_cast<std::int32_t>(insideY * _authoredHeight / _fit.height);
  return true;
}

} // namespace Neuron
