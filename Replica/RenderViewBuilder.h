#pragma once

#include "ModelComposer.h"
#include "Picking.h"
#include "Replica.h"

#include "RenderView.h"

#include <cstdint>
#include <map>
#include <span>
#include <vector>

// The replica turned into a plain list of what to draw (TechnicalDesign.md §6.3;
// m1-vertical-slice/R2). This is the seam the whole layering rests on: Client draws a RenderView
// and never names a Device, so "the engine does not know the game" (AGENTS.md R9) is structural
// rather than a promise, and the executable holds no logic worth a test.
//
// WHAT IT FILLS AND WHAT IT DOES NOT. Devices, structures and wrecks as instances; the chunks this
// frame's structures flattened; the commander's fog. NOT features and NOT projectiles: a feature's
// `design` indexes a table Content does not have and nothing anywhere says what a shot looks like,
// so m1-vertical-slice/C7 and C8 own those and each carries its own change to this file
// (Design/Interface.md §11 rows 12 and 13). Leaving them out is the honest state; drawing a
// placeholder for them would be a decision about content taken in a renderer.
//
// THE CHANGED CHUNKS ARE COMPUTED HERE AND NOT RECEIVED, and that is a security boundary rather
// than a convenience. Net/Records.h says it plainly: "a snapshot carries the landscape's flatten
// deltas and the wire must not, because the terrain under an unscouted base is not public"
// (TechnicalDesign.md §5.2). A client generates the landscape from the definition it was given at
// the join and flattens it under the structures it can SEE - which is exactly why StructureState
// carries `y`, "the flattened height under the footprint". So a commander who has not scouted an
// enemy base has terrain that is still the landscape's own, on his own machine, and no amount of
// reading his memory says otherwise.
//
// IT REMEMBERS THE STRUCTURES IT HAS FLATTENED, which is the one piece of state here. The replica
// says what structures there ARE and not which arrived this frame, and re-flattening every
// structure every frame would rebuild every chunk of a base on every frame of the match. The
// builder keeps what it last flattened and reports the difference.

namespace Frontier
{

/// The chunks a footprint standing at (_cellX, _cellY) touches, appended to _outChunks in row-major
/// chunk order. A structure on a chunk boundary touches two and one on a corner four, which is why
/// this is a rectangle of chunks and not one of them.
///
/// FREE, BECAUSE IT IS THE ONLY ARITHMETIC HERE WORTH A TEST. Building the rest of a render view
/// needs a Replica, which needs a Net::Client, which needs a host to have sent it a JoinAccepted -
/// the whole stack, which is what Tests/ReplicaTests/ConvergenceTests.cpp already stands up. This
/// is the part that is wrong by one chunk at an edge, and it can be checked with two integers.
void ChunksOfFootprint(std::uint32_t _cellX, std::uint32_t _cellY, std::uint32_t _footprintCellsX, std::uint32_t _footprintCellsY,
                       std::uint32_t _cellsPerSide, std::uint32_t _chunkCells, std::vector<std::uint32_t>& _outChunks);

/// The two numbers the builder cannot get from the replica or from Content.
struct RenderViewSettings
{
  /// The landscape's, for turning a cell into a chunk index.
  std::uint32_t cellsPerSide = 0;
  /// Client/TerrainChunk.h's CHUNK_CELLS. PASSED AND NOT NAMED: it is a rendering decision and
  /// Replica may not include Client, so a copy of the constant here would be a second definition
  /// that nothing makes agree with the first.
  std::uint32_t chunkCells = 32;
};

/// What the builder produced beside the render view: the things a click can land on, built from the
/// same walk rather than from a second one. Wrecks are not here - §5 selects none of them - and
/// neither are features or projectiles.
struct PickSet
{
  std::vector<PickCandidate> candidates;
};

class RenderViewBuilder
{
public:
  /// _content and _composer outlive the builder. The tree is taken directly rather than reached
  /// through the composer: what is wanted from it is a structure's FOOTPRINT, which is a content
  /// row and not a model, and a composer that handed out its whole tree would be a second way to
  /// read content that nobody could see the callers of.
  RenderViewBuilder(const ContentTree& _content, const ModelComposer& _composer, const RenderViewSettings& _settings);

  /// Fills _outView and _outPicks from _replica at _renderTime, both cleared first. _selectedIds is
  /// what the commander has selected, ascending; a handful of ids, so a linear scan.
  void Build(const Replica& _replica, std::int64_t _renderTime, std::span<const std::uint32_t> _selectedIds, Neuron::RenderView& _outView,
             PickSet& _outPicks);

  /// Objects the walk could not draw: a device whose design never arrived, a wreck of a design the
  /// content does not have. Counted rather than thrown - a client that stopped drawing because one
  /// object was unresolvable would be worse than one that draws the other four thousand.
  [[nodiscard]] std::uint32_t LastUnresolvedObjects() const noexcept
  {
    return m_lastUnresolved;
  }

  /// The landscape's size is not known when a match is built - Sim is what validates and holds the
  /// landscape - so the settings can arrive afterwards. Setting them forgets the flattened terrain
  /// too: a chunk index computed against another landscape means nothing.
  void Settings(const RenderViewSettings& _settings);

  /// Forgets every structure it has flattened, so that the next Build reports them all again. What
  /// a full frame needs: the replica cleared itself, and a chunk list that assumed otherwise would
  /// leave the terrain under a rejoined commander's own base flat-looking and never rebuilt.
  void ForgetTerrain();

private:
  /// What was flattened under one structure, and therefore what has to change when it is gone.
  struct Flattened
  {
    std::uint16_t cellX = 0;
    std::uint16_t cellY = 0;
    std::uint32_t footprintCellsX = 0;
    std::uint32_t footprintCellsY = 0;
    std::int32_t y = 0;

    [[nodiscard]] constexpr bool operator==(const Flattened&) const noexcept = default;
  };

  void MarkChunks(const Flattened& _footprint, std::vector<std::uint32_t>& _outChunks) const;

  const ContentTree* m_content;
  const ModelComposer* m_composer;
  RenderViewSettings m_settings;
  std::map<std::uint32_t, Flattened> m_flattened;
  std::uint32_t m_lastUnresolved = 0;
};

} // namespace Frontier
