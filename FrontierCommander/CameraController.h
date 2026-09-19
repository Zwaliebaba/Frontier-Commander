#pragma once

#include "Camera.h"
#include "FrameInput.h"
#include "HeightView.h"

#include <cstdint>

namespace Frontier
{

/// Flies the camera from the frame's input (TechnicalDesign.md §6.5; SpeciesLook.md §7): the arrow
/// keys and the screen's edges move it along the ground, the MIDDLE button's drag aims it, the
/// wheel and PageUp/PageDown change its height, and the floor keeps it out of the terrain.
///
/// NO LETTER KEYS. Design/Interface.md §7's hotkey table gives the letters to orders and two of the
/// camera's collided with it: S was Stop as well as backward, R was Attack-move as well as pitch
/// down. The owner ruled on 2026-09-19 that the orders win - §7 specifies the game's controls and
/// these were M0 placeholders that predate any order existing - so W, A, S, D, Q, E, R and F are
/// the commander's, and the camera keeps every other way it already had of doing the same things.
///
/// THE MIDDLE BUTTON AND NOT THE RIGHT (Design/Interface.md §12 ruling 3). M0 bound aiming to the
/// right button because no orders existed yet; the right button is the order button now, and a drag
/// threshold on it - the alternative the document considered - delays every order by however long
/// the threshold takes to fail.
class CameraController
{
public:
  void Advance(Neuron::Camera& _camera, const Neuron::FrameInput& _input, const Neuron::HeightView& _terrain, float _seconds,
               std::uint32_t _clientWidth, std::uint32_t _clientHeight) const;
};

} // namespace Frontier
