# MLL File Progress

## Current Support

SPICE reads raw or AKLZ-wrapped MLL containers, exposes the big-endian header and named `0x20`-byte member table, and preserves every member payload. Known tightly packed containers can be rebuilt while retaining names, header words, record metadata, and unchanged payload bytes. Member offsets can be recalculated when an explicitly replaced payload changes size.

Lightweight probes identify likely MLD, indexed BIN, Ninja, `POF0`, compressed, empty, and unknown members. Full interpretation is delegated to the project that owns the detected inner format, keeping the MLL model limited to container responsibilities.

## Known Limitations

The safe rebuild path is based on the normal tightly packed layout. Variant containers with gaps, unexpected record sentinels, or ambiguous payloads may be readable but are not automatically normalized. Classification is routing evidence only and can remain uncertain where several headers overlap.

MLL does not define the texture, model, or UI semantics of its members. Editing an inner resource requires parsing and rebuilding that member with its owning format before replacing the bounded payload.
