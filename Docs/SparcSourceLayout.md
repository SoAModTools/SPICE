# SPARC source-layout correction

Experimental correction for review, based on SimmerDev's qualified GC-US static
and live research packet `mode11_batch_research_20260915`. The packet is external;
no game resources, captures, or machine-specific evidence paths belong here.

## Physical mapping

Constructor `0x8003BA08` copies source `[0,0x164)` to local `[0x28,0x18C)`
without rearrangement. A simultaneous observation of all 89 source/copied words
agrees in six identity-qualified occurrences across three independent jobs.
Loop `0x80042B10` consumes the corrected local/source offsets below. This fixes
the source representation, not the downstream scheduler or RNG implementation.

| Source span | Representation |
| --- | --- |
| +0x00..+0x13 | Existing prefix, model key, setup words, child metadata (unchanged) |
| +0x14 / +0x16 | Signed `sparcDelayTicks` / `effectLifetimeTicks` |
| +0x18 / +0x1A | Existing signed duration/activation and child parameter |
| +0x1C..+0x27 | Three bit-exact `positionOrOffset` floats |
| +0x28..+0x33 | Three bit-exact `velocityVector` floats |
| +0x34 / +0x36 | Signed `spawnCount` / `randomRange` (random delay divisor) |
| +0x38 / +0x3A | Signed `spawnMode` / `raw3a` |
| +0x3C | Bit-exact `childDivisorOrParameter` float |
| +0x40..+0x47 | Two bit-exact `childParameters40` floats |
| +0x48..+0x53 | Three bit-exact `vectorMultipliers` floats |
| +0x54..+0x5F | Three bit-exact `secondaryVector` floats |
| +0x60..+0x15F | 64 signed two-halfword `choices` |
| +0x160 / +0x162 | Separate signed `choiceFirstRaw` / `choiceLastRaw` |

The delay comparison loads local +0x3C at `0x80042BF0`; the effect lifetime is
loaded from local +0x3E at `0x80043114`. Spawn count is read at local +0x5C
(`0x80043278`), the random divisor at local +0x5E (`0x800430E4`, `0x80043100`).
At `0x80043154..0x80043170` the code copies 0x100 bytes from local +0x88 and
separately copies the halfwords at local +0x188/+0x18A. Those halfwords are not
the final choice. Raw names preserve their bytes without imposing runtime range
validation or admitting an unobserved gameplay branch.

## Compatibility and preservation

The old codec inserted two nonexistent halfwords at +0x1C/+0x1E, shifting every
following field by four bytes and swallowing the trailing halfwords into the
choice table. Importer and writer agreed with each other, masking the error in
round-trip-only tests. The corrected payload remains exactly 0x164 bytes.

This is an intentional C++/JSON representation change: remove `raw1c` and
`reserved1e`; rename `raw3e` to `raw3a` and `childParameters44` to
`childParameters40`; replace the misnamed 32-bit `behaviorFlags` at +0x14 with
two independent signed fields. Add the two separate trailing raw halfwords.
JSON export schema advances from 5 to 6. Do not migrate old serialized semantic
values by renaming keys: reimport the original native resource with the corrected
codec. Source receipts, source hashes, prefix/model identity and payload extent
are unchanged. Importer/writer preserve all physical slots, float bits, negative
values and zero divisors; SPICE does not simulate or normalize their behavior.

## Validation scope

Independent synthetic native-byte tests distinguish adjacent offsets, all 64
choices, trailing fields, signed limits, NaN bits and negative zero. They check
byte-exact output and localized edits in both endian modes. Existing relocation,
AKLZ and cross-endian round trips remain applicable.

Live/static qualification for this correction is GC-US. Little-endian tests
prove codec mechanics, not Dreamcast game-layout equivalence. Other game releases
and platforms need independent static qualification before claiming six-build
semantic coverage. SimmerDev owns live capture and integration evidence; no live
analysis infrastructure or gameplay behavior is introduced into SPICE.
