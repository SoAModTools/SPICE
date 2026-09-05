#pragma once

#include "../../SpiceRoot/Binary/Endian.h"
#include "IndexEntry.h"
#include "MldGroundModel.h"
#include "MldDiagnostics.h"
#include "MldTextureArchiveModel.h"
#include "../../SpiceModeling/Animation/MotionTargetLayout.h"
#include "../../SpiceModeling/File/NinjaMotionBlock.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace spice::modeling::File {
class ModelFile;
}

namespace spice::modeling::Animation {
struct Motion;
}

namespace spice::mld::model {

enum class MldParseStatus {
    Empty,
    Partial,
    Complete,
    Failed,
};

enum class TargetPlatform {
    Unknown,
    Dreamcast,
    GameCube,
};

struct MldHeader {
    std::uint32_t entryCount = 0;
    std::uint32_t indexTableOffset = 0;
    std::uint32_t functionParametersOffset = 0;
    std::uint32_t realDataOffset = 0;
    std::uint32_t textureTableOffset = 0;
};

struct MldUnknownRange {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string label{};
    bool pinned = true;
    std::vector<std::uint8_t> bytes{};
};

struct MldSourceRange {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string label{};
    bool known = false;
    bool pinned = false;
};

struct MldRawDataBlock {
    enum class Kind {
        Unknown,
        Grnd,
        Gobj,
        TextureArchive,
        Ninja,
    };

    Kind kind = Kind::Unknown;
    std::uint32_t offset = 0;
    std::size_t size = 0;
    std::string tag{};
    std::vector<std::uint8_t> bytes{};
};

struct MldIndexEntryRecord {
    IndexEntry entry{};
    std::uint32_t groundLinksPointer = 0;
    std::uint32_t paramList2Pointer = 0;
    std::uint32_t functionParametersPointer = 0;
    std::uint32_t objectAddressesPointer = 0;
    std::uint32_t groundAddressesPointer = 0;
    std::uint32_t motionAddressesPointer = 0;
    std::vector<std::uint8_t> rawBytes{};
};

struct MldObjectResource {
    MldResourceStatus status = MldResourceStatus::Empty;
    std::uint32_t sourceAddress = 0;
    std::uint32_t blockOffset = 0;
    std::size_t blockSize = 0;
    bool includesNjtlPrefix = false;
    std::optional<std::uint32_t> modelBlockOffset{};
    std::optional<std::size_t> modelReadOffset{};
    std::optional<std::uint32_t> textureListOffset{};
    std::string wrapperLayout{};
    std::vector<std::uint8_t> rawBytes{};
    std::shared_ptr<const spice::modeling::File::ModelFile> model{};
    std::shared_ptr<const spice::modeling::File::ModelFile> originalModel{};
    std::uint64_t originalSemanticHash = 0;
    std::vector<MldDiagnostic> diagnostics{};
};

struct MldMotionVariant {
    std::uint32_t nodeCount = 0;
    bool shortRot = false;
    std::uint64_t targetLayoutSignature = 0;
    spice::modeling::Animation::MotionTargetLayout targetLayout{};
    std::shared_ptr<const spice::modeling::Animation::Motion> motion{};
    std::shared_ptr<const spice::modeling::Animation::Motion> originalMotion{};
    std::uint64_t originalSemanticHash = 0;
};

struct MldMotionResource {
    MldResourceStatus status = MldResourceStatus::Empty;
    std::uint32_t sourceAddress = 0;
    std::uint32_t blockOffset = 0;
    std::size_t blockSize = 0;
    std::vector<std::uint8_t> rawBytes{};
    spice::modeling::File::NinjaMotionBlock structure{};
    std::vector<MldMotionVariant> variants{};
    std::vector<MldDiagnostic> diagnostics{};
};

enum class MldMotionRelationStatus {
    Camera,
    Unique,
    Ambiguous,
    NoCompatibleTarget,
    TargetUnavailable,
};

enum class MldMotionRelationScope {
    SameEntryObjectList,
    NoObjectTarget,
    StructuralOnly,
};

struct MldMotionTargetCandidate {
    std::size_t objectSlot = 0;
    std::uint32_t objectAddress = 0;
    std::uint64_t targetLayoutSignature = 0;
    bool targetAvailable = false;
    bool compatible = false;
    std::string diagnostic{};
    std::optional<std::size_t> motionVariantIndex{};
};

struct MldEntryMotionRelation {
    std::size_t tableIndex = 0;
    std::uint32_t sourceEntryId = 0;
    std::size_t motionSlot = 0;
    std::uint32_t motionAddress = 0;
    spice::modeling::File::NinjaMotionKind motionKind = spice::modeling::File::NinjaMotionKind::Unknown;
    MldMotionRelationScope scope = MldMotionRelationScope::StructuralOnly;
    std::vector<MldMotionTargetCandidate> targetCandidates{};
    MldMotionRelationStatus status = MldMotionRelationStatus::NoCompatibleTarget;
    std::optional<std::size_t> resolvedCandidateIndex{};
};

struct MldAnimationBinding {
    std::size_t tableIndex = 0;
    std::uint32_t sourceEntryId = 0;
    std::size_t motionSlot = 0;
    std::uint32_t motionAddress = 0;
    std::uint32_t objectAddress = 0;
    std::uint32_t nodeCount = 0;
    bool shortRot = false;
    std::size_t motionVariantIndex = 0;
    MldMotionRelationScope scope = MldMotionRelationScope::SameEntryObjectList;
};

struct MldGroundResource {
    enum class Kind {
        Grnd,
        Gobj,
        Unknown,
    };

