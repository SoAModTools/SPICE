# SCT File Progress

## Current Support

**Capability:** Import, in-memory editing, validation, and canonical or preserving whole-file export are supported.

SPICE reads raw and AKLZ-wrapped SCT files in either byte order. It models the header, named section index, script instructions, label and string data, SCPT expressions, scheduled and skip-refresh prefixes, cross-section control flow, and referenced footer strings. Unknown section bytes and footer regions are retained so partially understood scripts remain inspectable.

Canonical export rebuilds section offsets and known instruction references from the parsed representation. A preserving path is also available when exact original bytes are required.

## Known Limitations

The first eight header bytes are not yet named. Endian and section-kind selection rely partly on structural plausibility because no complete magic-based discriminator is known. Opcode parameter shapes cover the current metadata table, but many opcode meanings and some loop forms remain incomplete. Mixed-endian instruction words are handled defensively without a complete explanation of why they occur.

String decoding currently assumes printable ASCII and common whitespace. Footer detection depends on section boundaries, terminators, and known reference forms, so unusual unreferenced footer data may remain opaque.
