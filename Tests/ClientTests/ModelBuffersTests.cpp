#include "pch.h"

#include "ModelBuffers.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <tuple>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The loader's split (m1-vertical-slice/K1; ADR-011). What is under test is the rule the pass
// depends on and the shader cannot check: one vertex per (description vertex, triangle colour,
// triangle normal), the normal the SDK's outward one rather than the textbook's, and the positions
// in world units rather than in the subunits Content stores. The box the tests build is the one
// Tools/MakePlaceholderModels.py writes, corner order and winding included, so that what passes
// here is what GameData\Models holds.
namespace ClientTests
{

namespace
{

constexpr float TOLERANCE = 1e-4f;
constexpr std::int32_t HALF_LENGTH = 512; // two world units
constexpr std::int32_t HALF_WIDTH = 256;  // one
constexpr std::int32_t HEIGHT = 1024;     // four
constexpr std::array<std::uint8_t, 4> GRAY = {130, 128, 120, 255};
constexpr std::array<std::uint8_t, 4> RUST = {160, 70, 40, 255};
constexpr std::array<std::uint8_t, 4> SLOT = {0, 0, 0, 0}; ///< The team-colour slot: alpha zero

/// The generator's six faces, in its order: top, bottom, -z, +z, +x, -x.
constexpr std::array<std::array<std::uint16_t, 4>, 6> FACES = {{
  {4, 5, 6, 7},
  {3, 2, 1, 0},
  {0, 1, 5, 4},
  {2, 3, 7, 6},
  {1, 2, 6, 5},
  {3, 0, 4, 7},
}};

/// A box with one colour per triangle, twelve of them, so that a test can colour the two triangles
/// of a face differently and see what the split does.
[[nodiscard]] Frontier::ModelDesc BoxModel(const std::array<std::array<std::uint8_t, 4>, 12>& _colors, bool _reversed = false)
{
  Frontier::ModelDesc model{};
  model.version = Frontier::MODEL_DESC_VERSION;
  model.id = "Box";
  model.vertices = {
    {-HALF_LENGTH, 0, -HALF_WIDTH},    {HALF_LENGTH, 0, -HALF_WIDTH},       {HALF_LENGTH, 0, HALF_WIDTH},
    {-HALF_LENGTH, 0, HALF_WIDTH},     {-HALF_LENGTH, HEIGHT, -HALF_WIDTH}, {HALF_LENGTH, HEIGHT, -HALF_WIDTH},
    {HALF_LENGTH, HEIGHT, HALF_WIDTH}, {-HALF_LENGTH, HEIGHT, HALF_WIDTH},
  };
  std::size_t triangle = 0;
  for (const std::array<std::uint16_t, 4>& face : FACES)
  {
    for (const std::array<std::uint16_t, 3>& corners :
         {std::array<std::uint16_t, 3>{face[0], face[1], face[2]}, std::array<std::uint16_t, 3>{face[0], face[2], face[3]}})
    {
      // Reversed swaps the second and third corner, which is a face wound the other way and the
      // one data fault a baked normal can report.
      model.triangles.push_back(
        {corners[0], _reversed ? corners[2] : corners[1], _reversed ? corners[1] : corners[2], _colors[triangle], 0});
      ++triangle;
    }
  }
  return model;
}

[[nodiscard]] std::array<std::array<std::uint8_t, 4>, 12> SixColors()
{
  std::array<std::array<std::uint8_t, 4>, 12> colors{};
  for (std::size_t face = 0; face < 6; ++face)
  {
    // A colour per face, distinct by its red channel, both triangles of the face alike.
    const std::array<std::uint8_t, 4> color = {static_cast<std::uint8_t>(10 * (face + 1)), 128, 120, 255};
    colors[face * 2] = color;
    colors[face * 2 + 1] = color;
  }
  return colors;
}

[[nodiscard]] std::array<std::array<std::uint8_t, 4>, 12> OneColor(const std::array<std::uint8_t, 4>& _color)
{
  std::array<std::array<std::uint8_t, 4>, 12> colors{};
  colors.fill(_color);
  return colors;
}

/// The normal of the triangle the _triangle-th three indices name.
[[nodiscard]] std::array<float, 3> NormalOf(const Neuron::ModelMesh& _mesh, std::size_t _triangle)
{
  const Neuron::GeometryVertex& vertex = _mesh.vertices[_mesh.indices[_triangle * 3]];
  return {vertex.nx, vertex.ny, vertex.nz};
}

void AssertNormal(const std::array<float, 3>& _expected, const std::array<float, 3>& _actual, const wchar_t* _message)
{
  Assert::AreEqual(_expected[0], _actual[0], TOLERANCE, _message);
  Assert::AreEqual(_expected[1], _actual[1], TOLERANCE, _message);
  Assert::AreEqual(_expected[2], _actual[2], TOLERANCE, _message);
}

} // namespace

TEST_CLASS(ModelBuffersTests)
{
public:
  TEST_METHOD(EveryCornerSplitsOncePerFaceItBelongsTo)
  {
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(BoxModel(SixColors()));
    // Eight corners, each in three faces, and the two triangles of a face agree on colour and
    // normal so they share: 24 rather than the 36 a blind triplication would write.
    Assert::AreEqual(std::size_t{24}, mesh.vertices.size());
    Assert::AreEqual(std::size_t{36}, mesh.indices.size());
  }

  TEST_METHOD(TheTwoTrianglesOfAFaceShareTheirVerticesEvenWhenEveryFaceIsOneColor)
  {
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(BoxModel(OneColor(GRAY)));
    // One colour throughout, so only the normal splits, and it splits by face: still 24.
    Assert::AreEqual(std::size_t{24}, mesh.vertices.size());
  }

  TEST_METHOD(ADifferentColorOnTheSameFaceSplitsThatFace)
  {
    std::array<std::array<std::uint8_t, 4>, 12> colors = OneColor(GRAY);
    colors[0] = RUST; // the top face's first triangle alone
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(BoxModel(colors));
    // The top face's four vertices become six: three for each triangle, sharing nothing, because a
    // flat colour reaches the pixel shader from the triangle's own first vertex.
    Assert::AreEqual(std::size_t{26}, mesh.vertices.size());
  }

  TEST_METHOD(TheOutwardNormalUsesTheSdkCrossNotTheTextbookOne)
  {
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(BoxModel(SixColors()));
    // The face order of the generator: top, bottom, -z, +z, +x, -x, two triangles each. This is
    // the whole of ADR-011's handedness in one assertion; get it backwards and every model is lit
    // from inside and comes out black.
    AssertNormal({0.0f, 1.0f, 0.0f}, NormalOf(mesh, 0), L"the top face faces up");
    AssertNormal({0.0f, -1.0f, 0.0f}, NormalOf(mesh, 2), L"the bottom face faces down");
    AssertNormal({0.0f, 0.0f, -1.0f}, NormalOf(mesh, 4), L"the -z face faces -z");
    AssertNormal({0.0f, 0.0f, 1.0f}, NormalOf(mesh, 6), L"the +z face faces +z");
    AssertNormal({1.0f, 0.0f, 0.0f}, NormalOf(mesh, 8), L"the +x face faces +x");
    AssertNormal({-1.0f, 0.0f, 0.0f}, NormalOf(mesh, 10), L"the -x face faces -x");
  }

  TEST_METHOD(BothTrianglesOfAFaceCarryThatFaceNormal)
  {
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(BoxModel(SixColors()));
    for (std::size_t face = 0; face < 6; ++face)
    {
      AssertNormal(NormalOf(mesh, face * 2), NormalOf(mesh, face * 2 + 1), L"a face is one plane");
    }
    std::set<std::tuple<float, float, float>> normals;
    for (const Neuron::GeometryVertex& vertex : mesh.vertices)
    {
      normals.emplace(vertex.nx, vertex.ny, vertex.nz);
    }
    Assert::AreEqual(std::size_t{6}, normals.size(), L"six faces, six normals");
  }

  TEST_METHOD(NoTriangleOfAClosedSolidFacesInward)
  {
    Assert::AreEqual(std::uint32_t{0}, Neuron::BuildModelMesh(BoxModel(SixColors())).inwardFacingTriangles);
    // Wound the other way, every one of the twelve does, which is what the count is for: a model
    // the authoring tool wound backwards says so as a number instead of as a dark face.
    Assert::AreEqual(std::uint32_t{12}, Neuron::BuildModelMesh(BoxModel(SixColors(), true)).inwardFacingTriangles);
  }

  TEST_METHOD(PositionsBecomeWorldUnits)
  {
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(BoxModel(SixColors()));
    float lowest = 0.0f;
    float highest = 0.0f;
    for (const Neuron::GeometryVertex& vertex : mesh.vertices)
    {
      lowest = std::min(lowest, vertex.y);
      highest = std::max(highest, vertex.y);
      Assert::AreEqual(2.0f, std::fabs(vertex.x), TOLERANCE, L"half a length is two world units");
      Assert::AreEqual(1.0f, std::fabs(vertex.z), TOLERANCE, L"half a width is one");
    }
    Assert::AreEqual(0.0f, lowest, TOLERANCE);
    Assert::AreEqual(4.0f, highest, TOLERANCE, L"1024 subunits is four world units");
  }

  TEST_METHOD(TheTeamColorSlotKeepsItsZeroAlpha)
  {
    std::array<std::array<std::uint8_t, 4>, 12> colors = OneColor(GRAY);
    colors[0] = SLOT;
    colors[1] = SLOT;
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(BoxModel(colors));
    std::size_t flagged = 0;
    for (const Neuron::GeometryVertex& vertex : mesh.vertices)
    {
      if ((vertex.color >> 24) == 0)
      {
        ++flagged;
      }
    }
    // The top face's four corners, and nothing else: the alpha survives the split, and it is what
    // the pixel shader tests to write the seat's colour unlit.
    Assert::AreEqual(std::size_t{4}, flagged);
  }

  TEST_METHOD(TheRadiusReachesTheFurthestCorner)
  {
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(BoxModel(SixColors()));
    Assert::AreEqual(std::sqrt(4.0f + 16.0f + 1.0f), mesh.radiusWorldUnits, TOLERANCE, L"a top corner, from the model's origin");
  }

  TEST_METHOD(AModelWithNothingInItBuildsAnEmptyMesh)
  {
    Frontier::ModelDesc model{};
    model.version = Frontier::MODEL_DESC_VERSION;
    model.id = "Nothing";
    const Neuron::ModelMesh empty = Neuron::BuildModelMesh(model);
    Assert::AreEqual(std::size_t{0}, empty.vertices.size());
    Assert::AreEqual(std::size_t{0}, empty.indices.size());
    Assert::AreEqual(0.0f, empty.radiusWorldUnits, TOLERANCE);

    model.vertices = {{0, 0, 0}, {256, 0, 0}, {0, 256, 0}};
    Assert::AreEqual(std::size_t{0}, Neuron::BuildModelMesh(model).vertices.size(), L"vertices without triangles draw nothing");
  }

  TEST_METHOD(ATriangleWithNoAreaOrNoSuchVertexIsDropped)
  {
    Frontier::ModelDesc model{};
    model.version = Frontier::MODEL_DESC_VERSION;
    model.id = "Broken";
    model.vertices = {{0, 0, 0}, {256, 0, 0}, {0, 0, 256}};
    model.triangles = {
      {0, 1, 2, GRAY, 0},
      {0, 1, 1, GRAY, 0}, // no area, so no normal worth writing
      {0, 1, 9, GRAY, 0}, // past the end of the vertex list; ContentValidator is what reports it
    };
    const Neuron::ModelMesh mesh = Neuron::BuildModelMesh(model);
    Assert::AreEqual(std::size_t{3}, mesh.indices.size(), L"one triangle survives");
    Assert::AreEqual(std::size_t{3}, mesh.vertices.size());
  }
};

} // namespace ClientTests