    MldResourceStatus status = MldResourceStatus::Empty;
    Kind kind = Kind::Unknown;
    std::uint32_t sourceAddress = 0;
    std::size_t blockSize = 0;
    std::string tag{};
    std::vector<std::uint8_t> rawBytes{};
    std::optional<GrndData> grnd{};
    std::optional<GobjData> gobj{};
    std::uint64_t originalSemanticHash = 0;
    std::vector<MldDiagnostic> diagnostics{};
};

enum class MldTextureListLayout {
    Njtl,
    Gjtl,
    Wrapper,
    CountedRecords,
    Unknown,
};

struct MldTextureListEntry {
    std::size_t ordinal = 0;
    MldByteRange recordRange{};
    std::uint32_t rawNamePointer = 0;
    std::optional<MldByteRange> nameRange{};
    std::string name{};
    std::vector<std::uint8_t> rawRecordBytes{};
    std::vector<std::uint8_t> rawNameBytes{};
};

struct MldTextureListResource {
    MldResourceStatus status = MldResourceStatus::Empty;
    std::uint32_t sourceAddress = 0;
    std::uint32_t resolvedListOffset = 0;
    MldTextureListLayout layout = MldTextureListLayout::Unknown;
    std::optional<MldByteRange> wrapperRange{};
    MldByteRange listRange{};
    std::uint32_t declaredSize = 0;
    std::uint32_t declaredCount = 0;
    std::vector<std::uint8_t> sourceBytes{};
    std::vector<std::uint8_t> wrapperBytes{};
    std::vector<std::uint8_t> listBytes{};
    std::vector<MldTextureListEntry> entries{};
    std::vector<MldDiagnostic> diagnostics{};
};

struct MldFile {
    MldParseStatus parseStatus = MldParseStatus::Empty;
    MldResourceStatus assetStatus = MldResourceStatus::Empty;
    TargetPlatform sourcePlatform = TargetPlatform::Unknown;
    spice::root::Endian endian = spice::root::Endian::Big;
    bool sourceWasCompressedAklz = false;
    MldHeader header{};
    std::vector<MldIndexEntryRecord> entries{};
    std::map<std::uint32_t, std::shared_ptr<U32List>> u32Lists{};
    std::vector<MldRawDataBlock> rawDataBlocks{};
    std::map<std::uint32_t, MldObjectResource> objectResources{};
    std::map<std::uint32_t, MldMotionResource> motionResources{};
    std::map<std::uint32_t, MldGroundResource> groundResources{};
    std::map<std::uint32_t, MldTextureListResource> textureListResources{};
    std::vector<MldEntryMotionRelation> motionRelations{};
    std::vector<MldAnimationBinding> animationBindings{};
    std::vector<MldSourceRange> sourceRanges{};
    std::vector<MldUnknownRange> paddingAndUnknownRanges{};
    std::optional<MldTextureArchive> textureArchive{};
    std::vector<std::uint8_t> sourceBytes{};
    std::vector<std::uint8_t> decodedBytes{};
    std::vector<std::uint8_t> originalBytes{};
    std::vector<MldDiagnostic> parseDiagnostics{};
};

} // namespace spice::mld::model
