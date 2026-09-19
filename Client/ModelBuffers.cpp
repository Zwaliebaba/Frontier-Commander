#include "pch.h"

#include "ModelBuffers.h"

#include "FixedPoint.h"
#include "GraphicsDevice.h"
#include "Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>
#include <string>

namespace Neuron
{

namespace
{

/// The edge vectors of a triangle, and the cross product that is its outward normal in the SDK's
/// left-handed sense (ModelBuffers.h says why it is c-a before b-a). Integers throughout: a model is
/// authored in subunits, so the direction is exact and the same on every machine, and only the last
/// division to unit length is floating point.
struct Direction
{
  std::array<std::int64_t, 3> value;

  [[nodiscard]] constexpr auto operator<=>(const Direction&) const noexcept = default;
};

[[nodiscard]] Direction FaceDirection(const Frontier::ModelVertex& _a, const Frontier::ModelVertex& _b,
                                      const Frontier::ModelVertex& _c) noexcept
{
  const std::int64_t ux = static_cast<std::int64_t>(_c.x) - _a.x;
  const std::int64_t uy = static_cast<std::int64_t>(_c.y) - _a.y;
  const std::int64_t uz = static_cast<std::int64_t>(_c.z) - _a.z;
  const std::int64_t vx = static_cast<std::int64_t>(_b.x) - _a.x;
  const std::int64_t vy = static_cast<std::int64_t>(_b.y) - _a.y;
  const std::int64_t vz = static_cast<std::int64_t>(_b.z) - _a.z;
  Direction direction{{uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx}};
  // Reduced by the greatest common divisor, so that two coplanar triangles of one face - which
  // produce proportional cross products rather than equal ones - key on the same direction and
  // share their vertices instead of splitting on a rounding difference.
  const std::int64_t divisor = std::gcd(std::gcd(std::abs(direction.value[0]), std::abs(direction.value[1])), std::abs(direction.value[2]));
  if (divisor > 1)
  {
    for (std::int64_t& axis : direction.value)
    {
      axis /= divisor;
    }
  }
  return direction;
}

/// What a vertex is, once the flat attributes are part of it: the description's vertex, the colour
/// of the triangle that uses it, and that triangle's direction.
struct VertexKey
{
  std::uint32_t vertex;
  std::uint32_t color;
  Direction direction;

  [[nodiscard]] constexpr auto operator<=>(const VertexKey&) const noexcept = default;
};

[[nodiscard]] std::uint32_t PackColor(const std::array<std::uint8_t, 4>& _color) noexcept
{
  return static_cast<std::uint32_t>(_color[0]) | (static_cast<std::uint32_t>(_color[1]) << 8) |
         (static_cast<std::uint32_t>(_color[2]) << 16) | (static_cast<std::uint32_t>(_color[3]) << 24);
}

[[nodiscard]] float WorldUnits(std::int32_t _subunits) noexcept
{
  return static_cast<float>(_subunits) / static_cast<float>(SUBUNITS_PER_WORLD_UNIT);
}

[[nodiscard]] winrt::com_ptr<ID3D12Resource> UploadBuffer(ID3D12Device* _device, const void* _bytes, std::size_t _size,
                                                          const wchar_t* _name)
{
  winrt::com_ptr<ID3D12Resource> buffer;
  const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
  const CD3DX12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(std::max<std::size_t>(_size, 16));
  winrt::check_hresult(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(buffer.put())));
  winrt::check_hresult(buffer->SetName(_name));
  if (_bytes != nullptr && _size > 0)
  {
    void* mapped = nullptr;
    const D3D12_RANGE nothing{0, 0};
    winrt::check_hresult(buffer->Map(0, &nothing, &mapped));
    std::memcpy(mapped, _bytes, _size);
    buffer->Unmap(0, nullptr);
  }
  return buffer;
}

} // namespace

