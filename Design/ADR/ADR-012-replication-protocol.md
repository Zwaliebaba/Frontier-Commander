# ADR-012 — The replication protocol

**Status:** accepted, 2026-09-19 (`m1-vertical-slice/N2`)
**Supersedes:** nothing. **Superseded by:** nothing.

## Context

The owner chose host-authoritative state replication on 2026-09-17 (`OpenQuestions.md` Q2): only the
host simulates, and every client holds a replica of what its commander is entitled to know.
`TechnicalDesign.md` §5 wrote that model down — an interest set per client, delta frames against an
acknowledged baseline, fragments for a frame larger than a datagram, a reliable stream for orders —
and left the numbers it rests on to be measured by the task that built it. This is that record.

Two things make the numbers matter rather than merely fit. The first is that the fog of war is a
**security** property here and not a bandwidth one: `GameDesign.md` §10 says a modified client must
see nothing an honest one does, so the filter has to be on the host and has to be the only door a
record can reach a frame through. The second is that a delta protocol has exactly one failure mode
worth designing around — a client whose acknowledged baseline the host no longer holds — and the
cost of avoiding it is the history length chosen below.

## Decision

**Interest per client per publish** (`Net/Interest.h`), from the fog grids of `m1-vertical-slice/S9`:

- every object in a cell the commander's **alliance** currently sees — allies share vision
  (`GameDesign.md` §2), so it is a union over the alliance's grids rather than a grid of its own;
- every object the commander **owns**, wherever it stands, because a commander is never in the dark
  about his own army;
- a **ghost** for every structure he has seen and cannot see now, at its last-seen state, from the
  ghost store of §4.6. A structure he can see is sent at its true state and is never also a ghost;
- his **own** `SeatState` and his **own** fog, as runs of cells that changed since the last frame he
  was sent. No other seat's power, research or fog ever reaches him;
- at the join, the match settings and the **landscape definition**.

**The one leak this model keeps** is that definition, and §5.2 names it rather than hiding it: the
seed, the size class, the tile list, the start positions and the deposits go to every client,
because they are known to every commander from the first tick in most strategy games. **The flatten
deltas do not.** A structure's footprint reaches a client only inside that structure's own record or
its ghost, so the ground under an unscouted base stays as the generator made it.

**The numbers.**

| | Value | Why this one |
|---|---|---|
| Publish rate | every second tick, 10 Hz | `TechnicalDesign.md` §4.8, stage 14. The replica interpolates between frames, so the rate sets latency and not smoothness |
| History per client | **32 frames**, 3.2 seconds | A client that has acknowledged nothing for 3.2 seconds has lost more than thirty consecutive datagrams, which is a link that is down rather than lossy. The cost is 32 copies of that client's view — 600 KB at 600 visible objects, measured below |
| Position quantisation | a **quarter of a world unit** (64 subunits), floor division | §5.3. Floor rather than truncation so the step is uniform across zero; the value comes back as the middle of the quarter, so the round trip errs half a step either way rather than a whole step downwards |
| Heading | the **high byte** of a binary angle — 256 headings, 1.4° apart | Finer than a model's silhouette resolves at any camera height |
| Changed device record | **14 bytes plus a one-byte mask** | §5.7's arithmetic, pinned by a `static_assert`. Four of id, six of position delta, one of heading, two of hit points and one of order-and-stances |
| Fragment payload | **1,189 bytes** — the 1,200-byte datagram payload less the Fragment header | §5.3's 1,200 is under every common path MTU. A frame at or under it travels whole, with no fragment header at all |
| Fragments per frame | at most **64** | 75 KB, which is four times the largest frame measured below; past it is a bug rather than a big battle, and nothing is sent |
| Rejoin grace | `MatchSettings::rejoinGraceTicks`, default **2,400 ticks** (two minutes) | `GameDesign.md` §10 asks for a grace period and gives no number. A lobby setting, because how long a match waits for a player is the host's decision and not the protocol's |

**A lost frame costs nothing but a larger next frame.** There is no retransmission and no ordering:
the host encodes against the newest frame the client has acknowledged, and a client whose baseline
has aged out of the history gets a full frame — the same path a joining client takes, so there is
one path rather than two. A frame with a fragment missing is discarded rather than waited for.

**The acknowledgement rides every datagram the client sends, and a SKIPPED frame forces another
one.** That second half is not in §5.3 and the code needed it: when a client's acknowledgement is
lost after it applied a frame, the host goes on encoding against the older baseline and the client
goes on discarding what arrives, and the two talk past each other until something breaks the tie. A
frame discarded for a baseline mismatch is evidence the host's idea of this client is stale, so it
is what makes the client say so again.

**Orders are the one reliable stream** (§5.5), and its round-trip estimate follows **Karn's rule**:
an order that was sent twice measures no round trip, because there is no telling which copy the
acknowledgement answers. The estimate is held in eighths of a tick, because a smoothed average over
eight samples held in whole ticks cannot move for a sample within eight of it.

**The host checks an order's SHAPE and the simulation checks the rules.** "He is not that
commander" is a client lying about who it is and never reaches the simulation; "he cannot afford it"
is stage 1's judgement and belongs in the rejection list the seat carries.

## The bytes, measured

`NetTests::HostTests::TheBytesAFrameTakesAtAHundredAndAtSixHundredObjects` builds a match with the
stated number of devices visible to one commander, measures the full frame it joins on, then moves
half of them one step and measures the delta. Measured on 2026-09-19:

| Visible objects | Full frame | Delta, half of them moving | At 10 Hz |
|---|---|---|---|
| 100 | 3,653 bytes | 637 bytes | 6 KB/s |
| 600 | 19,153 bytes | 3,387 bytes | 33 KB/s |

§5.7's arithmetic predicted about 24 KB for a full frame of 600 and 4.2 KB for a delta with 300
changing; the measurements come in a fifth under both, because a device that did not move sends
nothing at all and a changed one sends only the fields its mask names. **The peak stands as §5.7
put it: 30 to 70 KB/s a client, and about 0.6 MB/s out of a host with eight.**

A full frame of 600 objects is 19 KB, so the 32-frame history is **600 KB a client** at that size,
4.8 MB for a host with eight commanders in the largest battle any of them can see. That is the price
of never having to retransmit, and it is the figure to reopen this decision against.

## Consequences

- **A client can be lied to about nothing it cannot see, by construction.** The encoder reads the
  simulation only for ids the interest set names, so the only way a record of an unseen object could
  reach a frame is through a set that named it — which is what
  `NetTests::InterestTests::NoFrameOfAScriptedMatchEverNamesWhatItsCommanderCannotSee` asserts over a
  whole match rather than over one frame. **Any new record type must be added to the interest set
  before the encoder can name it**, and a record added to a frame without one is the bug this
  arrangement exists to make impossible.
- **The per-client fog copy is O(cells).** A Frontier landscape is 1,048,576 cells, so a client's
  copy is 1 MB and eight are 8 MB. ADR-008 already names that cost for the simulation's own grids;
  this adds one copy a client on the host. A sparse form would reopen both at once, and only a
  measurement on a Frontier landscape justifies it.
- **The protocol version is `NET_PROTOCOL_VERSION`, and it is separate from the snapshot's.** A
  snapshot that gains a field bumps `SNAPSHOT_VERSION` and leaves the wire alone; a wire that shared
  its writer would break every client of a host that could save. The two write the same settings and
  the same landscape definition twice, on purpose.
- **Dominance and spectating are not addressed.** A spectator is a client with an interest set of
  everything, which this shape allows and nothing here implements.
