# Effect-model source semantics

## Public entry points

`SpiceStd/SpiceStd.h` exports `StdType53Projector::project` and
`decodeStdModelResourceReference`. `SpiceMLD/SpiceMLD.h` exports
`MldMotionListProjector::selectImportedEntry` and `project`.

These APIs decode individual resources. They do not choose a dataset, open model
files, reproduce a runtime cache, or establish a relationship between arbitrary
STD and MLD inputs. SMORES owns those joins and their dataset, revision, platform,
edition, spelling, source-member, and loader-context checks.

## Type53 scope and preservation

The supported interpretation profiles are GameCube US/EU/JP and Dreamcast US/EU/JP,
qualified by the September 11, 2026 static effect-model mapping investigation.
The caller supplies the exact release context; byte order alone cannot identify
an edition. `Unknown` and unrecognized enum values are unsupported. GameCube
profiles require a big-endian import and Dreamcast profiles a little-endian import.

Combined type `0x00030053` has a `0x90`-byte payload. The projection exposes:

| Payload offset | Representation | Exposed meaning |
| --- | --- | --- |
| `0x08` | Native-endian u32 | Raw encoded key in the registered model-name namespace |
| `0x10` | Native-endian u32 | Attachment flags, retaining every unknown bit |
| `0x14` | Native-endian signed i16 | Delay field; no scheduling behavior is implied |

`meshOrdinal` is explicitly zero for this producer on the six qualified releases.
It is a statically qualified builder argument, not a serialized field or a digit
extracted from the key. No other type53 bytes receive new meanings.

The payload remains `StdOpaquePayload` in `StdDocument`. This additive projection
does not introduce a mutable payload variant, change command descriptors, or
conflate type53 with ordinary `StdPutModelPayload` (`0x00030003`, size `0x194`).
The existing writer preserves the complete payload at its native byte order and
rejects cross-endian opaque output. SPARC, ordinary PUTMODEL, MOVE MODEL, and
action-row semantics are unchanged. STD JSON export remains schema version 5,
with type53 bytes represented as opaque content.

Projection failures distinguish unsupported profile/document/command, missing
record/payload, malformed payload, invalid IDs, missing/mismatched receipt,
edited source, and an unsupported resource key. Resource results further
distinguish the sentinel from an unsupported key. Read fields only after checking
the status; default values in failed projections are not evidence.

## Registered model-name decoding

Keep the full raw u32 key. The executable's arithmetic interprets it as signed
32-bit. Negative values are outside the qualified usable-key domain; the API
reports `UnsupportedKey`. Key `9999999` reports `Sentinel` and produces no name.

For nonnegative `K < 10000000`, excluding the sentinel:

1. `A = K / 100000`, `R = K % 100000`.
2. On GameCube only, narrow `R` to its low 16 bits and sign-extend.
3. `B = R / 100`, `C = R % 100`, with signed division truncating toward zero.
4. Format `E%02d%03d%02d.MLD`. Widths are minimum widths, including signs.

For `K >= 10000000`, set `V = K - 10000000`. Narrow `V / 1000` to signed 16 bits
for the family index. Values greater than two fall back to index zero; negative
indices are unsupported. Indices 0/1/2 select A/B/G. Format `M%c%03d.MLD` using
`V % 1000` for the numeric part.

The returned `fallbackDirectory` is `BCHARA` for M names and E category 99,
otherwise `BEFF`. It describes a cold standalone fallback only. The runtime first
checks registered filenames and other loaded candidates/direct inputs. Its cache
does not distinguish container, directory, hash, or entry ordinal. Existing
same-name registration wins. Therefore a formatted name plus fallback directory
does not prove the physical source of a loaded model.

The same formatter is used for MLK registration. Existing `MlkParser` records
expose index, record offset, key, payload offset/size, and bounds. SMORES must use
a qualified loader context to select the exact container member or standalone
resource, retaining the container/member identity and source hashes. Formatting
does not require a new executable table input. This decoder intentionally does
not change the older SPARC-specific filename helper.

## Explicit MLD entry selection and list state

The qualified loader preserves serialized entry order. For table offset `T` and
an explicit signed selector `S`, select ordinal `S` and source offset
`T + S * 0x68`. Require `0 <= S < entryCount`. `selectImportedEntry` validates this
selection and returns the document-local `MldEntryId`; `project` accepts that ID
explicitly. Neither method defaults to the first entry. Ordinal, encoded
`entryId`, encoded `tableId`, and document-local ID are distinct.

The selected entry's `+0x1c` pointer is relative to the beginning of the decoded
MLD member. Returned entry/list offsets use that same base, even for AKLZ input
or an MLD extracted from an MLK. They are not offsets into a compressed source or
the containing MLK.

| `listStatus` | Meaning after `bindingStatus == Matched` |
| --- | --- |
| `Absent` | Source pointer is zero; no source list exists |
| `PresentEmpty` | Nonzero pointer addresses a valid count of zero |
| `PresentNonempty` | Bounded list with one or more ordered slots |
| `PointerOutOfBounds` | Pointer is at or beyond the decoded member end |
| `TruncatedCount` | Pointer is inside the member but a complete u32 count is unavailable |
| `ExcessiveCount` | Count exceeds the parser's defensive 65,536-slot limit |
| `TruncatedValues` | Count is readable but its declared values overrun the member |
| `Unqualified` | No supported source list evidence is available |

