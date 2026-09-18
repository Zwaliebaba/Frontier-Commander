#pragma once

#include "WindowsHeader.h"

#include <d3d12.h>
#include <DirectXMath.h>
#include <unknwn.h>
#include <winrt/base.h>

#include "Camera.h"
#include "DescriptorHeap.h"
#include "GraphicsDevice.h"
#include "RenderView.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Neuron
{

class SceneTarget;

// The fog pass (TechnicalDesign.md §6.2): a full-screen composite that blacks the cells the
// commander has never seen and halves the brightness of the ones seen but not in sight now, drawn
// after the geometry pass and before the UI, from the fog grid the replica sends and nothing else.
//
// THE PASS READS THE DEPTH BUFFER AND MULTIPLIES. What the fog darkens is whatever was drawn at a
// pixel - ground, device, structure, water - so the pass has to know where that pixel IS, and the
// depth buffer is where a renderer already keeps that. One triangle covers the target, each pixel
// takes its depth, the inverse view projection turns pixel and depth into a world position, and
// its cell indexes the fog texture. The darkening itself is the BLEND STATE - source ZERO,
// destination SRC_COLOR, which is destination times source - so the colour target is never read as
// a texture while it is a render target, which would not be legal.
//
// A PIXEL WITH NOTHING DRAWN AT IT IS LEFT ALONE. The depth is cleared to 0 and reversed
// (SceneTarget.h), so 0 means no geometry: that is the background, and blacking the background
// would make the clear colour a lie rather than tell the player anything.
//
// THE FOG TEXTURE IS FETCHED, NOT SAMPLED. There is no sampler in this pass. A cell is either seen
// or it is not, and interpolating between the two would invent a state the simulation does not
// have; the edges are the cell edges. That the boundary is visible is the point - GameDesign.md §3
// makes the three states a rule of play and Interface.md §7 draws the same grid on the minimap.
//
// WHAT THE MULTISAMPLING COSTS, STATED RATHER THAN HIDDEN. The scene target is multisampled and
// this pass runs once per PIXEL, taking sample 0's depth, so all of a pixel's samples are darkened
// by one cell's shade. Where a silhouette crosses a cell boundary the four samples can belong to
// two cells and get one answer. That is one pixel wide along one line in the frame; running the
// pass per sample would cost four times the work to fix it, and the fix would not be visible.

/// How much of what was drawn survives, by FogShade: black, half brightness, and untouched
/// (GameDesign.md §3; Interface.md §7, which gives the minimap the same three).
inline constexpr std::array<float, 3> FOG_SHADES = {0.0f, 0.5f, 1.0f};

/// A stretch of rows contiguous in the grid, and therefore contiguous in the upload buffer too, so
/// that it copies as one region.
struct FogRowRun
{
  std::uint32_t first;
  std::uint32_t count;

  [[nodiscard]] constexpr bool operator==(const FogRowRun&) const noexcept = default;
};

/// The runs of _rows, which the view says are ascending and without repeats, with anything at or
/// past _cellsPerSide dropped. Out of order input costs extra runs and never wrong ones: a run
/// continues only where a row is exactly the last one plus one, so no row is ever copied from a
/// place it was not written to. Pure, so the unit test needs no device.
[[nodiscard]] std::vector<FogRowRun> CoalesceFogRows(std::span<const std::uint32_t> _rows, std::uint32_t _cellsPerSide);

class FogPass
{
public:
  /// _cellsPerSide is the landscape's, and the fog texture is that size for the life of the pass.
  FogPass(GraphicsDevice& _device, const SceneTarget& _scene, std::uint32_t _cellsPerSide);

  /// Uploads the rows of _fog that changed and draws. _scene's depth buffer is DEPTH_WRITE on entry
  /// and on exit, and both targets are bound again before returning.
  void Draw(ID3D12GraphicsCommandList* _list, const SceneTarget& _scene, const Camera& _camera, float _aspect, const FogView& _fog);

  [[nodiscard]] std::uint32_t CellsPerSide() const noexcept
  {
    return m_cellsPerSide;
  }
  /// Rows copied into the texture on the last Draw; the whole grid on the first.
  [[nodiscard]] std::uint32_t LastRowsUploaded() const noexcept
  {
    return m_lastRowsUploaded;
  }

private:
  /// What the pixel shader reads from b0.
  struct Constants
  {
    DirectX::XMFLOAT4X4 inverseViewProjection;
    DirectX::XMFLOAT4 grid;  ///< cells per side, world units per cell, the scene's sample count, 0
    DirectX::XMFLOAT4 shade; ///< FOG_SHADES, and 0
  };
  static_assert(sizeof(Constants) % 16 == 0);

  /// Copies the rows _fog changed - all of them the first time - into the fog texture.
  void Upload(ID3D12GraphicsCommandList* _list, const FogView& _fog, std::uint32_t _slot);

  GraphicsDevice* m_device;
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  winrt::com_ptr<ID3D12Resource> m_texture;
  std::array<winrt::com_ptr<ID3D12Resource>, FRAMES_IN_FLIGHT> m_uploads;
  std::array<std::uint8_t*, FRAMES_IN_FLIGHT> m_uploadMapped{};
  winrt::com_ptr<ID3D12Resource> m_constants;
  std::uint8_t* m_constantsMapped = nullptr;
  /// Shader visible, three descriptors in the order the shader declares them: the fog texture, the
  /// multisampled depth and the single-sampled depth. One of the two depth views is null, because a
  /// Texture2DMS view of a one-sample resource is not legal and neither is the reverse, and a null
  /// descriptor is; the shader branches on the sample count in the constants.
  DescriptorHeap m_views;
  std::uint32_t m_cellsPerSide = 0;
  std::uint32_t m_uploadRowBytes = 0; ///< 512-aligned, so that any row is a legal copy source
  std::uint32_t m_sampleCount = 1;
  std::uint32_t m_lastRowsUploaded = 0;
  bool m_uploaded = false; ///< Whether the texture holds a grid at all
};

} // namespace Neuron