ModelMesh BuildModelMesh(const Frontier::ModelDesc& _model)
{
  ModelMesh mesh{};
  if (_model.vertices.empty() || _model.triangles.empty())
  {
    return mesh;
  }

  // The centroid of the description's vertices, in subunits, against which a triangle's normal is
  // judged to face outward or in. A sum of a model's positions cannot leave a 64-bit accumulator.
  std::array<std::int64_t, 3> sum{0, 0, 0};
  for (const Frontier::ModelVertex& vertex : _model.vertices)
  {
    sum[0] += vertex.x;
    sum[1] += vertex.y;
    sum[2] += vertex.z;
  }
  const double count = static_cast<double>(_model.vertices.size());
  const std::array<double, 3> centroid{static_cast<double>(sum[0]) / count, static_cast<double>(sum[1]) / count,
                                       static_cast<double>(sum[2]) / count};

  std::map<VertexKey, std::uint32_t> emitted;
  mesh.indices.reserve(_model.triangles.size() * 3);
  const std::uint32_t vertexCount = static_cast<std::uint32_t>(_model.vertices.size());
  for (const Frontier::ModelTriangle& triangle : _model.triangles)
  {
    const std::array<std::uint32_t, 3> corners{triangle.a, triangle.b, triangle.c};
    if (corners[0] >= vertexCount || corners[1] >= vertexCount || corners[2] >= vertexCount)
    {
      // ContentValidator is what reports a triangle that indexes past the vertex list; this only
      // declines to read it.
      continue;
    }
    const Frontier::ModelVertex& a = _model.vertices[corners[0]];
    const Frontier::ModelVertex& b = _model.vertices[corners[1]];
    const Frontier::ModelVertex& c = _model.vertices[corners[2]];
    const Direction direction = FaceDirection(a, b, c);
    const double length = std::sqrt(static_cast<double>(direction.value[0]) * static_cast<double>(direction.value[0]) +
                                    static_cast<double>(direction.value[1]) * static_cast<double>(direction.value[1]) +
                                    static_cast<double>(direction.value[2]) * static_cast<double>(direction.value[2]));
    if (length <= 0.0)
    {
      // A triangle with no area covers no pixel; emitting it would only put a normal of nothing in
      // the buffer.
      continue;
    }
    const std::array<float, 3> normal{static_cast<float>(static_cast<double>(direction.value[0]) / length),
                                      static_cast<float>(static_cast<double>(direction.value[1]) / length),
                                      static_cast<float>(static_cast<double>(direction.value[2]) / length)};
    const std::array<double, 3> toCentroid{centroid[0] - (static_cast<double>(a.x) + b.x + c.x) / 3.0,
                                           centroid[1] - (static_cast<double>(a.y) + b.y + c.y) / 3.0,
                                           centroid[2] - (static_cast<double>(a.z) + b.z + c.z) / 3.0};
    if (static_cast<double>(direction.value[0]) * toCentroid[0] + static_cast<double>(direction.value[1]) * toCentroid[1] +
          static_cast<double>(direction.value[2]) * toCentroid[2] >
        0.0)
    {
      ++mesh.inwardFacingTriangles;
    }
    const std::uint32_t color = PackColor(triangle.color);
    for (const std::uint32_t corner : corners)
    {
      const VertexKey key{corner, color, direction};
      const auto found = emitted.find(key);
      if (found != emitted.end())
      {
        mesh.indices.push_back(found->second);
        continue;
      }
      const Frontier::ModelVertex& source = _model.vertices[corner];
      const GeometryVertex vertex{WorldUnits(source.x), WorldUnits(source.y), WorldUnits(source.z), normal[0], normal[1], normal[2], color};
      const std::uint32_t index = static_cast<std::uint32_t>(mesh.vertices.size());
      mesh.vertices.push_back(vertex);
      emitted.emplace(key, index);
      mesh.indices.push_back(index);
      mesh.radiusWorldUnits = std::max(mesh.radiusWorldUnits, std::sqrt(vertex.x * vertex.x + vertex.y * vertex.y + vertex.z * vertex.z));
    }
  }
  return mesh;
}

ModelBuffers::ModelBuffers(GraphicsDevice& _device, std::span<const Frontier::ModelDesc> _models)
{
  std::vector<GeometryVertex> vertices;
  std::vector<std::uint32_t> indices;
  m_ids.reserve(_models.size());
  m_ranges.reserve(_models.size());
  for (const Frontier::ModelDesc& model : _models)
  {
    const ModelMesh mesh = BuildModelMesh(model);
    m_ids.push_back(model.id);
    m_ranges.push_back({static_cast<std::uint32_t>(vertices.size()), static_cast<std::uint32_t>(indices.size()),
                        static_cast<std::uint32_t>(mesh.indices.size()), mesh.radiusWorldUnits});
    vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
    m_inwardFacingTriangles += mesh.inwardFacingTriangles;
  }

  ID3D12Device* device = _device.Device();
  m_vertexBuffer = UploadBuffer(device, vertices.data(), vertices.size() * sizeof(GeometryVertex), L"model vertices");
  m_indexBuffer = UploadBuffer(device, indices.data(), indices.size() * sizeof(std::uint32_t), L"model indices");
  m_vertexView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
  m_vertexView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(GeometryVertex));
  m_vertexView.StrideInBytes = sizeof(GeometryVertex);
  m_indexView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
  m_indexView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
  m_indexView.Format = DXGI_FORMAT_R32_UINT;
  Log::Write(LogLevel::Info, "models: " + std::to_string(m_ranges.size()) + " built, " + std::to_string(vertices.size()) + " vertices, " +
                               std::to_string(indices.size() / 3) + " triangles");
  if (m_inwardFacingTriangles > 0)
  {
    // Not fatal: the model still draws, lit as though from inside. It is said out loud because it
    // means the authoring tool wound a face the other way and nothing else will report it.
    Log::Write(LogLevel::Warning,
               "models: " + std::to_string(m_inwardFacingTriangles) + " triangles wound so that their normal points inward");
  }
}

std::uint32_t ModelBuffers::Find(std::string_view _id) const noexcept
{
  for (std::size_t index = 0; index < m_ids.size(); ++index)
  {
    if (m_ids[index] == _id)
    {
      return static_cast<std::uint32_t>(index);
    }
  }
  return NO_MODEL;
}

} // namespace Neuron
