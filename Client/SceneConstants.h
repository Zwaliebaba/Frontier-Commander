#pragma once

#include "WindowsHeader.h"

#include <DirectXMath.h>

#include <cstddef>

namespace Neuron
{

/// What every world pass reads from b0 (TerrainVS.hlsl, TerrainPS.hlsl, WaterPS.hlsl): the view
/// projection transposed for HLSL's column-major default, the camera, the two lights of
/// Lighting.h, and the fog. One per frame in flight, in a buffer the terrain pass owns.
struct SceneConstants
{
  DirectX::XMFLOAT4X4 viewProjection;
  DirectX::XMFLOAT4 cameraPosition;
  DirectX::XMFLOAT4 lightDirection0; ///< Toward the light, normalized
  DirectX::XMFLOAT4 lightColor0;
  DirectX::XMFLOAT4 lightDirection1;
  DirectX::XMFLOAT4 lightColor1;
  DirectX::XMFLOAT4 fog; ///< start, end, mode (0 linear to the colour, 1 desaturation), the desaturation's ceiling
  DirectX::XMFLOAT4 fogColor;
};

static_assert(sizeof(SceneConstants) % 16 == 0);

/// A constant buffer's offset is 256-byte aligned.
inline constexpr std::size_t SCENE_CONSTANTS_STRIDE = 256;
static_assert(sizeof(SceneConstants) <= SCENE_CONSTANTS_STRIDE);

} // namespace Neuron
