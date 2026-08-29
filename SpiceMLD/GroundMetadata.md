# MLD ground triangle metadata

GRND and ground-role GOBJ triangle streams retain three raw `u16` words per triangle. The third word is the authored decimal ground value plus the stream winding bit. Raw words remain authoritative; `decodeTriangleMetadataWord()` computes the runtime-derived view when needed.

The source `0x8000` bit is separated as stream winding before decoding. The low 15 bits are split into decimal ones, tens, hundreds, and thousands positions. Ones, tens, and hundreds table contributions are combined with bitwise OR; the thousands contribution is added afterward. There is no ten-thousands lookup, so values in that position are reported but ignored by the decoder.

Static GameCube consumers establish the following useful descriptions:

- Ones: packed collision and camera modifier family.
- Tens: encounter selector. The movement consumer accepts `1..9`; selector `9` is statically supported but absent from the analyzed corpora.
- Hundreds: surface-response payload. GameCube audio behavior is directly observed, and payload one has an additional effect path.
- Thousands: high-bit ground-class contribution, observed in Area 99 data.

The Dreamcast executable independently confirms the same decoder tables and arithmetic. Equivalent downstream meanings on Dreamcast are a strong cross-version inference, not a directly traced Dreamcast consumer result.

## GOBJ vertex layouts

The ground-stream parser and canonical writer support these observed vertex chunks:

- `0x22`: position, three 32-bit words.
- `0x29`: position and normal, six 32-bit words.
- `0x2A`: position, normal, and ARGB8 diffuse color, seven 32-bit words.
- `0x2B`: position, normal, and raw user attributes, seven 32-bit words.

Diffuse color and raw user attributes are separate model fields. Canonical writing rejects mixed or partially populated records rather than converting between `0x2A` and `0x2B`.

## Patch output

Triangle selector edits operate on decoded MLD offsets and preserve winding plus every non-tens decimal position. Uncompressed GameCube and Dreamcast files receive endian-correct two-byte replacements. AKLZ GameCube files are decompressed, patched with the same expected-byte checks, recompressed, and verified by decompression before the replacement file is returned.

Patch planning does not attach disc, filename, area, resource-role, or content-identity policy to an edit.