`verifiedPresentEmpty()` requires both a matching source binding and
`PresentEmpty`. `validPresentList()` also accepts `PresentNonempty`. Source pointer,
declared count, ordered raw slot references, source ordinal/entry offset, source
hash/size, and platform are available without reading receipt internals.

A zero slot value represents an existing unassigned slot. It is not removed or
converted into an absent list. The `motionFrames` member uses the existing
`MldMotionFrameProjector` on the **current** document resources, distinguishing
null slots, missing resources, opaque motions, missing decoded variants,
conflicting declared frame counts, and resolved motions including zero frames.
An imported nonzero reference whose bytes cannot be owned rejects the native
import; it does not become a successful source-backed null-slot projection.
Its `ok()` contract is unchanged: an existing entry with a nonempty list in which
every slot resolves. An empty list does not pass that action-motion check.

The list query does not prove that arbitrary runtime pointers are safe to
dereference. A zero source list pointer stays zero after relocation; it is not
proof of safe runtime selector failure. For recognized Ninja motion chunks, the
initial loaded slot incorporates the platform's eight-byte adjustment. Dreamcast
also has an alternate conversion path; this API does not provide a universal
inverse from runtime motion pointers to source offsets.

## Import evidence and edits

Each native import creates a private identity shared by its document and receipt.
An ordinary copy retains that identity. Independent imports, even of identical
bytes with identical local IDs, are distinct instances. A default receipt or a
caller-built document cannot certify source list state. Receipt source hash,
sizes, byte order/platform, and wrapper metadata are checked against immutable
import evidence. Source paths are descriptive and may be relocated.

Type53 projection checks the complete imported STD document. Any semantic,
opaque-byte, identity, order, or layout edit requires write/reimport before it
can be projected as original source evidence again. Record and payload IDs, plus
decoded record/payload offsets, identify the selected command within that import.

MLD list projection checks the entire entry ID/order sequence and all fields of
the selected entry, including slot order and IDs. Any selected-entry edit or
entry reordering invalidates it. Duplicate/zero entry, motion, or motion-variant
IDs are rejected. Other resource edits do not certify new source bytes: the list
evidence remains scoped to the original selected entry, while `motionFrames`
describes current resources. Consumers requiring an entirely unchanged model
must retain an immutable document revision. Write/reimport establishes fresh
evidence for supported edits.

The preserving MLD writer clones retained lists before assigning edits, so it
cannot modify a receipt's source evidence. It rejects invalid imported lists
instead of rewriting their empty parser fallback as a valid list. Adding values
to an absent source list is also rejected by preserving output; constructive
output without a preservation receipt can allocate new lists. An unchanged
absent list retains its zero pointer on supported output.

Import tokens and integrity fingerprints are in-process evidence, not serialized
credentials. `StdDocument` and `MldDocument` gain source-identity storage, and
`StdImportReceipt` gains private state and is no longer an aggregate. Rebuild
binary consumers; aggregate initialization of `StdImportReceipt` must become
default construction followed by field assignment. Existing native import/copy
call sites continue to work. Document payload variants and disk encodings are
unchanged; STD document equality still compares semantic content only.

## Corpus-free usage sketch

The application already holds `selectedRecordId`, native STD bytes, the release
profile, and the exact MLD member bytes selected by SMORES's qualified dataset
and loader-context relationship:

```cpp
#include "SpiceStd/SpiceStd.h"
#include "SpiceMLD/SpiceMLD.h"

auto script = spice::stdfile::StdDocumentImporter::importBytes(stdBytes);
if (!script.ok()) return;
auto reference = spice::stdfile::StdType53Projector::project(
    *script.document, selectedRecordId, script.receipt, releaseProfile);
if (!reference.ok()) return;

// SMORES verifies reference.resource.logicalName against its selected resource
// and retains dataset/revision/container/member/loader-context provenance.
auto model = spice::mld::MldDocumentImporter::importBytes(qualifiedMldBytes);
if (!model.ok()) return;
auto selected = spice::mld::MldMotionListProjector::selectImportedEntry(
    *model.document, reference.meshOrdinal, model.receipt);
if (selected.verifiedPresentEmpty()) {
    // selected.entryId explicitly identifies this imported source entry.
    // The selected motion list is present and contains zero slots.
}
```

## Qualification versus validation

The type53 size/fields, formatter arithmetic, producer ordinal zero, preserved MLD
entry order, and member-relative list structure have static qualification across
the six named releases. The existing GameCube runtime packet corroborates the
bounded payload/list example. No Dreamcast runtime equivalence is claimed.
The exact historical cache population for those captures was not observed;
Jahorta accepted the static source mapping as sufficient for this implementation.
This does not establish global filename uniqueness or arbitrary cache identity.

Private evidence remains in the established local investigation workflow under
the identifier `20260911_1728_effect_model_mapping`; the SPICE implementation
checkpoint records local references and actual validation results. Public tests
in `test_effect_model_projection.cpp` use synthetic data and demonstrate API,
preservation, and integrity behavior, not game-specific mapping truth. SMORES
relationship composition and SIMMER preflight/gameplay remain separate work.
