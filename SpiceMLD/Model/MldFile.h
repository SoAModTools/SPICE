#pragma once

#include "../../SpiceRoot/Binary/Endian.h"
#include "IndexEntry.h"
#include "MldGroundModel.h"
#include "MldTextureArchiveModel.h"
#include "../../Sa3Dport/Animation/MotionTargetLayout.h"
#include "../../Sa3Dport/File/NinjaMotionBlock.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Sa3Dport::File {
class ModelFile;
}

namespace Sa3Dport::Animation {
struct Motion;
}

namespace spice::mld::model {

enum class MldParseStatus {
    Empty,
    Partial,
    Complete,
    Failed,
};

struct MldDiagnostic {
    enum class Severity {
        Info,
        Warning,
        Error,
    };

    Severity severity = Severity::Info;
    std::string message{};
    std::optional<std::uint32_t> sourceOffset{};
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
    std::uint32_t sourceAddress = 0;
    std::uint32_t blockOffset = 0;
    std::size_t blockSize = 0;
    bool includesNjtlPrefix = false;
    std::optional<std::uint32_t> modelBlockOffset{};
    std::optional<std::size_t> modelReadOffset{};
    std::optional<std::uint32_t> textureListOffset{};
    std::string wrapperLayout{};
    std::vector<std::uint8_t> rawBytes{};
    std::shared_ptr<const Sa3Dport::File::ModelFile> model{};
    std::shared_ptr<const Sa3Dport::File::ModelFile> originalModel{};
    std::uint64_t originalSemanticHash = 0;
    std::vector<MldDiagnostic> diagnostics{};
};

struct MldMotionVariant {
    std::uint32_t nodeCount = 0;
    bool shortRot = false;
    std::uint64_t targetLayoutSignature = 0;
    Sa3Dport::Animation::MotionTargetLayout targetLayout{};
    std::shared_ptr<const Sa3Dport::Animation::Motion> motion{};
    std::shared_ptr<const Sa3Dport::Animation::Motion> originalMotion{};
    std::uint64_t originalSemanticHash = 0;
};

struct MldMotionResource {
    std::uint32_t sourceAddress = 0;
    std::uint32_t blockOffset = 0;
    std::size_t blockSize = 0;
    std::vector<std::uint8_t> rawBytes{};
    Sa3Dport::File::NinjaMotionBlock structure{};
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
    Sa3Dport::File::NinjaMotionKind motionKind = Sa3Dport::File::NinjaMotionKind::Unknown;
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

struct MldFile {
    MldParseStatus parseStatus = MldParseStatus::Empty;
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
