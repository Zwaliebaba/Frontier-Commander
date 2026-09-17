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

} // namespace Neuron
