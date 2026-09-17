"""xoshiro128** and splitmix64 in pure Python, as the simulation stream will be in Core (TechnicalDesign.md §4.2).

The landscape tool draws every random number from this generator so that the C++ port
(m0-foundation/T8, T17) can be checked against it bit for bit. Written from the public-domain
reference at https://prng.di.unimi.it/ ; no code was copied.

    python3 Tools/Xoshiro.py          # prints the test vectors the C++ tests pin
"""
from __future__ import annotations

MASK32 = 0xFFFFFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
GOLDEN_GAMMA = 0x9E3779B97F4A7C15


def rotl32(x: int, k: int) -> int:
    return ((x << k) | (x >> (32 - k))) & MASK32


def splitmix64_next(state: int) -> tuple[int, int]:
    """One step of splitmix64: returns (new state, output)."""
    state = (state + GOLDEN_GAMMA) & MASK64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return state, z ^ (z >> 31)


def derive_seed(seed64: int, index: int) -> int:
    """A 64-bit seed for stream `index` of match seed `seed64`: one splitmix64 step from seed + index * gamma."""
    return splitmix64_next((seed64 + index * GOLDEN_GAMMA) & MASK64)[1]


class Xoshiro128StarStar:
    """xoshiro128**: 16 bytes of state, 32-bit output; seeded from a 64-bit seed through two splitmix64 steps."""

    def __init__(self, seed64: int) -> None:
        state = seed64 & MASK64
        state, a = splitmix64_next(state)
        state, b = splitmix64_next(state)
        self.s = [a & MASK32, (a >> 32) & MASK32, b & MASK32, (b >> 32) & MASK32]

    def next_u32(self) -> int:
        s0, s1, s2, s3 = self.s
        result = (rotl32((s1 * 5) & MASK32, 7) * 9) & MASK32
        t = (s1 << 9) & MASK32
        s2 ^= s0
        s3 ^= s1
        s1 ^= s2
        s0 ^= s3
        s2 ^= t
        s3 = rotl32(s3, 11)
        self.s = [s0, s1, s2, s3]
        return result

    def below(self, n: int) -> int:
        """Uniform in [0, n) by rejection, so that no value is favoured; n in 1..2^32."""
        if n <= 0 or n > (1 << 32):
            raise ValueError("below(n) needs 1 <= n <= 2^32")
        threshold = ((1 << 32) - n) % n
        while True:
            r = self.next_u32()
            if r >= threshold:
                return r % n

    def state(self) -> tuple[int, int, int, int]:
        return tuple(self.s)  # type: ignore[return-value]


def _vectors() -> None:
    print("splitmix64 from seed 0:", ", ".join(f"0x{v:016x}" for v in _splitmix_run(0, 3)))
    print("splitmix64 from seed 1:", ", ".join(f"0x{v:016x}" for v in _splitmix_run(1, 3)))
    for seed in (0, 1, 42, 0xDEADBEEF):
        rng = Xoshiro128StarStar(seed)
        print(f"xoshiro128** seed {seed}: state {tuple(f'0x{v:08x}' for v in rng.state())}")
        print("  next_u32 x10:", ", ".join(f"0x{rng.next_u32():08x}" for _ in range(10)))
        rng = Xoshiro128StarStar(seed)
        print("  below(6) x10:", ", ".join(str(rng.below(6)) for _ in range(10)))
    print("derive_seed(1, 0..3):", ", ".join(f"0x{derive_seed(1, i):016x}" for i in range(4)))


def _splitmix_run(seed: int, count: int) -> list[int]:
    out = []
    state = seed
    for _ in range(count):
        state, value = splitmix64_next(state)
        out.append(value)
    return out


if __name__ == "__main__":
    _vectors()
