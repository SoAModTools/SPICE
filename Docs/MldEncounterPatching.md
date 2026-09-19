# MLD encounter patches

`spice::mld::patching::MldEncounterPatchPlan` combines existing GRND/GOBJ triangle selector edits with existing entry function-parameter word edits. Include `SpiceMLD/SpiceMLD.h` or `SpiceMLD/Patching/EncounterPatcher.h`.

This is a native byte-patching path. Planning rechecks native locations against the original source using `MldParser`; neither planning nor materialization invokes document reconstruction or a document writer. No file is installed by these APIs.

## Request and source identity

Keep the original parsed `model::MldFile`, with its retained source and decoded bytes, alongside the imported document. Copy `MldImportReceipt.sourceSha256` and `sourceSize` into `MldEncounterPatchRequest`. These identify the exact encoded input, including compression; they are not a hash of the decoded document. An independently recompressed copy is a different source and must be imported again before editing.

`triangleEdits` uses the existing `TriangleSelectorEdit` identity: resource kind, source resource address, optional GOBJ node index, and triangle index. Selector digits remain 0–9; the caller decides which selectors have encounter meaning.

`parameterEdits` contains `MldFunctionParameterEdit` records. `entryTableIndex` is the original native entry ordinal (`IndexEntry.tableIndex`), not an encoded entry ID, a document-local `MldEntryId`, or a position in a reordered parsed vector. Assert the original entry ID, table ID, function name, and parameter count. Parameter indices are zero-based, and expected/replacement values are numeric `uint32_t` words; SPICE handles byte order.

```cpp
using namespace spice::mld::patching;

MldEncounterPatchRequest request{
    .sourceSha256 = receipt.sourceSha256,
    .sourceSize = receipt.sourceSize,
};
request.triangleEdits = triangleEdits;
request.parameterEdits.push_back({
    .entryTableIndex = originalEntry.tableIndex,
    .expectedEntryId = originalEntry.entryId,
    .expectedTableId = originalEntry.tblId,
    .expectedFunctionName = originalEntry.fxnName,
    .expectedParameterCount = *originalEntry.functionParameters->declaredCount,
    .parameterIndex = parameterIndex,
    .expectedValue = originalEntry.functionParameters->values.at(parameterIndex),
    .replacementValue = replacementWord,
});

const auto plan = planEncounterPatches(originalParsedMld, request);
if (!plan.ok()) {
    // Report plan.diagnostics(); do not publish anything.
    return;
}
const auto result = materializeEncounterPatchPlan(currentSourceBytes, plan);
if (!result.ok()) {
    // Report result.diagnostics; result.bytes is empty.
    return;
}
// result.bytes is the complete materialized output for the caller to install.
```

SKEWER owns `fldEfcontrol` interpretation and lookup-byte packing. Combine all changes affecting the same parameter word into one edit from the original word to the final word. Every expected value refers to the original source, never the result of a previous requested edit.

## Validation and preservation

The planner rejects stale fingerprints, inconsistent parsed provenance, invalid identities/counts/indices/pointers, and expected-value mismatches. Parameter storage must be exclusive: shared lists, partial overlaps with other counted lists, and overlaps with unrelated resource storage are rejected. An unresolved referenced allocation also prevents parameter patching because its preservation cannot be established safely. Lists are never merged, inserted, removed, resized, or relocated.

Both edit kinds participate in one conflict check. Identical writes are deduplicated; conflicting duplicates and partial overlaps are errors. No-op requests participate in validation and conflict detection before being omitted. Diagnostics identify the requested edit and native offset when available. A default-constructed or failed plan cannot be applied, and callers cannot change a plan's resolved writes.

Materialization rechecks the encoded source fingerprint, including for empty plans. All writes are validated before a private decoded buffer is changed. Only the requested parameter words and selector bits change: triangle winding, other metadata, geometry, collision routing, models, textures, and other decoded bytes remain intact. The output preserves platform byte order and wrapper kind. AKLZ is compressed once and its decoded output is verified. A no-op returns the exact original encoded bytes without recompression.

Success returns complete bytes and `appliedPatchCount`, counting distinct changed native words after deduplication. Failure returns diagnostics, no output bytes, and a count of zero. In-place refers to decoded layout and list allocation; recompression may change encoded file size. Existing triangle-only APIs remain available.
