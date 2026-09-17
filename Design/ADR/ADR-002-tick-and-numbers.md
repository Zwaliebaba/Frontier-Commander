# ADR-002 — The tick, the units and the fixed-point formats

**Status:** Accepted
**Date:** 2026-09-17
**Owner:** the owner for the tick rate (20 Hz, 2026-09-17); the author for the formats, on `Design/TechnicalDesign.md` §3 and §4.1

## Context

The first `Sim` task (`m0-foundation/T15`) writes the tick loop, the state hash and the arithmetic every later system computes in, and `TechnicalDesign.md` §12 asks that the tick rate, the position unit and the fixed-point formats of §4.1 be recorded when it does, with a dated measurement section added when there is a full tick to measure (`ImplementationPlan.md` §6, item 6). `AGENTS.md` R16 says a default is not a decision; this is where the decisions are written down, so that nobody re-derives a unit from a constant they found in a header.

## Decision

**The simulation ticks at 20 Hz**: 50 ms per tick, and the tick is the only clock the simulation has. Every duration in the simulation is a tick count, every rate is per tick, and wall time meets the tick in one place, step 2 of the host loop of `TechnicalDesign.md` §3. `Sim::Advance()` advances the tick counter and the simulation Random, and nothing else does; `Sim::Submit()` draws nothing and moves nothing.

**The units**, as `TechnicalDesign.md` §4.1 tabulates them and `Core` implements them:

| Quantity | Type and unit |
|---|---|
| Position and height | `std::int32_t` in 1/256 world unit (`SUBUNITS_PER_WORLD_UNIT`); a cell is 64 world units, 16,384 subunits, and cell arithmetic is a shift by `SUBUNITS_PER_CELL_SHIFT`, 14 |
| Velocity | subunits per tick; nothing per second ever enters the simulation |
| Angle | `std::uint16_t` binary angle, 65,536 per turn; `Sin` and `Cos` in 16.16 fixed point from the 1,024-entry table of `Core/SinTable.h` with linear interpolation; an exact half turn turns the positive way |
| Distance | the integer square root of an `std::int64_t` squared distance, taken only where a range check needs it |
| Percentages, multipliers, power | `std::int32_t` hundredths, the unit in the name (R6); a seat's stockpile is `powerHundredths`, so 5 power a second at 20 Hz is 25 a tick |
| Time | ticks, `std::uint32_t`; 6.8 years before it wraps |
| Products of two positions | `std::int64_t`, through `MulDiv`, `MulShift`, `Dot`, `LengthSquared` and `Sqrt` in `Core/FixedPoint.h`, never widened by hand |
| The landscape's generation arithmetic | 24.8 fixed point during generation and whole units in the output, the C++ generator copying `Tools/LandscapeTool.py` exactly (`m0-foundation/T16`, T17) |
| The simulation Random | xoshiro128\*\*, one per match, seeded through splitmix64 from the match seed (`Core/Random.h`); sixteen bytes of state, carried whole by every snapshot |

**No floating-point type is named under `Sim/`.** `grep -rn "float\|double" Sim/` is empty and stays empty; `Tests/SimTests` is held to the same rule. The first float in the executable is the client's interpolation between two frames (§3), and it never reaches the simulation.

**The state hash** of stage 13 is FNV-1a 64 (`Core/Hash.h`, every integer fed as little-endian bytes) over, in this order: the tick; the four words of the Random state; every seat's kind, alliance, power and defeated flag; every object kind's map in ascending id order, as the kinds arrive; the last targeting roll; the applied and dropped order counts; the finished flag and the winning alliance. The pending order queue is not state and is left out, so that a match fed its orders early hashes as one fed them on time, which is what lets a replay be fed whole (ADR-003).

**An order** is the fixed record of `Sim/Order.h`: the tick it is for, four `std::int32_t` operands, the seat and the kind, 24 bytes in memory and 22 in a stream; the twenty kinds of §4.7 in that order, and their numbering is the wire's. The host gives an arriving order the next tick; an order for a tick already advanced is moved to the next one rather than dropped, and the queue hands a tick's orders out in seat order then arrival order.

## Consequences

- A 50 ms budget per tick, spent by pathing, visibility and publishing (§3, §4.5, §4.6); the empty tick below is the floor under all of it.
- The 1/256 unit puts a Frontier landscape's 65,536 world units at 16.8 million subunits, inside `std::int32_t` with a hundred times to spare, and puts every product of two positions in `std::int64_t` without exception.
- Changing the tick rate changes every tick-count table in `Content`; changing the subunit changes every position on the wire and in every snapshot; changing the hash order changes every recorded replay's checkpoints. Each is a superseding ADR, not an edit.
- What would reopen the rate: the measurement of a full tick (`m1-vertical-slice/G3`) over budget on the target machine, or the owner's play finding 50 ms of order quantisation too coarse or too fine.

## Measurements

The cost of an empty tick and of the hash of the empty state, as `SimTests::DeterminismTests::TheCostOfAnEmptyTickAndOfTheHashIsMeasured` prints them: 10,000 `Advance()` calls and 10,000 `ComputeHash()` calls over a four-seat match with no orders, timed with `std::chrono::steady_clock` and divided down to integer nanoseconds.

| Build and machine | Empty tick | Hash of the empty state | Date |
|---|---|---|---|
| clang++ 18.1.3 `-O1`, Linux x86-64 on an Intel Xeon at 2.10 GHz, the session container (the native test runner of the `m0-foundation/T11` notes, against stand-in framework headers) | 56 to 62 ns over three runs | 43 to 49 ns | 2026-09-17 |
| MSVC v145 Debug\|x64 on the GitHub `windows-latest` runner | read off CI's `TestResults` artefact once the run that lands this ADR has finished, and written here by `m0-foundation/T15` | | |

Arithmetic, not measurement: 5,000 objects at 10 µs each is the 50 ms budget (§3), and the empty tick is a ten-thousandth of it. The dated section for a full tick, with objects, pathing and visibility in it, is added by `m1-vertical-slice/G3`.
