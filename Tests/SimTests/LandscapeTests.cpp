#include "pch.h"

#include "FractalTable.h"
#include "Landscape.h"
#include "LandscapeGenerator.h"
#include "Sim.h"
#include "Snapshot.h"

#include "Hash.h"
#include "Json.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SimTests
{

namespace
{

// The goldens Tools/LandscapeTool.py wrote (Tests/SimTests/Fixtures/Landscape/README.md): the C++
// is held to them and is never the one that changes them.
std::filesystem::path FixtureDirectory()
{
  return std::filesystem::path(__FILE__).parent_path() / "Fixtures" / "Landscape";
}

std::vector<std::byte> ReadFile(const std::filesystem::path& _path)
{
  std::ifstream in(_path, std::ios::binary);
  if (!in)
  {
    Assert::Fail((L"cannot read " + _path.wstring()).c_str());
  }
  std::ostringstream out;
  out << in.rdbuf();
  const std::string text = out.str();
  std::vector<std::byte> bytes(text.size());
  for (std::size_t index = 0; index < text.size(); ++index)
  {
    bytes[index] = static_cast<std::byte>(text[index]);
  }
  return bytes;
}

std::int64_t IntegerOf(const Neuron::JsonValue& _object, const char* _key)
{
  const Neuron::JsonValue* value = _object.Find(_key);
  if (value == nullptr || !value->IsInteger())
  {
    const std::string message = std::string("the definition lacks the integer '") + _key + "'";
    Assert::Fail(std::wstring(message.begin(), message.end()).c_str());
  }
  return value->AsInteger();
}

std::vector<Frontier::CellPosition> PositionsOf(const Neuron::JsonValue& _object, const char* _key)
{
  std::vector<Frontier::CellPosition> positions;
  const Neuron::JsonValue* list = _object.Find(_key);
  Assert::IsTrue(list != nullptr && list->IsArray());
  positions.reserve(list->Size());
  for (std::size_t index = 0; index < list->Size(); ++index)
  {
    positions.push_back(
      {static_cast<std::uint32_t>(IntegerOf(list->At(index), "cellX")), static_cast<std::uint32_t>(IntegerOf(list->At(index), "cellY"))});
  }
  return positions;
}

/// The definition fixture of a Small seed, field for field from the tool's JSON.
Frontier::LandscapeDefinition LoadDefinition(int _seed)
{
  char name[32];
  std::snprintf(name, sizeof name, "small-%04d.json", _seed);
  const std::vector<std::byte> bytes = ReadFile(FixtureDirectory() / name);
  Neuron::JsonValue root;
  Neuron::JsonError error;
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (!Neuron::ParseJson(text, name, root, error))
  {
    const std::string message = error.ToString();
    Assert::Fail(std::wstring(message.begin(), message.end()).c_str());
  }
  Frontier::LandscapeDefinition definition{};
  definition.version = static_cast<std::uint32_t>(IntegerOf(root, "version"));
  const Neuron::JsonValue* sizeClass = root.Find("sizeClass");
  Assert::IsTrue(sizeClass != nullptr && sizeClass->IsString() && sizeClass->AsString() == "Small");
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.cellsPerSide = static_cast<std::uint32_t>(IntegerOf(root, "cellsPerSide"));
  Assert::AreEqual(static_cast<std::int64_t>(Frontier::SamplesPerSide(definition.cellsPerSide)), IntegerOf(root, "samplesPerSide"));
  Assert::AreEqual(static_cast<std::int64_t>(Frontier::SAMPLE_SPACING_WORLD_UNITS), IntegerOf(root, "sampleSpacingWorldUnits"));
  Assert::AreEqual(static_cast<std::int64_t>(Frontier::OUTSIDE_HEIGHT), IntegerOf(root, "outsideHeight"));
  definition.seed = static_cast<std::uint64_t>(IntegerOf(root, "seed"));
  const Neuron::JsonValue* palette = root.Find("palette");
  Assert::IsTrue(palette != nullptr && palette->IsString());
  definition.palette = std::string(palette->AsString());
  const Neuron::JsonValue* tiles = root.Find("tiles");
  Assert::IsTrue(tiles != nullptr && tiles->IsArray());
  for (std::size_t index = 0; index < tiles->Size(); ++index)
  {
    const Neuron::JsonValue& tile = tiles->At(index);
    Frontier::LandscapeTile out{};
    out.x = static_cast<std::int32_t>(IntegerOf(tile, "x"));
    out.y = static_cast<std::int32_t>(IntegerOf(tile, "y"));
    out.extent = static_cast<std::uint32_t>(IntegerOf(tile, "extent"));
    out.fractalDimensionHundredths = static_cast<std::int32_t>(IntegerOf(tile, "fractalDimensionHundredths"));
    out.amplitude = static_cast<std::int32_t>(IntegerOf(tile, "amplitude"));
    out.desiredHeight = static_cast<std::int32_t>(IntegerOf(tile, "desiredHeight"));
    out.heightShift = static_cast<std::int32_t>(IntegerOf(tile, "heightShift"));
    out.lowlandExponentHundredths = static_cast<std::int32_t>(IntegerOf(tile, "lowlandExponentHundredths"));
    out.method = static_cast<std::uint8_t>(IntegerOf(tile, "method"));
    out.edgeFalloff = static_cast<std::uint32_t>(IntegerOf(tile, "edgeFalloff"));
    definition.tiles.push_back(out);
  }
  definition.starts = PositionsOf(root, "starts");
  definition.deposits = PositionsOf(root, "deposits");
  return definition;
}

std::uint64_t HashOfHeights(std::span<const std::int16_t> _heights)
{
  // The tool hashes the little-endian bytes; HashIntegers feeds every integer as exactly those.
  return Neuron::HashIntegers(Neuron::FNV1A64_OFFSET, _heights);
}

Frontier::MatchSettings TwoSeats()
{
  Frontier::MatchSettings settings{};
  settings.seed = 1;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 2;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.seats[0] = {Frontier::SeatKind::Human, 0};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1};
  return settings;
}

} // namespace

