# MLD texture relationships

`SpiceMLD.h` exposes texture relationships directly through `MldDocument` and `MldTextureResolver`. Blender IR is an optional consumer of this API.

## Two independent lists

- `MldEntry::textureList` identifies the entry's ordered texture list. Its use by individual game handlers is outside the document's binding policy.
- `MldObjectResource::textureList` identifies the model's ordered texture list, including lists embedded in native object wrappers. Model material texture indices address this list.
- A missing optional reference means no designated list. A referenced list with no names is a present empty list. An incomplete imported list has `complete == false`; it is not silently converted to a valid empty list.

Neither list overrides or falls back to the other. Import retains source relationships and separately stored lists even when the names match. Slot order and duplicate names are significant. Texture archive position and global texture index are not material texture slots.

`MldTextureList::nativeRecordWords` retains the two non-name words of each native texture record. Slots without retained words encode with zeros. When moving slots, move their retained words with their names; renaming a slot does not require changing those words.

## Resolve a binding

```cpp
#include "SpiceMLD/SpiceMLD.h"

using namespace spice::mld;
const auto entryTexture = MldTextureResolver::resolveEntryTexture(document, entryId, 0);
const auto modelTexture = MldTextureResolver::resolveEntryObjectTexture(
    document, entryId, objectSlot, materialTextureSlot);

if (modelTexture.resolved()) {
    const MldTexture& image = *modelTexture.texture;
    // image.encodedBytes is the native image; decoded/rgba8 describe available pixels.
}
```

`resolveObjectTexture` accepts an object ID directly; `resolveListTexture` accepts an optional list ID. A material texture slot means its native texture index, not its ordinal within a node's material collection. Callers should not request a binding for an untextured material.

Results include the selected list and slot, actual stored name, matching archive and texture IDs when unique, a borrowed image pointer, candidate texture IDs for ambiguous names, and a status. Statuses distinguish absent or missing lists, incomplete lists, invalid slots, empty names, missing images, ambiguous images, and a named image record with unavailable data. Owner and object-slot failures are also explicit. No placeholder name is a successful binding.

Image lookup uses exact stored names across all document archives. Multiple matching images remain ambiguous, even when their bytes match. External texture caches are not searched implicitly; consumers can retain an unresolved name and supply their own resource context separately.

Texture IDs are stable within the editable document, including after archive reordering. Use `allocateTextureId()` for new textures. IDs are document-local, are not serialized game identifiers, and are reassigned on a fresh import. Borrowed image pointers are invalidated by document mutation or destruction; retain IDs when editing.

## Editing and native output

Changing an entry list does not change its models' list references. Replacing a model payload does not replace the entry list or the model resource's explicitly assigned list. To replace both model and model textures, assign the desired model list explicitly. Adding a document resource requires a unique ID and a corresponding `layout` item, including embedded texture lists; layout is a resource inventory rather than a requirement that every resource be emitted as a standalone block.

The writer emits model lists with native object wrappers, correctly encoded list pointers and Ninja `POF0` relocation records, and retained model payload bytes. Growing models/lists can relocate without overwriting neighboring allocations. Each model owns its emitted embedded list; assigning the same document list to several models emits equivalent per-model copies. Native entry references remain separate and use their selected list. Texture names, image replacements, archive ordering, and texture additions/removals are encoded from the document.

Native MLD output contains one texture archive. The resolver can inspect multiple document archives, but the writer requires callers to combine them explicitly. Existing restrictions on adding/removing entries and non-texture resources in receipt-preserving output, opaque content, and platform-compatible image encodings still apply. A texture's decoded pixels alone do not replace `encodedBytes`: provide the intended encoded PVR/GVR payload for binary export.

## Blender IR

Both native-file and document projections use the shared resolver. Materials retain their native `textureId` as a local slot and carry the real `textureName` plus `textureBindingStatus`; `resolved` indicates a unique available image. Consumers must not interpret local slots as archive indices or global texture IDs. The bundled Blender importer honors unresolved statuses and never falls back from a missing name to an archive/global ID. Blender display validation remains manual.
