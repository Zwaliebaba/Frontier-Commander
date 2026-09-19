#pragma once

#include "ContentTree.h"

#include <cstdint>

// The digest of the tables the simulation reads (OpenQuestions.md Q20, owner 2026-09-18). Two
// hosts agree on the rules or they do not, and a snapshot reloaded against tables it was not
// taken against is a different match: the hash is what turns either into a refusal rather than a
// divergence nobody notices until the state hashes part.
//
// IT COVERS WHAT THE SIMULATION READS AND NOTHING ELSE: components, structures, research and the
// damage matrix. Biomes, models and sounds are the client's and change no outcome, so a player
// with different art plays the same match; m3-multiplayer/T3's join check may want a wider digest
// over the whole tree, and this is not it.
//
// Every field of every row is fed, in the tree's own order (ContentTree.h keeps the tables in a
// stable order for exactly this). A field the digest misses is two different rule sets that hash
// alike, which is the failure this exists to prevent, so ContentHashTests moves one field of every
// table and each mutation must move the digest.

namespace Frontier
{

[[nodiscard]] std::uint64_t ContentHash(const ContentTree& _tree) noexcept;

} // namespace Frontier