TEST_CLASS(LandscapeTests)
{
public:
  TEST_METHOD(TheTablesAreTheGeneratedOnes)
  {
    static_assert(Frontier::FALLOFF_16_16.size() == 301, "fd 100..400 in hundredths");
    static_assert(Frontier::FALLOFF_16_16[0] == 32768, "2^-1 in 16.16");
    static_assert(Frontier::LOWLAND_EXPONENTS.size() == 4);
    static_assert(Frontier::LOWLAND_POWER_16_16[3][255] == 255 * 65536, "x^1.00 at 255");
    Assert::AreEqual(65536, Frontier::LOWLAND_POWER_16_16[0][1]);
  }

  TEST_METHOD(SeedOneMatchesTheGoldenFieldSampleForSample)
  {
    Frontier::Landscape landscape;
    Assert::IsTrue(landscape.Create(LoadDefinition(1)), L"the definition should generate");
    Assert::AreEqual(513u, landscape.SamplesPerSide());
    Assert::AreEqual(128u, landscape.CellsPerSide());
    const std::vector<std::byte> golden = ReadFile(FixtureDirectory() / "small-0001.heights");
    Assert::AreEqual(static_cast<std::size_t>(513 * 513 * 2), golden.size());
    const std::span<const std::int16_t> heights = landscape.Heights();
    for (std::size_t index = 0; index < heights.size(); ++index)
    {
      const std::int16_t expected = static_cast<std::int16_t>(std::to_integer<std::uint8_t>(golden[index * 2]) |
                                                              (std::to_integer<std::uint8_t>(golden[index * 2 + 1]) << 8));
      if (heights[index] != expected)
      {
        const std::wstring message = L"sample (" + std::to_wstring(index % 513) + L", " + std::to_wstring(index / 513) + L"): got " +
                                     std::to_wstring(heights[index]) + L", the tool wrote " + std::to_wstring(expected);
        Assert::Fail(message.c_str());
      }
    }
  }

  TEST_METHOD(SeedsOneToTwentyHashAsTheToolRecorded)
  {
    const std::vector<std::byte> listing = ReadFile(FixtureDirectory() / "small-hashes.txt");
    std::istringstream lines(std::string(reinterpret_cast<const char*>(listing.data()), listing.size()));
    std::string line;
    int checked = 0;
    while (std::getline(lines, line))
    {
      if (line.empty() || line[0] == '#')
      {
        continue;
      }
      std::istringstream fields(line);
      int seed = 0;
      std::string hash;
      fields >> seed >> hash;
      Frontier::Landscape landscape;
      Assert::IsTrue(landscape.Create(LoadDefinition(seed)));
      const std::uint64_t expected = std::stoull(hash, nullptr, 16);
      if (HashOfHeights(landscape.Heights()) != expected)
      {
        Assert::Fail((L"seed " + std::to_wstring(seed) + L" hashes differently from the tool's record").c_str());
      }
      ++checked;
    }
    Assert::AreEqual(20, checked);
  }

  TEST_METHOD(TheCellGridsMatchABruteForceOverTheWholeField)
  {
    Frontier::Landscape landscape;
    Assert::IsTrue(landscape.Create(LoadDefinition(3)));
    const std::uint32_t side = landscape.SamplesPerSide();
    const std::uint32_t cells = landscape.CellsPerSide();
    int water = 0;
    for (std::uint32_t cellY = 0; cellY < cells; ++cellY)
    {
      for (std::uint32_t cellX = 0; cellX < cells; ++cellX)
      {
        // The tool's analyse: over the 5x5 samples, the lowest and the steepest adjacent step.
        int lowest = 32767;
        int steepest = 0;
        for (std::uint32_t j = 0; j <= 4; ++j)
        {
          for (std::uint32_t i = 0; i <= 4; ++i)
          {
            const int h = landscape.HeightAt(cellX * 4 + i, cellY * 4 + j);
            lowest = std::min(lowest, h);
            if (i > 0)
            {
              steepest = std::max(steepest, std::abs(h - landscape.HeightAt(cellX * 4 + i - 1, cellY * 4 + j)));
            }
            if (j > 0)
            {
              steepest = std::max(steepest, std::abs(h - landscape.HeightAt(cellX * 4 + i, cellY * 4 + j - 1)));
            }
          }
        }
        const Frontier::Landscape::Cell& cell = landscape.CellAt(cellX, cellY);
        Assert::AreEqual(steepest * 100 / 16, static_cast<int>(cell.slopePercent));
        Assert::AreEqual(lowest < 0, (cell.flags & Frontier::Landscape::CELL_WATER) != 0);
        Assert::AreEqual(0, static_cast<int>(cell.obstruction));
        water += lowest < 0 ? 1 : 0;
      }
    }
    Assert::IsTrue(water > 0 && water < static_cast<int>(cells * cells), L"a Small landscape has both land and water");
    Assert::AreEqual(static_cast<std::size_t>(side) * side, landscape.Heights().size());
  }

  TEST_METHOD(ADeltaReplacesTheHeightsRederivesTheCellsAndIsRecorded)
  {
    Frontier::Landscape landscape;
    Assert::IsTrue(landscape.Create(LoadDefinition(2)));
    Frontier::HeightDelta flatten{};
    flatten.x = 100;
    flatten.y = 200;
    flatten.width = 9;
    flatten.height = 9;
    flatten.heights.assign(81, 40);
    Assert::IsTrue(landscape.ApplyDelta(flatten));
    Assert::AreEqual(static_cast<std::int16_t>(40), landscape.HeightAt(104, 204));
    Assert::AreEqual(static_cast<std::int16_t>(40), landscape.HeightAt(108, 208));
    // Cells 25..26 by 50..51 lie wholly inside the flattened rectangle (samples 100..108 by 200..208).
    Assert::AreEqual(0, static_cast<int>(landscape.CellAt(25, 50).slopePercent));
    Assert::AreEqual(0, static_cast<int>(landscape.CellAt(26, 51).slopePercent));
    Assert::AreEqual(0, static_cast<int>(landscape.CellAt(25, 50).flags & Frontier::Landscape::CELL_WATER));
    Assert::AreEqual(static_cast<std::size_t>(1), landscape.Deltas().size());
    Assert::IsTrue(landscape.Deltas()[0] == flatten);

    Frontier::HeightDelta outside{};
    outside.x = 510;
    outside.y = 0;
    outside.width = 9;
    outside.height = 1;
    outside.heights.assign(9, 0);
    Assert::IsFalse(landscape.ApplyDelta(outside), L"a rectangle past the edge is refused");
    Frontier::HeightDelta wrongCount = flatten;
    wrongCount.heights.pop_back();
    Assert::IsFalse(landscape.ApplyDelta(wrongCount), L"a count that does not match the rectangle is refused");
    Assert::AreEqual(static_cast<std::size_t>(1), landscape.Deltas().size());

    Frontier::Landscape restored;
    Assert::IsTrue(restored.Restore(landscape.Definition(), landscape.Deltas()));
    Assert::IsTrue(std::vector<std::int16_t>(restored.Heights().begin(), restored.Heights().end()) ==
                   std::vector<std::int16_t>(landscape.Heights().begin(), landscape.Heights().end()));
    Assert::IsTrue(std::vector<Frontier::Landscape::Cell>(restored.Cells().begin(), restored.Cells().end()) ==
                   std::vector<Frontier::Landscape::Cell>(landscape.Cells().begin(), landscape.Cells().end()));
  }

  TEST_METHOD(TheSnapshotCarriesTheDefinitionAndTheDeltasNotTheSamples)
  {
    Frontier::Sim sim(TwoSeats());
    Assert::IsFalse(sim.Terrain().Created());
    Assert::IsTrue(sim.CreateLandscape(LoadDefinition(4)));
    Assert::IsTrue(sim.Terrain().Created());
    sim.Advance();
    Frontier::HeightDelta flatten{};
    flatten.x = 40;
    flatten.y = 40;
    flatten.width = 5;
    flatten.height = 5;
    flatten.heights.assign(25, 12);
    Assert::IsTrue(sim.FlattenTerrain(flatten));
    sim.Advance();
    const std::vector<std::byte> bytes = Frontier::Snapshot::Write(sim);
    Assert::IsTrue(bytes.size() < 4096, L"the snapshot carries the definition and the deltas, not 526 KB of samples");
    std::optional<Frontier::Sim> reloaded = Frontier::Snapshot::Read(bytes);
    if (!reloaded.has_value())
    {
      Assert::Fail(L"the snapshot did not read back");
    }
    Assert::IsTrue(reloaded->Terrain().Created());
    Assert::IsTrue(reloaded->Terrain().Definition() == sim.Terrain().Definition());
    Assert::AreEqual(static_cast<std::size_t>(1), reloaded->Terrain().Deltas().size());
    Assert::IsTrue(std::vector<std::int16_t>(reloaded->Terrain().Heights().begin(), reloaded->Terrain().Heights().end()) ==
                   std::vector<std::int16_t>(sim.Terrain().Heights().begin(), sim.Terrain().Heights().end()));
    Assert::AreEqual(sim.Hash(), reloaded->Hash());
    Assert::AreEqual(sim.ComputeHash(), reloaded->ComputeHash());
    reloaded->Advance();
    sim.Advance();
    Assert::AreEqual(sim.Hash(), reloaded->Hash());

    Frontier::Sim other(TwoSeats());
    Assert::IsTrue(other.CreateLandscape(LoadDefinition(5)));
    Assert::AreNotEqual(sim.ComputeHash(), other.ComputeHash(), L"the landscape reaches the hash");
  }

  TEST_METHOD(AnInvalidTileIsRefused)
  {
    Frontier::LandscapeDefinition definition = LoadDefinition(1);
    definition.tiles[0].extent = 500; // not a power of two
    Frontier::Landscape landscape;
    Assert::IsFalse(landscape.Create(definition));
    Assert::IsFalse(landscape.Created());
    definition = LoadDefinition(1);
    definition.tiles[1].lowlandExponentHundredths = 75; // not in the table
    Assert::IsFalse(Frontier::LandscapeGenerator::IsValid(definition.tiles[1]));
    definition = LoadDefinition(1);
    definition.tiles[2].fractalDimensionHundredths = 401;
    Assert::IsFalse(Frontier::LandscapeGenerator::IsValid(definition.tiles[2]));
  }
};

} // namespace SimTests
