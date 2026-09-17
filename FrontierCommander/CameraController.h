#pragma once

#include "Camera.h"
#include "FrameInput.h"
#include "HeightView.h"

#include <cstdint>

namespace Frontier
{

/// Flies the camera from the frame's input (TechnicalDesign.md §6.5; SpeciesLook.md §7): the
/// movement keys and the screen's edges move it along the ground, the right button's drag aims it,
/// the wheel and the vertical keys change its height, and the floor keeps it out of the terrain.
/// The bindings are M0's fixed ones until the preferences of M1 map controls to keys.
class CameraController
{
public:
  void Advance(Neuron::Camera& _camera, const Neuron::FrameInput& _input, const Neuron::HeightView& _terrain, float _seconds,
               std::uint32_t _clientWidth, std::uint32_t _clientHeight) const;
};

} // namespace Frontier
