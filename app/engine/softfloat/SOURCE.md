# SoftFloat 3e (a subset)

Berkeley SoftFloat, Release 3e, by John R. Hauser: IEEE binary64 arithmetic in integer code, for `f64.c`.

- Source: <http://www.jhauser.us/arithmetic/SoftFloat-3e.zip>
- SHA-256 of the archive: `21130ce885d35c1fe73fc1e1bf2244178167e05c6747cad5f450cc991714c746`
- Licence: BSD 3-clause, `LICENSE` (the archive's `COPYING.txt`, unchanged)

## The files

Every file below is byte-identical to the archive's (`SoftFloat-3e/` + the path given). The only file of ours is
`platform.h`, the configuration header each of SoftFloat's `build/<target>/` directories supplies.

| Files | From |
| --- | --- |
| `softfloat.h`, `softfloat_types.h`, `internals.h`, `primitives.h`, `primitiveTypes.h` | `source/include/` |
| `specialize.h`, `s_propagateNaNF64UI.c`, `softfloat_raiseFlags.c` | `source/ARM-VFPv2/` |
| `f64_add.c`, `f64_sub.c`, `f64_mul.c`, `f64_div.c`, `f64_lt.c`, `f64_le.c`, `f64_eq.c` | `source/` |
| `i64_to_f64.c`, `ui64_to_f64.c`, `f64_to_i64_r_minMag.c`, `f64_roundToInt.c`, `softfloat_state.c` | `source/` |
| `s_addMagsF64.c`, `s_subMagsF64.c`, `s_roundPackToF64.c`, `s_normRoundPackToF64.c` | `source/` |
| `s_normSubnormalF64Sig.c`, `s_shiftRightJam64.c`, `s_shortShiftRightJam64.c` | `source/` |
| `s_countLeadingZeros64.c`, `s_countLeadingZeros8.c` | `source/` |
| `s_approxRecip32_1.c`, `s_approxRecip_1Ks.c` | `source/` |

That is what `f64_add`, `f64_sub`, `f64_mul`, `f64_div`, `f64_lt`, `f64_le`, `f64_eq`, `i64_to_f64`,
`ui64_to_f64`, `f64_to_i64_r_minMag` and `f64_roundToInt` need, and no more (the host tests link without any other),
but for `softfloat_mul64To128`, which is ours (below). The firmware build's register check skips this directory
(fm1.toml `vendored`): SoftFloat's hexadecimal constants are numbers, not addresses. To check the copies against a
fresh download:

    unzip -q SoftFloat-3e.zip && cd SoftFloat-3e/source && for f in ~/path/to/app/engine/softfloat/*; do
      n=$(basename $f); case $n in platform.h|SOURCE.md|LICENSE) continue;; esac
      cmp $f $( [ -f $n ] && echo $n || { [ -f include/$n ] && echo include/$n || echo ARM-VFPv2/$n; } ); done

## The specialisation: ARM-VFPv2

The specialisation decides only what NaNs look like: the default NaN of an invalid operation (0/0, inf - inf) and
which operand's payload a NaN result carries. Every other result is the same in all of them. ARM-VFPv2 gives the bits
of the Mac Berlinan's reference runs on (Apple silicon, arm64, default-NaN mode off): the host tests compare 2,000,000
pairs bit for bit against the host's own arithmetic, NaNs included, and with `8086-SSE` the default NaN comes out
`0xFFF8000000000000` where arm64 gives `0x7FF8000000000000`. The engine never meets a NaN (Swift would trap on the
conversions that follow), so the choice shows only in those tests.

## The configuration (platform.h)

- `LITTLEENDIAN`: the FM-1's pi32v2 and the hosts.
- `SOFTFLOAT_FAST_INT64`: the pi32v2 compiler does 64-bit integers natively in register pairs (shifts, adds, compares
  without library calls); `f64_mul` then gets its 128-bit product from `softfloat_mul64To128`.
- `softfloat_mul64To128` is a macro for `bl_mul128` (`f64.c`), declared here. This is SoftFloat's own hook:
  `primitives.h` declares the function only while its name is not a macro (as `opts-GCC.h` uses it for its inline
  versions).
- `INLINE_LEVEL` not defined: every primitive in `primitives.h` stays an out-of-line function in its own `s_*.c`
  (with `INLINE_LEVEL` the header would define them inline). The 64-bit multiplies are then in
  `softfloat_approxRecip32_1`, a function of its own, in `f64_div` itself and in `f64.c`'s `bl_mul32` (below).
- Not defined either: `SOFTFLOAT_FAST_DIV64TO32` (a 64-by-32-bit divide, a libgcc call the freestanding firmware does
  not have; without it `softfloat_approxRecip32_1` uses its table), `SOFTFLOAT_FAST_DIV32TO16`,
  `SOFTFLOAT_ROUND_ODD` (no use), `SOFTFLOAT_BUILTIN_CLZ` (the table in `s_countLeadingZeros8.c` instead of
  `opts-GCC.h`), `SOFTFLOAT_INTRINSIC_INT128` (no `__int128` on a 32-bit CPU). `THREAD_LOCAL` stays empty.
- SoftFloat's own `f64_add`, `f64_sub`, `f64_mul`, `f64_div`, `f64_lt`, `f64_le`, `f64_eq` and `f64_to_i64` are
  renamed `sf_f64_*` by macros, so that `f64.h` can give those names to its operations on `f64`.
- `-Wsign-compare` and `-Wunused-label` are off for SoftFloat's files (the host tests build with `-Wall -Wextra
  -Werror`; `f64_to_i64_r_minMag.c` compares a signed with an unsigned value correctly, and `s_roundPackToF64.c` has
  a label only `SOFTFLOAT_ROUND_ODD` uses). `f64.c` includes `platform.h` between a diagnostic push and pop, so its
  own code keeps both warnings.

## 64-bit multiplies (felucca-bsp docs/CPU.md)

The 32x32->64-bit multiplies are in `s_approxRecip32_1.c` (a helper of its own, the product its only work) and
`f64_div.c`, whose quotient steps multiply inline (six products), and in `f64.c`'s `bl_mul32`, which makes every
product of `bl_mul128`. No other vendored file and nothing else in `f64.c` multiplies 64-bit values. For the helpers
to stay out of line, each SoftFloat file must be a compilation unit of its own in the firmware (fm1.toml `o2 =
["engine/softfloat/*.c", ...]`), never `#include`d into one unit with its callers: without LTO nothing inlines across
units.

fm1-base's check (tools/build.py `mul64_near_loads`: a load beside a multiply) over the firmware with the engine linked
in, and over every engine unit's object, names no `f64_div` and no `softfloat_approxRecip32_1`. It named
upstream's `softfloat_mul64To128`: the compiler put its four products beside loads of its arguments (the second
operand comes partly from the stack). Hence `bl_mul128`, the same sums as `s_mul64To128.c` on products from
`bl_mul32`, a function that loads nothing. The host tests' 2,000,000 `f64_mul` pairs and the equivalence scripts
check it.

## Not taken

`s_mul64To128.c` (above). Everything else: the other formats (f16, f32, extF80, f128), `f64_rem`, `f64_sqrt`,
`f64_mulAdd` (Swift's `*` and `+` never fuse), the other specialisations, `opts-GCC.h`, the build directories, the
documentation and the test programs.
