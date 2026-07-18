#include "MldParser.h"

#include "Sa3dBlenderIrBuilder.h"
#include "../../Compression/Aklz.h"
#include "../Model/IndexEntry.h"
#include "../common/ByteUtils.h"
#include "EntryHandlers.h"
#include "GobjParser.h"
#include "GrndParser.h"
#include "MldTextureArchiveParser.h"

#include "../../SpiceCore/Binary/EndianReader.h"
#include "../../Sa3Dport/Sa3Dport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace spice::mld::parsing {

namespace {

using spice::mld::model::EncounterOrTriggerRegion;
using spice::mld::model::GrndSurface;
using spice::mld::model::UnknownEntry;
using spice::mld::model::Vec3;
using spice::mld::model::WalkSurfaceNode;
using spice::core::Endian;
using spice::core::EndianReader;

constexpr std::uint32_t makeTag(const char a, const char b, const char c, const char d) {
    return static_cast<std::uint32_t>(a) |
        (static_cast<std::uint32_t>(b) << 8) |
        (static_cast<std::uint32_t>(c) << 16) |
        (static_cast<std::uint32_t>(d) << 24);
}

[[nodiscard]] std::string tagToString(const std::uint32_t tag) {
    std::array<char, 5> out{};
    out[0] = static_cast<char>(tag & 0xFFU);
    out[1] = static_cast<char>((tag >> 8) & 0xFFU);
    out[2] = static_cast<char>((tag >> 16) & 0xFFU);
    out[3] = static_cast<char>((tag >> 24) & 0xFFU);
    out[4] = '\0';

    for (std::size_t i = 0; i < 4; ++i) {
        const unsigned char c = static_cast<unsigned char>(out[i]);
        if (c < 32U || c > 126U) {
            out[i] = '?';
        }
    }
    return std::string(out.data());
}

[[nodiscard]] std::span<const std::byte> asByteSpan(const std::vector<std::uint8_t>& bytes) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bytes.data()),
        bytes.size());
}

[[nodiscard]] Vec3 applyCoordinates(const Vec3& value, const CoordinatePolicy& policy) {
    Vec3 out = value;
    if (policy.swapYZ) {
        std::swap(out.y, out.z);
    }
    if (policy.negateX) {
        out.x = -out.x;
    }
    if (policy.negateY) {
        out.y = -out.y;
    }
    if (policy.negateZ) {
        out.z = -out.z;
    }
    out.x *= policy.uniformScale;
    out.y *= policy.uniformScale;
    out.z *= policy.uniformScale;
    return out;
}

void addHistogram(std::unordered_map<std::string, std::size_t>& histogram, const ParseOptions& options, const std::string& fxnName) {
    if (!options.emitFxnHistogram) {
        return;
    }
    ++histogram[fxnName];
}

[[nodiscard]] std::string normalizeFxnName(std::string_view fxnName) {
    std::string normalized{};
    normalized.reserve(fxnName.size());
    for (const auto ch : fxnName) {
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return normalized;
}

[[nodiscard]] bool isNjModelTag(const std::uint32_t tag) {
    return tag == 0x4E4A434DU || // NJCM
        tag == 0x4E4A424DU;      // NJBM
}

[[nodiscard]] bool isNjTextureListTag(const std::uint32_t tag) {
    return tag == 0x4E4A544CU; // NJTL
}

[[nodiscard]] bool tagAt(
    std::span<const std::uint8_t> payload,
    const std::size_t offset,
    bool (*predicate)(std::uint32_t)) {
    const auto tag = common::readU32AtBE(payload, offset);
    return tag.has_value() && predicate(*tag);
}

[[nodiscard]] std::optional<std::uint32_t> findAlignedTag(
    std::span<const std::uint8_t> payload,
    const std::size_t begin,
    const std::size_t end,
    bool (*predicate)(std::uint32_t)) {
    if (begin >= end || begin + 4U > payload.size()) {
        return std::nullopt;
    }

    const auto boundedEnd = std::min(end, payload.size());
    for (std::size_t offset = begin; offset + 4U <= boundedEnd; offset += 4U) {
        if (tagAt(payload, offset, predicate)) {
            return static_cast<std::uint32_t>(offset);
        }
    }
    return std::nullopt;
}

void resolveObjectNjLayout(
    ExtractedNjBlock& block,
    std::span<const std::uint8_t> payload,
    const std::size_t begin,
    const std::size_t end) {
    if (block.kind != ExtractedNjBlock::Kind::Object || begin >= end) {
        return;
    }

    if (auto textureListOffset = findAlignedTag(payload, begin, end, isNjTextureListTag);
        textureListOffset.has_value()) {
        block.textureListOffset = *textureListOffset;
    }

    const auto setModelOffset = [&](const std::uint32_t absoluteOffset, std::string layout) {
        block.modelBlockOffset = absoluteOffset;
        block.modelReadOffset = static_cast<std::size_t>(absoluteOffset) - begin;
        block.wrapperLayout = std::move(layout);
    };

    if (tagAt(payload, begin, isNjModelTag)) {
        setModelOffset(static_cast<std::uint32_t>(begin), "raw-nj");
        return;
    }

    const auto relativeModelOffset = common::readU32AtBE(payload, begin);
    if (relativeModelOffset.has_value() && *relativeModelOffset > 0U) {
        const auto absoluteModelOffset = begin + static_cast<std::size_t>(*relativeModelOffset);
        if (absoluteModelOffset < end && tagAt(payload, absoluteModelOffset, isNjModelTag)) {
            setModelOffset(static_cast<std::uint32_t>(absoluteModelOffset), "mld-object-wrapper");
            return;
        }
    }

    constexpr std::size_t kLegacyWrapperTrim = 0x10U;
    if (begin + kLegacyWrapperTrim < end && tagAt(payload, begin + kLegacyWrapperTrim, isNjModelTag)) {
        setModelOffset(static_cast<std::uint32_t>(begin + kLegacyWrapperTrim), "legacy-0x10-wrapper");
        return;
    }

    if (auto modelOffset = findAlignedTag(payload, begin, end, isNjModelTag);
        modelOffset.has_value()) {
        setModelOffset(*modelOffset, "scanned-nj-model");
    }
}

[[nodiscard]] std::vector<ExtractedNjBlock> buildExtractedNjBlocks(
    std::span<const std::uint8_t> payload,
    const std::unordered_set<std::uint32_t>& objectAddresses,
    const std::unordered_set<std::uint32_t>& motionAddresses,
    const std::unordered_set<std::uint32_t>& textureAddresses) {
    struct CandidateAddress {
        enum class Kind {
            TextureList,
            Object,
            Motion,
        };

        std::uint32_t offset = 0;
        Kind kind = Kind::Object;
    };

    const auto readTag = [&](const std::uint32_t offset) -> std::optional<std::uint32_t> {
        const auto idx = static_cast<std::size_t>(offset);
        if (idx + 4U > payload.size()) {
            return std::nullopt;
        }
        return common::readU32AtBE(payload, idx);
    };

    std::vector<CandidateAddress> candidates{};
    candidates.reserve(objectAddresses.size() + motionAddresses.size() + textureAddresses.size());
    for (const auto offset : textureAddresses) {
        if (offset > 0U && static_cast<std::size_t>(offset) < payload.size()) {
            candidates.push_back(CandidateAddress{
                .offset = offset,
                .kind = CandidateAddress::Kind::TextureList,
            });
        }
    }
    for (const auto offset : objectAddresses) {
        if (offset > 0U && static_cast<std::size_t>(offset) < payload.size()) {
            candidates.push_back(CandidateAddress{
                .offset = offset,
                .kind = CandidateAddress::Kind::Object,
            });
        }
    }
    for (const auto offset : motionAddresses) {
        if (offset > 0U && static_cast<std::size_t>(offset) < payload.size()) {
            candidates.push_back(CandidateAddress{
                .offset = offset,
                .kind = CandidateAddress::Kind::Motion,
            });
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const CandidateAddress& a, const CandidateAddress& b) {
        if (a.offset != b.offset) {
            return a.offset < b.offset;
        }
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const CandidateAddress& a, const CandidateAddress& b) {
        return a.offset == b.offset;
    }), candidates.end());

    std::vector<ExtractedNjBlock> blocks{};
    blocks.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto candidate = candidates[i];
        const auto begin = static_cast<std::size_t>(candidate.offset);
        const auto end = (i + 1 < candidates.size())
            ? static_cast<std::size_t>(candidates[i + 1].offset)
            : payload.size();
        if (begin >= end || begin >= payload.size()) {
            continue;
        }

        bool includesNjtlPrefix = false;
        std::optional<std::uint32_t> sourceObjectAddress{};
        std::size_t effectiveEnd = std::min(end, payload.size());
        if (candidate.kind == CandidateAddress::Kind::TextureList && i + 1 < candidates.size()) {
            const auto currentTag = readTag(candidate.offset).value_or(0U);
            const auto nextTag = readTag(candidates[i + 1].offset).value_or(0U);
            const bool currentIsNjtl = isNjTextureListTag(currentTag);
            const bool nextIsNjcm = isNjModelTag(nextTag);
            const bool nextIsObject = candidates[i + 1].kind == CandidateAddress::Kind::Object;
            if (currentIsNjtl && nextIsNjcm && nextIsObject) {
                const auto objectEnd = (i + 2 < candidates.size())
                    ? static_cast<std::size_t>(candidates[i + 2].offset)
                    : payload.size();
                effectiveEnd = std::min(objectEnd, payload.size());
                includesNjtlPrefix = true;
                sourceObjectAddress = candidates[i + 1].offset;
                ++i; // consume the NJCM start with its preceding NJTL as one extracted NJ block.
            }
        }
        if (!sourceObjectAddress.has_value() && candidate.kind == CandidateAddress::Kind::Object) {
            sourceObjectAddress = candidate.offset;
        }

        ExtractedNjBlock block{};
        block.kind = candidate.kind == CandidateAddress::Kind::Motion
            ? ExtractedNjBlock::Kind::Motion
            : ExtractedNjBlock::Kind::Object;
        block.offset = candidate.offset;
        block.size = effectiveEnd - begin;
        block.includesNjtlPrefix = includesNjtlPrefix;
        block.sourceObjectAddress = sourceObjectAddress;
        block.bytes.assign(
            payload.begin() + static_cast<std::ptrdiff_t>(begin),
            payload.begin() + static_cast<std::ptrdiff_t>(effectiveEnd));
        resolveObjectNjLayout(block, payload, begin, effectiveEnd);
        blocks.push_back(std::move(block));
    }

    return blocks;
}

[[nodiscard]] const ExtractedNjBlock* findContainingObjectBlock(
    const std::vector<ExtractedNjBlock>& blocks,
    const std::uint32_t objectAddress) {
    for (const auto& block : blocks) {
        if (block.kind != ExtractedNjBlock::Kind::Object) {
            continue;
        }
        if (objectAddress >= block.offset &&
            static_cast<std::uint64_t>(objectAddress) < static_cast<std::uint64_t>(block.offset) + block.size) {
            return &block;
        }
    }
    return nullptr;
}

[[nodiscard]] const ExtractedNjBlock* findMotionBlock(
    const std::vector<ExtractedNjBlock>& blocks,
    const std::uint32_t motionAddress) {
    for (const auto& block : blocks) {
        if (block.kind == ExtractedNjBlock::Kind::Motion && block.offset == motionAddress) {
            return &block;
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<std::uint32_t> tryReadObjectNodeCount(const ExtractedNjBlock& block) {
    std::vector<std::size_t> readOffsets{};
    const auto appendReadOffset = [&](const std::size_t offset) {
        if (std::find(readOffsets.begin(), readOffsets.end(), offset) == readOffsets.end()) {
            readOffsets.push_back(offset);
        }
    };
    if (block.modelReadOffset.has_value()) {
        appendReadOffset(*block.modelReadOffset);
    }
    appendReadOffset(0U);
    appendReadOffset(0x10U);

    auto tryRead = [&](const std::size_t trim) -> std::optional<std::uint32_t> {
        if (trim >= block.bytes.size()) {
            return std::nullopt;
        }
        try {
            const auto bytes = asByteSpan(block.bytes).subspan(trim);
            const auto modelFile = Sa3Dport::File::ModelFile::read_from_bytes(bytes);
            if (!modelFile.model) {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(modelFile.model->tree_nodes().size());
        } catch (const std::exception&) {
            return std::nullopt;
        }
    };

    for (const auto readOffset : readOffsets) {
        if (auto nodeCount = tryRead(readOffset); nodeCount.has_value()) {
            return nodeCount;
        }
    }
    return std::nullopt;
}

struct AnimationTargetCandidate {
    std::uint32_t objectAddress = 0;
    std::uint32_t nodeCount = 0;
};

struct AnimationBindingProbe {
    AnimationTargetCandidate target{};
    Sa3Dport::File::AnimationProbeResult probe{};
};

[[nodiscard]] std::vector<AnimationTargetCandidate> collectAnimationTargets(
    const ParsedRawEntry& entry,
    const std::vector<ExtractedNjBlock>& blocks) {
    std::vector<AnimationTargetCandidate> candidates{};
    for (const auto objectAddress : entry.objectAddresses) {
        const auto* objectBlock = findContainingObjectBlock(blocks, objectAddress);
        if (objectBlock == nullptr) {
            continue;
        }
        if (auto nodeCount = tryReadObjectNodeCount(*objectBlock); nodeCount.has_value()) {
            candidates.push_back(AnimationTargetCandidate{
                .objectAddress = objectAddress,
                .nodeCount = *nodeCount,
            });
        }
    }

    return candidates;
}

[[nodiscard]] std::string describeAnimationProbe(
    const AnimationBindingProbe& probe) {
    std::ostringstream message;
    message << "object=0x" << std::hex << std::setw(8) << std::setfill('0') << probe.target.objectAddress
        << std::dec << "/nodes=" << probe.target.nodeCount
        << "/shortRot=" << (probe.probe.short_rot ? "true" : "false")
        << '/' << (probe.probe.valid ? "valid" : "invalid");
    if (probe.probe.valid) {
        message << "/consumedEnd=" << probe.probe.consumed_end;
    } else if (!probe.probe.failure_reason.empty()) {
        message << '/' << probe.probe.failure_reason;
    }
    return message.str();
}

[[nodiscard]] std::optional<AnimationBindingProbe> chooseAnimationBinding(
    const std::vector<AnimationTargetCandidate>& targets,
    const ExtractedNjBlock& motionBlock,
    std::vector<std::string>& rejectedCandidates) {
    std::optional<AnimationBindingProbe> best{};

    for (const auto& target : targets) {
        for (const bool shortRot : {false, true}) {
            auto probe = Sa3Dport::File::AnimationFile::probe_from_bytes(
                asByteSpan(motionBlock.bytes), target.nodeCount, shortRot);
            AnimationBindingProbe candidate{
                .target = target,
                .probe = std::move(probe),
            };
            if (!candidate.probe.valid) {
                rejectedCandidates.push_back(describeAnimationProbe(candidate));
                continue;
            }

            if (!best.has_value() ||
                candidate.probe.consumed_end > best->probe.consumed_end ||
                (candidate.probe.consumed_end == best->probe.consumed_end &&
                    best->probe.short_rot && !candidate.probe.short_rot)) {
                best = std::move(candidate);
            }
        }
    }

    return best;
}

void parseMldAnimations(ParseResult& result) {
    for (const auto& entry : result.rawEntries) {
        bool hasMotion = false;
        for (const auto address : entry.motionAddresses) {
            if (address != 0U) {
                hasMotion = true;
                break;
            }
        }
        if (!hasMotion) {
            continue;
        }

        const auto targets = collectAnimationTargets(entry, result.extractedNjBlocks);
        if (targets.empty()) {
            result.diagnostics.push_back(ParseDiagnostic{
                .severity = ParseDiagnostic::Severity::Warning,
                .message = "Entry " + std::to_string(entry.tableIndex) +
                    " has motion addresses but no parseable object tree for animation node count.",
            });
            continue;
        }

        for (std::size_t slot = 0; slot < entry.motionAddresses.size(); ++slot) {
            const auto motionAddress = entry.motionAddresses[slot];
            if (motionAddress == 0U) {
                continue;
            }

            const auto* block = findMotionBlock(result.extractedNjBlocks, motionAddress);
            if (block == nullptr) {
                result.diagnostics.push_back(ParseDiagnostic{
                    .severity = ParseDiagnostic::Severity::Warning,
                    .message = "Entry " + std::to_string(entry.tableIndex) +
                        " motion slot " + std::to_string(slot) +
                        " points to missing block " + std::to_string(motionAddress) + ".",
                });
                continue;
            }

            std::vector<std::string> rejectedCandidates{};
            const auto binding = chooseAnimationBinding(targets, *block, rejectedCandidates);
            if (!binding.has_value()) {
                std::ostringstream message;
                message << "Failed to bind animation for entry " << entry.tableIndex
                    << " fxn=\"" << entry.fxnName << "\" motion slot " << slot
                    << " motion=0x" << std::hex << std::setw(8) << std::setfill('0') << motionAddress
                    << std::dec << ". Candidates: ";
                for (std::size_t i = 0; i < rejectedCandidates.size(); ++i) {
                    if (i != 0U) {
                        message << "; ";
                    }
                    message << rejectedCandidates[i];
                }
                result.diagnostics.push_back(ParseDiagnostic{
                    .severity = ParseDiagnostic::Severity::Warning,
                    .message = message.str(),
                });
                continue;
            }

            try {
                const auto animationFile = Sa3Dport::File::AnimationFile::read_from_bytes(
                    asByteSpan(block->bytes), binding->target.nodeCount, binding->probe.short_rot);
                result.animations.push_back(ParsedMldAnimation{
                    .sourceEntryId = entry.sourceEntryId,
                    .tableIndex = entry.tableIndex,
                    .sourceObjectAddress = binding->target.objectAddress,
                    .sourceMotionAddress = motionAddress,
                    .motionSlot = slot,
                    .nodeCount = binding->target.nodeCount,
                    .shortRot = binding->probe.short_rot,
                    .motion = std::make_shared<Sa3Dport::Animation::Motion>(animationFile.animation),
                });
            } catch (const std::exception& ex) {
                result.diagnostics.push_back(ParseDiagnostic{
                    .severity = ParseDiagnostic::Severity::Warning,
                    .message = "Failed to parse animation for entry " + std::to_string(entry.tableIndex) +
                        " fxn=\"" + entry.fxnName +
                        "\" motion slot " + std::to_string(slot) +
                        " motion=" + std::to_string(motionAddress) +
                        " object=" + std::to_string(binding->target.objectAddress) +
                        " nodes=" + std::to_string(binding->target.nodeCount) +
                        " shortRot=" + (binding->probe.short_rot ? std::string("true") : std::string("false")) +
                        ": " + ex.what(),
                });
            }
        }
    }

    result.diagnostics.push_back(ParseDiagnostic{
        .severity = ParseDiagnostic::Severity::Info,
        .message = "Decoded MLD animations: " + std::to_string(result.animations.size()),
    });
}

using SpatialOwnerMap = std::unordered_map<std::uint32_t, std::vector<BlockOwnerRef>>;

[[nodiscard]] std::optional<std::uint16_t> readU16AtLE(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    if (offset + 2U > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8);
}

[[nodiscard]] std::optional<std::uint16_t> readU16AtBE(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    if (offset + 2U > bytes.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) << 8) |
        static_cast<std::uint16_t>(bytes[offset + 1U]);
}

[[nodiscard]] std::optional<std::int32_t> readI32AtLE(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    const auto value = common::readU32AtLE(bytes, offset);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*value);
}

[[nodiscard]] std::optional<std::int32_t> readI32AtBE(std::span<const std::uint8_t> bytes, const std::size_t offset) {
    const auto value = common::readU32AtBE(bytes, offset);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*value);
}

[[nodiscard]] std::string hex32(std::uint32_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "0x00000000";
    for (int i = 9; i >= 2; --i) {
        result[static_cast<std::size_t>(i)] = digits[value & 0xFu];
        value >>= 4;
    }
    return result;
}

[[nodiscard]] std::string asciiTagAt(std::span<const std::uint8_t> payload, const std::size_t offset) {
    if (offset + 4U > payload.size()) {
        return {};
    }

    std::string tag{};
    tag.reserve(4U);
    for (std::size_t i = 0; i < 4U; ++i) {
        const unsigned char c = payload[offset + i];
        tag.push_back((c >= 32U && c <= 126U) ? static_cast<char>(c) : '?');
    }
    return tag;
}

[[nodiscard]] std::uint32_t readTagAt(std::span<const std::uint8_t> payload, const std::size_t offset) {
    if (offset + 4U > payload.size()) {
        return 0U;
    }
    return (static_cast<std::uint32_t>(payload[offset]) << 24U) |
        (static_cast<std::uint32_t>(payload[offset + 1U]) << 16U) |
        (static_cast<std::uint32_t>(payload[offset + 2U]) << 8U) |
        static_cast<std::uint32_t>(payload[offset + 3U]);
}

[[nodiscard]] std::optional<model::MldHeader> tryReadMldHeader(
    std::span<const std::uint8_t> payload,
    const Endian endian) {
    if (payload.size() < 0x14U) {
        return std::nullopt;
    }
    const EndianReader reader(payload, endian);
    auto entryCount = reader.try_read_u32(0x00U);
    auto index = reader.try_read_u32(0x04U);
    auto fxn = reader.try_read_u32(0x08U);
    auto real = reader.try_read_u32(0x0CU);
    auto texture = reader.try_read_u32(0x10U);
    if (!entryCount.has_value() || !index.has_value() || !fxn.has_value() || !real.has_value() || !texture.has_value()) {
        return std::nullopt;
    }
    return model::MldHeader{
        .entryCount = *entryCount,
        .indexTableOffset = *index,
        .functionParametersOffset = *fxn,
        .realDataOffset = *real,
        .textureTableOffset = *texture,
    };
}

[[nodiscard]] bool isPlausibleMldHeader(std::span<const std::uint8_t> payload, const model::MldHeader& header) {
    constexpr std::size_t entrySize = 0x68U;
    constexpr std::uint32_t hardEntryCap = 1U << 16U;
    if (header.entryCount == 0U || header.entryCount > hardEntryCap) {
        return false;
    }
    const auto tableOffset = static_cast<std::size_t>(header.indexTableOffset);
    const auto count = static_cast<std::size_t>(header.entryCount);
    if (tableOffset >= payload.size() || count > ((payload.size() - tableOffset) / entrySize)) {
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<Endian> detectMldEndian(std::span<const std::uint8_t> payload) {
    const auto littleHeader = tryReadMldHeader(payload, Endian::Little);
    const auto bigHeader = tryReadMldHeader(payload, Endian::Big);
    const bool littleOk = littleHeader.has_value() && isPlausibleMldHeader(payload, *littleHeader);
    const bool bigOk = bigHeader.has_value() && isPlausibleMldHeader(payload, *bigHeader);
    if (littleOk && bigOk) {
        return littleHeader->entryCount < bigHeader->entryCount ? Endian::Little : Endian::Big;
    }
    if (littleOk) {
        return Endian::Little;
    }
    if (bigOk) {
        return Endian::Big;
    }
    return std::nullopt;
}

[[nodiscard]] std::string endianName(const Endian endian) {
    return endian == Endian::Little ? "little" : "big";
}

[[nodiscard]] std::string readFixedAsciiName(std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::size_t maxLength) {
    std::string out{};
    if (offset >= bytes.size()) {
        return out;
    }

    const auto end = std::min(bytes.size(), offset + maxLength);
    for (std::size_t i = offset; i < end; ++i) {
        const auto ch = bytes[i];
        if (ch == 0U) {
            break;
        }
        if (std::isprint(static_cast<unsigned char>(ch)) == 0) {
            break;
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

[[nodiscard]] std::vector<std::string> parseEntryTextureNames(std::span<const std::uint8_t> payload,
    const std::uint32_t texturesPointer,
    const Endian endian) {
    constexpr std::uint32_t kNjtlTag = 0x4E4A544CU;
    constexpr std::uint32_t kGjtlTag = 0x474A544CU;
    constexpr std::size_t textureRecordStride = 12U;
    std::vector<std::string> names{};
    std::size_t njtlOffset = static_cast<std::size_t>(texturesPointer);
    if (njtlOffset + 16U > payload.size()) {
        return names;
    }

    const EndianReader reader(payload, endian);
    auto tag = readTagAt(payload, njtlOffset);
    if (tag != kNjtlTag && tag != kGjtlTag) {
        const auto count = reader.try_read_u32(njtlOffset + 4U);
        if (count.has_value() && *count <= 4096U &&
            njtlOffset + 8U + (static_cast<std::size_t>(*count) * textureRecordStride) <= payload.size()) {
            names.reserve(*count);
            for (std::uint32_t i = 0; i < *count; ++i) {
                const auto namePointer = reader.try_read_u32(
                    njtlOffset + 8U + (static_cast<std::size_t>(i) * textureRecordStride));
                if (!namePointer.has_value() || static_cast<std::size_t>(*namePointer) >= payload.size()) {
                    names.push_back({});
                    continue;
                }
                names.push_back(readFixedAsciiName(payload, *namePointer, payload.size() - *namePointer));
            }
            return names;
        }
    }

    if (tag != kNjtlTag && tag != kGjtlTag) {
        const auto wrappedNjtlPointer = reader.try_read_u32(njtlOffset + 0x08U).value_or(0U);
        if (wrappedNjtlPointer == 0U || static_cast<std::size_t>(wrappedNjtlPointer) + 16U > payload.size()) {
            return names;
        }
        njtlOffset = static_cast<std::size_t>(wrappedNjtlPointer);
        tag = readTagAt(payload, njtlOffset);
    }
    if (tag != kNjtlTag && tag != kGjtlTag) {
        return names;
    }

    const auto blockSize = reader.try_read_u32(njtlOffset + 4U);
    const auto count = reader.try_read_u32(njtlOffset + 12U);
    if (!blockSize.has_value() || !count.has_value() || *count > 4096U) {
        return names;
    }

    constexpr std::size_t blockHeaderSize = 8U;
    constexpr std::size_t textureRecordTableOffset = 8U;
    constexpr std::size_t textureRecordSize = 12U;
    const std::size_t payloadStart = njtlOffset + blockHeaderSize;
    if (payloadStart >= payload.size()) {
        return names;
    }

    const std::size_t payloadSize = std::min<std::size_t>(*blockSize, payload.size() - payloadStart);
    const auto texturePayload = std::span<const std::uint8_t>(
        payload.data() + static_cast<std::ptrdiff_t>(payloadStart),
        payloadSize);

    const EndianReader textureReader(texturePayload, endian);
    names.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        const std::size_t recordOffset = textureRecordTableOffset + (static_cast<std::size_t>(i) * textureRecordSize);
        const auto namePointer = textureReader.try_read_u32(recordOffset);
        if (!namePointer.has_value() || static_cast<std::size_t>(*namePointer) >= texturePayload.size()) {
            names.push_back({});
            continue;
        }
        names.push_back(readFixedAsciiName(texturePayload, *namePointer, texturePayload.size() - *namePointer));
    }

    return names;
}

[[nodiscard]] ParsedEntryListItem makeEntryListItem(std::span<const std::uint8_t> payload,
    const model::IndexEntry& entry,
    const Endian endian) {
    ParsedEntryListItem item{};
    item.tableIndex = entry.tableIndex;
    item.entryId = entry.entryId;
    item.tblId = entry.tblId;
    item.fxnName = entry.fxnName;
    item.objectCount = entry.objectCount;
    item.groundCount = entry.groundCount;
    item.motionCount = entry.motionCount;
    item.texturesPointer = entry.texturesPointer;
    item.groundLinks = entry.groundLinks ? entry.groundLinks->values : std::vector<std::uint32_t>{};
    item.paramList2 = entry.paramList2 ? entry.paramList2->values : std::vector<std::uint32_t>{};
    item.functionParameters = entry.functionParameters ? entry.functionParameters->values : std::vector<std::uint32_t>{};
    item.objectAddresses = entry.objectAddresses ? entry.objectAddresses->values : std::vector<std::uint32_t>{};
    item.groundAddresses = entry.groundAddresses ? entry.groundAddresses->values : std::vector<std::uint32_t>{};
    item.motionAddresses = entry.motionAddresses ? entry.motionAddresses->values : std::vector<std::uint32_t>{};
    item.textureNames = parseEntryTextureNames(payload, entry.texturesPointer, endian);
    item.textureCount = item.textureNames.size();
    return item;
}

[[nodiscard]] bool isNjLikeTag(const std::string& tag) {
    return tag == "NJCM" ||
        tag == "GJCM" ||
        tag == "NJTL" ||
        tag == "GJTL";
}

[[nodiscard]] bool isPlausibleBlockSize(
    std::span<const std::uint8_t> payload,
    const std::size_t offset,
    const std::uint32_t size) {
    if (size < 0x10U || offset > payload.size()) {
        return false;
    }
    return static_cast<std::size_t>(size) <= payload.size() - offset;
}

[[nodiscard]] std::optional<std::size_t> nextCandidateAfter(
    const std::vector<std::uint32_t>& candidateOffsets,
    const std::uint32_t offset) {
    const auto it = std::upper_bound(candidateOffsets.begin(), candidateOffsets.end(), offset);
    if (it == candidateOffsets.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*it);
}

void appendProbeValue(
    std::vector<std::pair<std::string, std::string>>& probe,
    std::string key,
    std::string value) {
    probe.emplace_back(std::move(key), std::move(value));
}

void appendProbeValue(
    std::vector<std::pair<std::string, std::string>>& probe,
    std::string key,
    const std::optional<std::uint32_t>& value) {
    appendProbeValue(probe, std::move(key), value.has_value() ? hex32(*value) : std::string("unreadable"));
}

void appendProbeValue(
    std::vector<std::pair<std::string, std::string>>& probe,
    std::string key,
    const std::optional<std::int32_t>& value) {
    appendProbeValue(probe, std::move(key), value.has_value() ? std::to_string(*value) : std::string("unreadable"));
}

void appendProbeValue(
    std::vector<std::pair<std::string, std::string>>& probe,
    std::string key,
    const std::optional<std::uint16_t>& value) {
    appendProbeValue(probe, std::move(key), value.has_value() ? std::to_string(*value) : std::string("unreadable"));
}

void appendProbeValue(
    std::vector<std::pair<std::string, std::string>>& probe,
    std::string key,
    const std::optional<float>& value) {
    appendProbeValue(probe, std::move(key), value.has_value() ? std::to_string(*value) : std::string("unreadable"));
}

[[nodiscard]] bool offsetInBlock(
    const std::size_t target,
    const std::size_t blockOffset,
    const std::size_t blockSize) {
    return target >= blockOffset && target < blockOffset + blockSize;
}

void appendGobjNodeProbe(
    const std::span<const std::uint8_t> payload,
    ExtractedMldSpatialBlock& block,
    const std::size_t nodeOffset,
    const std::string& prefix) {
    appendProbeValue(block.headerProbe, prefix + ".offset", hex32(static_cast<std::uint32_t>(nodeOffset)));
    if (nodeOffset + 0x34U > payload.size()) {
        appendProbeValue(block.headerProbe, prefix + ".status", "out_of_bounds");
        return;
    }

    const auto dataPtrLe = common::readU32AtLE(payload, nodeOffset);
    const auto dataPtrBe = common::readU32AtBE(payload, nodeOffset);
    const auto childPtrLe = common::readU32AtLE(payload, nodeOffset + 0x2CU);
    const auto childPtrBe = common::readU32AtBE(payload, nodeOffset + 0x2CU);
    const auto siblingPtrLe = common::readU32AtLE(payload, nodeOffset + 0x30U);
    const auto siblingPtrBe = common::readU32AtBE(payload, nodeOffset + 0x30U);

    appendProbeValue(block.headerProbe, prefix + ".data_ptr_le", dataPtrLe);
    appendProbeValue(block.headerProbe, prefix + ".data_ptr_be", dataPtrBe);
    appendProbeValue(block.headerProbe, prefix + ".child_ptr_le", childPtrLe);
    appendProbeValue(block.headerProbe, prefix + ".child_ptr_be", childPtrBe);
    appendProbeValue(block.headerProbe, prefix + ".sibling_ptr_le", siblingPtrLe);
    appendProbeValue(block.headerProbe, prefix + ".sibling_ptr_be", siblingPtrBe);

    if (dataPtrLe.has_value() && *dataPtrLe > 0U) {
        const auto target = nodeOffset + static_cast<std::size_t>(*dataPtrLe);
        appendProbeValue(block.headerProbe, prefix + ".data_target_le", hex32(static_cast<std::uint32_t>(target)));
        appendProbeValue(block.headerProbe, prefix + ".data_target_le_in_block",
            offsetInBlock(target, block.offset, block.size) ? "true" : "false");
    }
    if (childPtrLe.has_value() && *childPtrLe > 0U) {
        const auto target = nodeOffset + 0x2CU + static_cast<std::size_t>(*childPtrLe);
        appendProbeValue(block.headerProbe, prefix + ".child_target_le", hex32(static_cast<std::uint32_t>(target)));
        appendProbeValue(block.headerProbe, prefix + ".child_target_le_in_block",
            offsetInBlock(target, block.offset, block.size) ? "true" : "false");
    }
    if (siblingPtrLe.has_value() && *siblingPtrLe > 0U) {
        const auto target = nodeOffset + 0x2CU + static_cast<std::size_t>(*siblingPtrLe);
        appendProbeValue(block.headerProbe, prefix + ".sibling_target_le", hex32(static_cast<std::uint32_t>(target)));
        appendProbeValue(block.headerProbe, prefix + ".sibling_target_le_in_block",
            offsetInBlock(target, block.offset, block.size) ? "true" : "false");
    }
    if (dataPtrBe.has_value() && *dataPtrBe > 0U) {
        const auto target = nodeOffset + 4U + static_cast<std::size_t>(*dataPtrBe);
        appendProbeValue(block.headerProbe, prefix + ".data_target_be_rel_field", hex32(static_cast<std::uint32_t>(target)));
        appendProbeValue(block.headerProbe, prefix + ".data_target_be_rel_field_in_block",
            offsetInBlock(target, block.offset, block.size) ? "true" : "false");
    }
    if (childPtrBe.has_value() && *childPtrBe > 0U) {
        const auto target = nodeOffset + 0x2CU + static_cast<std::size_t>(*childPtrBe);
        appendProbeValue(block.headerProbe, prefix + ".child_target_be_rel_field", hex32(static_cast<std::uint32_t>(target)));
        appendProbeValue(block.headerProbe, prefix + ".child_target_be_rel_field_in_block",
            offsetInBlock(target, block.offset, block.size) ? "true" : "false");
    }
    if (siblingPtrBe.has_value() && *siblingPtrBe > 0U) {
        const auto target = nodeOffset + 0x30U + static_cast<std::size_t>(*siblingPtrBe);
        appendProbeValue(block.headerProbe, prefix + ".sibling_target_be_rel_field", hex32(static_cast<std::uint32_t>(target)));
        appendProbeValue(block.headerProbe, prefix + ".sibling_target_be_rel_field_in_block",
            offsetInBlock(target, block.offset, block.size) ? "true" : "false");
    }
}

void appendSpatialHeaderProbe(
    const std::span<const std::uint8_t> payload,
    ExtractedMldSpatialBlock& block,
    const std::optional<std::size_t> nextCandidate) {
    appendProbeValue(block.headerProbe, "tag", block.tag.empty() ? std::string("unreadable") : block.tag);
    appendProbeValue(block.headerProbe, "size_le", common::readU32AtLE(payload, static_cast<std::size_t>(block.offset) + 4U));
    appendProbeValue(block.headerProbe, "size_be", common::readU32AtBE(payload, static_cast<std::size_t>(block.offset) + 4U));
    if (nextCandidate.has_value()) {
        appendProbeValue(block.headerProbe, "next_candidate_offset", hex32(static_cast<std::uint32_t>(*nextCandidate)));
        appendProbeValue(block.headerProbe, "next_candidate_delta", std::to_string(*nextCandidate - block.offset));
    }

    if (block.kind == ExtractedMldSpatialBlock::Kind::Grnd) {
        const std::size_t inner = static_cast<std::size_t>(block.offset) + 0x10U;
        appendProbeValue(block.headerProbe, "grnd.inner_header_offset", hex32(static_cast<std::uint32_t>(inner)));
        if (inner + 0x1CU <= payload.size()) {
            const auto relTriSets = readI32AtBE(payload, inner);
            const auto relQuads = readI32AtBE(payload, inner + 4U);
            appendProbeValue(block.headerProbe, "grnd.rel_tri_sets", relTriSets);
            appendProbeValue(block.headerProbe, "grnd.rel_quad_registry", relQuads);
            if (relTriSets.has_value()) {
                appendProbeValue(block.headerProbe, "grnd.tri_sets_offset",
                    hex32(static_cast<std::uint32_t>(static_cast<std::int64_t>(inner) + *relTriSets)));
            }
            if (relQuads.has_value()) {
                appendProbeValue(block.headerProbe, "grnd.quad_registry_offset",
                    hex32(static_cast<std::uint32_t>(static_cast<std::int64_t>(inner) + *relQuads + 4)));
            }
            appendProbeValue(block.headerProbe, "grnd.center_x", common::readF32AtBE(payload, inner + 8U));
            appendProbeValue(block.headerProbe, "grnd.center_z", common::readF32AtBE(payload, inner + 0x0CU));
            appendProbeValue(block.headerProbe, "grnd.grid_x", readU16AtBE(payload, inner + 0x10U));
            appendProbeValue(block.headerProbe, "grnd.grid_z", readU16AtBE(payload, inner + 0x12U));
            appendProbeValue(block.headerProbe, "grnd.cell_size_x", readU16AtBE(payload, inner + 0x14U));
            appendProbeValue(block.headerProbe, "grnd.cell_size_z", readU16AtBE(payload, inner + 0x16U));
            appendProbeValue(block.headerProbe, "grnd.triangle_set_count", readU16AtBE(payload, inner + 0x18U));
            appendProbeValue(block.headerProbe, "grnd.quad_count", readU16AtBE(payload, inner + 0x1AU));
        } else {
            appendProbeValue(block.headerProbe, "grnd.inner_header_status", "out_of_bounds");
        }
    }

    if (block.kind == ExtractedMldSpatialBlock::Kind::Gobj ||
        block.kind == ExtractedMldSpatialBlock::Kind::UnknownObject) {
        appendGobjNodeProbe(payload, block, block.offset, "node_at_block_start");
        appendGobjNodeProbe(payload, block, static_cast<std::size_t>(block.offset) + 0x10U, "node_at_0x10");
    }
}

[[nodiscard]] std::vector<ExtractedMldSpatialBlock> buildExtractedSpatialBlocks(
    std::span<const std::uint8_t> payload,
    const Endian endian,
    const std::unordered_set<std::uint32_t>& groundAddresses,
    const std::unordered_set<std::uint32_t>& objectAddresses,
    const std::unordered_set<std::uint32_t>& motionAddresses,
    const std::unordered_set<std::uint32_t>& textureAddresses,
    const SpatialOwnerMap& groundOwners,
    const SpatialOwnerMap& objectOwners) {
    std::vector<std::uint32_t> candidateOffsets{};
    candidateOffsets.reserve(groundAddresses.size() + objectAddresses.size() + motionAddresses.size() + textureAddresses.size());
    const auto appendCandidate = [&](const std::uint32_t offset) {
        if (offset > 0U && static_cast<std::size_t>(offset) < payload.size()) {
            candidateOffsets.push_back(offset);
        }
    };
    for (const auto offset : groundAddresses) {
        appendCandidate(offset);
    }
    for (const auto offset : objectAddresses) {
        appendCandidate(offset);
    }
    for (const auto offset : motionAddresses) {
        appendCandidate(offset);
    }
    for (const auto offset : textureAddresses) {
        appendCandidate(offset);
    }
    std::sort(candidateOffsets.begin(), candidateOffsets.end());
    candidateOffsets.erase(std::unique(candidateOffsets.begin(), candidateOffsets.end()), candidateOffsets.end());

    std::vector<ExtractedMldSpatialBlock> blocks{};
    blocks.reserve(groundAddresses.size() + objectAddresses.size());

    const auto appendBlock = [&](const std::uint32_t offset, const bool isGround) {
        if (offset == 0U || static_cast<std::size_t>(offset) >= payload.size()) {
            return;
        }

        const auto begin = static_cast<std::size_t>(offset);
        const std::string tag = asciiTagAt(payload, begin);
        if (!isGround && isNjLikeTag(tag)) {
            return;
        }

        const auto nextCandidate = nextCandidateAfter(candidateOffsets, offset);
        std::size_t size = 0U;
        std::string sizeSource{};

        if (tag == "GRND" || tag == "GOBJ") {
            const auto leSize = common::readU32AtLE(payload, begin + 4U);
            if (leSize.has_value() && isPlausibleBlockSize(payload, begin, *leSize)) {
                size = static_cast<std::size_t>(*leSize);
                sizeSource = "header_le";
            } else {
                const auto beSize = common::readU32AtBE(payload, begin + 4U);
                if (beSize.has_value() && isPlausibleBlockSize(payload, begin, *beSize)) {
                    size = static_cast<std::size_t>(*beSize);
                    sizeSource = "header_be";
                }
            }
        }

        if (size == 0U && nextCandidate.has_value() && *nextCandidate > begin) {
            size = *nextCandidate - begin;
            sizeSource = "next_candidate";
        }
        if (size == 0U) {
            return;
        }

        ExtractedMldSpatialBlock block{};
        block.offset = offset;
        block.size = size;
        block.endian = endian;
        block.tag = tag;
        block.sizeSource = sizeSource;
        if (tag == "GOBJ") {
            block.kind = ExtractedMldSpatialBlock::Kind::Gobj;
            if (isGround) {
                if (const auto owners = groundOwners.find(offset); owners != groundOwners.end()) {
                    block.owners = owners->second;
                }
            } else if (const auto owners = objectOwners.find(offset); owners != objectOwners.end()) {
                block.owners = owners->second;
            }
        } else if (isGround) {
            block.kind = tag == "GRND"
                ? ExtractedMldSpatialBlock::Kind::Grnd
                : ExtractedMldSpatialBlock::Kind::UnknownGround;
            if (const auto owners = groundOwners.find(offset); owners != groundOwners.end()) {
                block.owners = owners->second;
            }
        } else {
            block.kind = tag == "GOBJ"
                ? ExtractedMldSpatialBlock::Kind::Gobj
                : ExtractedMldSpatialBlock::Kind::UnknownObject;
            if (const auto owners = objectOwners.find(offset); owners != objectOwners.end()) {
                block.owners = owners->second;
            }
        }

        block.bytes.assign(
            payload.begin() + static_cast<std::ptrdiff_t>(begin),
            payload.begin() + static_cast<std::ptrdiff_t>(begin + size));
        appendSpatialHeaderProbe(payload, block, nextCandidate);
        blocks.push_back(std::move(block));
    };

    for (const auto offset : groundAddresses) {
        appendBlock(offset, true);
    }
    for (const auto offset : objectAddresses) {
        appendBlock(offset, false);
    }

    std::sort(blocks.begin(), blocks.end(), [](const auto& a, const auto& b) {
        if (a.offset != b.offset) {
            return a.offset < b.offset;
        }
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    return blocks;
}

class CollisionEntryHandler final : public EntryHandler {
public:
    [[nodiscard]] bool canHandle(const std::string_view fxnName) const override {
        const auto normalized = normalizeFxnName(fxnName);
        return normalized == "wall" ||
            normalized == "walluv" ||
            normalized == "hasigo1" ||
            normalized == "hasigo2";
    }

    void parse(const RawEntry& entry, model::WorldModel& out) const override {
        model::CollisionVolume collision{};
        collision.sourceEntryId = entry.sourceEntryId;
        collision.fxnName = std::string(entry.fxnName);
        collision.tblId = entry.tblId;
        collision.transform = entry.transform;
        collision.objectAddresses = entry.objectAddresses;
        out.collisions.push_back(std::move(collision));
    }
};

class TriggerEntryHandler final : public EntryHandler {
public:
    [[nodiscard]] bool canHandle(const std::string_view fxnName) const override {
        const auto normalized = normalizeFxnName(fxnName);
        return normalized == "treasure" ||
            normalized == "goscript" ||
            normalized == "wallmot";
    }

    void parse(const RawEntry& entry, model::WorldModel& out) const override {
        model::TriggerVolume trigger{};
        trigger.sourceEntryId = entry.sourceEntryId;
        trigger.fxnName = std::string(entry.fxnName);
        trigger.tblId = entry.tblId;
        trigger.transform = entry.transform;
        trigger.objectAddresses = entry.objectAddresses;
        out.triggers.push_back(std::move(trigger));
    }
};

void appendListIfPresent(model::MldFile& file, std::shared_ptr<model::U32List>& list) {
    if (!list) {
        return;
    }
    const auto [existing, inserted] = file.u32Lists.emplace(list->pointer, list);
    if (!inserted) {
        list = existing->second;
    }
}

[[nodiscard]] model::MldRawDataBlock::Kind blockKindForTag(const std::string& tag) {
    if (tag == "GRND") {
        return model::MldRawDataBlock::Kind::Grnd;
    }
    if (tag == "GOBJ") {
        return model::MldRawDataBlock::Kind::Gobj;
    }
    if (isNjLikeTag(tag)) {
        return model::MldRawDataBlock::Kind::Ninja;
    }
    return model::MldRawDataBlock::Kind::Unknown;
}

void appendRawDataBlock(
    model::MldFile& file,
    std::span<const std::uint8_t> payload,
    const std::uint32_t offset,
    const Endian endian) {
    if (offset == 0U || static_cast<std::size_t>(offset) >= payload.size()) {
        return;
    }
    const auto existing = std::find_if(file.rawDataBlocks.begin(), file.rawDataBlocks.end(), [&](const model::MldRawDataBlock& item) {
        return item.offset == offset;
    });
    if (existing != file.rawDataBlocks.end()) {
        return;
    }

    const auto begin = static_cast<std::size_t>(offset);
    const auto tag = asciiTagAt(payload, begin);
    std::size_t size = 0U;
    if (tag == "GRND" || tag == "GOBJ") {
        const EndianReader reader(payload, endian);
        const auto declaredSize = reader.try_read_u32(begin + 4U);
        if (declaredSize.has_value() && isPlausibleBlockSize(payload, begin, *declaredSize)) {
            size = static_cast<std::size_t>(*declaredSize);
        }
    }
    if (size == 0U) {
        size = std::min<std::size_t>(0x40U, payload.size() - begin);
    }

    model::MldRawDataBlock block{};
    block.kind = blockKindForTag(tag);
    block.offset = offset;
    block.size = size;
    block.tag = tag;
    block.bytes.assign(
        payload.begin() + static_cast<std::ptrdiff_t>(begin),
        payload.begin() + static_cast<std::ptrdiff_t>(begin + size));
    file.rawDataBlocks.push_back(std::move(block));
}

[[nodiscard]] model::MldFile parseMldFilePayload(
    std::span<const std::uint8_t> payload,
    const ParseOptions& options) {
    model::MldFile file{};
    file.originalBytes.assign(payload.begin(), payload.end());

    if (payload.empty()) {
        file.parseDiagnostics.push_back({
            .severity = model::MldDiagnostic::Severity::Error,
            .message = "Input buffer is empty.",
        });
        return file;
    }
    if (payload.size() < 0x14U) {
        file.parseDiagnostics.push_back({
            .severity = model::MldDiagnostic::Severity::Error,
            .message = "MLD header too small.",
        });
        return file;
    }

    const auto endian = detectMldEndian(payload);
    if (!endian.has_value()) {
        file.parseDiagnostics.push_back({
            .severity = model::MldDiagnostic::Severity::Error,
            .message = "Could not detect MLD endian from entry count and entry table bounds.",
        });
        return file;
    }
    file.endian = *endian;
    file.sourcePlatform = *endian == Endian::Little
        ? model::TargetPlatform::Dreamcast
        : model::TargetPlatform::GameCube;
    file.parseDiagnostics.push_back({
        .severity = model::MldDiagnostic::Severity::Info,
        .message = "Detected " + endianName(*endian) + "-endian MLD.",
    });

    const auto header = tryReadMldHeader(payload, *endian);
    if (!header.has_value() || !isPlausibleMldHeader(payload, *header)) {
        file.parseDiagnostics.push_back({
            .severity = model::MldDiagnostic::Severity::Error,
            .message = "MLD header failed validation after endian detection.",
        });
        return file;
    }
    file.header = *header;

    constexpr std::size_t entrySize = 0x68U;
    const auto entryTableOffset = static_cast<std::size_t>(file.header.indexTableOffset);
    file.entries.reserve(file.header.entryCount);
    for (std::size_t i = 0; i < file.header.entryCount; ++i) {
        const auto entryOffset = entryTableOffset + (i * entrySize);
        std::vector<std::string> warnings{};
        auto entryOpt = model::parseIndexEntry(payload, i, entryOffset, *endian,
            [&](const Vec3& value) {
                return applyCoordinates(value, options.coordinates);
            },
            [&](const std::string& message) {
                warnings.push_back(message);
            });

        for (const auto& warning : warnings) {
            file.parseDiagnostics.push_back({
                .severity = model::MldDiagnostic::Severity::Warning,
                .message = warning,
                .sourceOffset = static_cast<std::uint32_t>(entryOffset),
            });
        }
        if (!entryOpt.has_value()) {
            file.parseDiagnostics.push_back({
                .severity = model::MldDiagnostic::Severity::Error,
                .message = "Entry " + std::to_string(i) + " malformed or truncated.",
                .sourceOffset = static_cast<std::uint32_t>(entryOffset),
            });
            continue;
        }

        const EndianReader reader(payload, *endian);
        model::MldIndexEntryRecord record{};
        record.groundLinksPointer = reader.read_u32(entryOffset + 0x08U);
        record.paramList2Pointer = reader.read_u32(entryOffset + 0x0CU);
        record.functionParametersPointer = reader.read_u32(entryOffset + 0x10U);
        record.objectAddressesPointer = reader.read_u32(entryOffset + 0x14U);
        record.groundAddressesPointer = reader.read_u32(entryOffset + 0x18U);
        record.motionAddressesPointer = reader.read_u32(entryOffset + 0x1CU);
        record.rawBytes.assign(
            payload.begin() + static_cast<std::ptrdiff_t>(entryOffset),
            payload.begin() + static_cast<std::ptrdiff_t>(entryOffset + entrySize));
        record.entry = std::move(*entryOpt);

        appendListIfPresent(file, record.entry.groundLinks);
        appendListIfPresent(file, record.entry.paramList2);
        appendListIfPresent(file, record.entry.functionParameters);
        appendListIfPresent(file, record.entry.objectAddresses);
        appendListIfPresent(file, record.entry.groundAddresses);
        appendListIfPresent(file, record.entry.motionAddresses);

        if (record.entry.objectAddresses) {
            for (const auto address : record.entry.objectAddresses->values) {
                appendRawDataBlock(file, payload, address, *endian);
            }
        }
        if (record.entry.groundAddresses) {
            for (const auto address : record.entry.groundAddresses->values) {
                appendRawDataBlock(file, payload, address, *endian);
            }
        }
        if (record.entry.motionAddresses) {
            for (const auto address : record.entry.motionAddresses->values) {
                appendRawDataBlock(file, payload, address, *endian);
            }
        }
        appendRawDataBlock(file, payload, record.entry.texturesPointer, *endian);

        file.entries.push_back(std::move(record));
    }

    if (static_cast<std::size_t>(file.header.textureTableOffset) < payload.size()) {
        file.textureArchive = parseMldTextureArchive(payload, static_cast<std::size_t>(file.header.textureTableOffset), *endian);
    }

    return file;
}

[[nodiscard]] std::uint64_t hashBytes(const std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void addCanonicalDiagnostic(model::MldFile& file,
    const model::MldDiagnostic::Severity severity,
    std::string message,
    const std::optional<std::uint32_t> sourceOffset = std::nullopt) {
    file.parseDiagnostics.push_back(model::MldDiagnostic{
        .severity = severity,
        .message = message,
        .sourceOffset = sourceOffset,
    });
}

[[nodiscard]] std::shared_ptr<const Sa3Dport::File::ModelFile> readCanonicalModel(
    const ExtractedNjBlock& block) {
    std::vector<std::size_t> offsets{};
    if (block.modelReadOffset.has_value()) {
        offsets.push_back(*block.modelReadOffset);
    }
    offsets.push_back(0U);
    offsets.push_back(0x10U);
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    for (const auto offset : offsets) {
        if (offset >= block.bytes.size()) {
            continue;
        }
        try {
            const auto bytes = asByteSpan(block.bytes).subspan(offset);
            return std::make_shared<const Sa3Dport::File::ModelFile>(
                Sa3Dport::File::ModelFile::read_from_bytes(bytes));
        } catch (const std::exception&) {
        }
    }
    return {};
}

void buildCanonicalRanges(model::MldFile& file) {
    struct Range {
        std::size_t begin = 0;
        std::size_t end = 0;
    };
    std::vector<Range> known{};
    const auto addRange = [&](const std::size_t offset, const std::size_t size) {
        if (size == 0U || offset >= file.decodedBytes.size()) {
            return;
        }
        known.push_back(Range{offset, std::min(file.decodedBytes.size(), offset + size)});
    };
    addRange(0U, 0x14U);
    addRange(file.header.indexTableOffset, static_cast<std::size_t>(file.header.entryCount) * 0x68U);
    for (const auto& [_, list] : file.u32Lists) {
        if (list) {
            addRange(list->pointer, 4U + list->values.size() * 4U);
        }
    }
    for (const auto& [_, resource] : file.objectResources) {
        addRange(resource.blockOffset, resource.blockSize);
    }
    for (const auto& [_, resource] : file.motionResources) {
        addRange(resource.blockOffset, resource.blockSize);
    }
    for (const auto& [_, resource] : file.groundResources) {
        addRange(resource.sourceAddress, resource.blockSize);
    }
    if (file.textureArchive.has_value()) {
        addRange(file.textureArchive->archiveStartOffset,
            file.textureArchive->archiveEndOffset - file.textureArchive->archiveStartOffset);
    }
    std::sort(known.begin(), known.end(), [](const auto& a, const auto& b) {
        return a.begin < b.begin || (a.begin == b.begin && a.end < b.end);
    });
    std::vector<Range> merged{};
    for (const auto& range : known) {
        if (merged.empty() || range.begin > merged.back().end) {
            merged.push_back(range);
        } else {
            merged.back().end = std::max(merged.back().end, range.end);
        }
    }

    file.sourceRanges.clear();
    file.paddingAndUnknownRanges.clear();
    std::size_t cursor = 0U;
    for (const auto& range : merged) {
        if (cursor < range.begin) {
            model::MldUnknownRange unknown{
                .offset = cursor,
                .size = range.begin - cursor,
                .label = "unclassified",
                .pinned = true,
            };
            unknown.bytes.assign(
                file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(unknown.offset),
                file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(unknown.offset + unknown.size));
            file.paddingAndUnknownRanges.push_back(std::move(unknown));
            file.sourceRanges.push_back(model::MldSourceRange{
                .offset = cursor,
                .size = range.begin - cursor,
                .label = "unclassified",
                .known = false,
                .pinned = true,
            });
        }
        file.sourceRanges.push_back(model::MldSourceRange{
            .offset = range.begin,
            .size = range.end - range.begin,
            .label = "known",
            .known = true,
        });
        cursor = std::max(cursor, range.end);
    }
    if (cursor < file.decodedBytes.size()) {
        model::MldUnknownRange unknown{
            .offset = cursor,
            .size = file.decodedBytes.size() - cursor,
            .label = "unclassified",
            .pinned = true,
        };
        unknown.bytes.assign(file.decodedBytes.begin() + static_cast<std::ptrdiff_t>(cursor), file.decodedBytes.end());
        file.paddingAndUnknownRanges.push_back(std::move(unknown));
        file.sourceRanges.push_back(model::MldSourceRange{
            .offset = cursor,
            .size = file.decodedBytes.size() - cursor,
            .label = "unclassified",
            .known = false,
            .pinned = true,
        });
    }
}

} // namespace

model::MldFile MldParser::parseBytes(
    const std::span<const std::uint8_t> mldBytes,
    const MldParseOptions& options) const {
    model::MldFile file{};
    if (options.preserveSourceBytes) {
        file.sourceBytes.assign(mldBytes.begin(), mldBytes.end());
    }

    std::vector<std::uint8_t> decoded{};
    std::span<const std::uint8_t> payload = mldBytes;
    if (spice::compression::aklz::isAklz(mldBytes)) {
        const auto decompressed = spice::compression::aklz::decompress(mldBytes);
        if (!decompressed.ok()) {
            file.sourceWasCompressedAklz = true;
            file.parseStatus = model::MldParseStatus::Failed;
            addCanonicalDiagnostic(file, model::MldDiagnostic::Severity::Error,
                "AKLZ decompression failed: " + std::string(spice::compression::aklz::errorToString(decompressed.error)));
            return file;
        }
        decoded = decompressed.bytes;
        payload = decoded;
        file.sourceWasCompressedAklz = true;
    }

    ParseOptions sourceOptions{};
    sourceOptions.buildBlenderIntermediateIr = false;
    sourceOptions.emitFxnHistogram = false;
    file = parseMldFilePayload(payload, sourceOptions);
    file.sourceWasCompressedAklz = spice::compression::aklz::isAklz(mldBytes);
    if (options.preserveSourceBytes) {
        file.sourceBytes.assign(mldBytes.begin(), mldBytes.end());
    }
    file.decodedBytes.assign(payload.begin(), payload.end());
    file.originalBytes = file.decodedBytes;
    if (file.header.entryCount == 0U || file.entries.empty()) {
        file.parseStatus = model::MldParseStatus::Failed;
        addCanonicalDiagnostic(file, model::MldDiagnostic::Severity::Error, "Failed to parse MLD index entries.");
        return file;
    }

    std::unordered_set<std::uint32_t> objectAddresses{};
    std::unordered_set<std::uint32_t> groundAddresses{};
    std::unordered_set<std::uint32_t> motionAddresses{};
    std::unordered_set<std::uint32_t> textureAddresses{};
    SpatialOwnerMap groundOwners{};
    SpatialOwnerMap objectOwners{};
    for (const auto& record : file.entries) {
        const auto& entry = record.entry;
        if (entry.objectAddresses) {
            for (const auto address : entry.objectAddresses->values) {
                if (address != 0U) {
                    objectAddresses.insert(address);
                    objectOwners[address].push_back(BlockOwnerRef{
                        .sourceEntryId = entry.entryId,
                        .tableIndex = entry.tableIndex,
                        .fxnName = entry.fxnName,
                        .role = "object",
                    });
                }
            }
        }
        if (entry.groundAddresses) {
            for (const auto address : entry.groundAddresses->values) {
                if (address != 0U) {
                    groundAddresses.insert(address);
                    groundOwners[address].push_back(BlockOwnerRef{
                        .sourceEntryId = entry.entryId,
                        .tableIndex = entry.tableIndex,
                        .fxnName = entry.fxnName,
                        .role = "ground",
                    });
                }
            }
        }
        if (entry.motionAddresses) {
            for (const auto address : entry.motionAddresses->values) {
                if (address != 0U) {
                    motionAddresses.insert(address);
                }
            }
        }
        if (entry.texturesPointer != 0U) {
            textureAddresses.insert(entry.texturesPointer);
        }
    }

    const auto njBlocks = buildExtractedNjBlocks(payload, objectAddresses, motionAddresses, textureAddresses);
    for (const auto& block : njBlocks) {
        if (block.kind == ExtractedNjBlock::Kind::Object) {
            const auto sourceAddress = block.sourceObjectAddress.value_or(block.offset);
            model::MldObjectResource resource{};
            resource.sourceAddress = sourceAddress;
            resource.blockOffset = block.offset;
            resource.blockSize = block.size;
            resource.includesNjtlPrefix = block.includesNjtlPrefix;
            resource.modelBlockOffset = block.modelBlockOffset;
            resource.modelReadOffset = block.modelReadOffset;
            resource.textureListOffset = block.textureListOffset;
            resource.wrapperLayout = block.wrapperLayout;
            resource.rawBytes = block.bytes;
            resource.originalSemanticHash = hashBytes(resource.rawBytes);
            resource.model = readCanonicalModel(block);
            resource.originalModel = resource.model;
            if (!resource.model) {
                resource.diagnostics.push_back(model::MldDiagnostic{
                    .severity = model::MldDiagnostic::Severity::Warning,
                    .message = "Existing Sa3Dport reader could not decode the object resource.",
                    .sourceOffset = sourceAddress,
                });
            }
            file.objectResources.emplace(sourceAddress, std::move(resource));
        } else {
            model::MldMotionResource resource{};
            resource.sourceAddress = block.offset;
            resource.blockOffset = block.offset;
            resource.blockSize = block.size;
            resource.rawBytes = block.bytes;
            file.motionResources.emplace(block.offset, std::move(resource));
        }
    }

    const auto spatialBlocks = buildExtractedSpatialBlocks(
        payload, file.endian, groundAddresses, objectAddresses, motionAddresses, textureAddresses,
        groundOwners, objectOwners);
    GrndParser grndParser{};
    GobjParser gobjParser{};
    for (const auto& block : spatialBlocks) {
        if (block.kind != ExtractedMldSpatialBlock::Kind::Grnd &&
            block.kind != ExtractedMldSpatialBlock::Kind::Gobj) {
            continue;
        }
        model::MldGroundResource resource{};
        resource.sourceAddress = block.offset;
        resource.blockSize = block.size;
        resource.tag = block.tag;
        resource.rawBytes = block.bytes;
        if (block.kind == ExtractedMldSpatialBlock::Kind::Grnd) {
            resource.kind = model::MldGroundResource::Kind::Grnd;
            auto parsed = grndParser.decode(block.bytes, block.offset, block.endian);
            resource.grnd = std::move(parsed.data);
            resource.originalSemanticHash = model::semanticHash(*resource.grnd);
            for (const auto& message : parsed.diagnostics) {
                resource.diagnostics.push_back(model::MldDiagnostic{
                    .severity = parsed.decoded ? model::MldDiagnostic::Severity::Info : model::MldDiagnostic::Severity::Warning,
                    .message = message,
                    .sourceOffset = block.offset,
                });
            }
        } else {
            resource.kind = model::MldGroundResource::Kind::Gobj;
            auto parsed = gobjParser.decode(block.bytes, block.offset, block.endian);
            resource.gobj = std::move(parsed.data);
            resource.originalSemanticHash = model::semanticHash(*resource.gobj);
            for (const auto& message : parsed.diagnostics) {
                resource.diagnostics.push_back(model::MldDiagnostic{
                    .severity = parsed.decoded ? model::MldDiagnostic::Severity::Info : model::MldDiagnostic::Severity::Warning,
                    .message = message,
                    .sourceOffset = block.offset,
                });
            }
        }
        file.groundResources.emplace(block.offset, std::move(resource));
    }

    for (const auto& record : file.entries) {
        const auto& entry = record.entry;
        if (!entry.motionAddresses || !entry.objectAddresses) {
            continue;
        }
        std::vector<AnimationTargetCandidate> targets{};
        for (const auto objectAddress : entry.objectAddresses->values) {
            const auto found = file.objectResources.find(objectAddress);
            if (found == file.objectResources.end() || !found->second.model || !found->second.model->model) {
                continue;
            }
            targets.push_back(AnimationTargetCandidate{
                .objectAddress = objectAddress,
                .nodeCount = static_cast<std::uint32_t>(found->second.model->model->tree_nodes().size()),
            });
        }
        for (std::size_t slot = 0; slot < entry.motionAddresses->values.size(); ++slot) {
            const auto motionAddress = entry.motionAddresses->values[slot];
            if (motionAddress == 0U) {
                continue;
            }
            const auto block = findMotionBlock(njBlocks, motionAddress);
            auto resource = file.motionResources.find(motionAddress);
            if (block == nullptr || resource == file.motionResources.end()) {
                addCanonicalDiagnostic(file, model::MldDiagnostic::Severity::Warning,
                    "Motion slot points to a missing resource.", motionAddress);
                continue;
            }
            std::vector<std::string> rejected{};
            const auto binding = chooseAnimationBinding(targets, *block, rejected);
            if (!binding.has_value()) {
                addCanonicalDiagnostic(file, model::MldDiagnostic::Severity::Warning,
                    "Could not bind motion slot " + std::to_string(slot) + " for entry " +
                        std::to_string(entry.tableIndex) + ".",
                    motionAddress);
                continue;
            }
            try {
                std::vector<AnimationBindingProbe> validInterpretations{};
                for (const auto& target : targets) {
                    for (const bool shortRot : {false, true}) {
                        auto probe = Sa3Dport::File::AnimationFile::probe_from_bytes(
                            asByteSpan(block->bytes), target.nodeCount, shortRot);
                        if (probe.valid) {
                            validInterpretations.push_back(AnimationBindingProbe{
                                .target = target,
                                .probe = std::move(probe),
                            });
                        }
                    }
                }
                for (const auto& interpretation : validInterpretations) {
                    const auto existing = std::find_if(resource->second.variants.begin(), resource->second.variants.end(), [&](const auto& item) {
                        return item.nodeCount == interpretation.target.nodeCount
                            && item.shortRot == interpretation.probe.short_rot;
                    });
                    if (existing != resource->second.variants.end()) {
                        continue;
                    }
                    const auto parsed = Sa3Dport::File::AnimationFile::read_from_bytes(
                        asByteSpan(block->bytes), interpretation.target.nodeCount, interpretation.probe.short_rot);
                    resource->second.variants.push_back(model::MldMotionVariant{
                        .nodeCount = interpretation.target.nodeCount,
                        .shortRot = interpretation.probe.short_rot,
                        .motion = std::make_shared<const Sa3Dport::Animation::Motion>(parsed.animation),
                        .originalSemanticHash = hashBytes(resource->second.rawBytes),
                    });
                    resource->second.variants.back().originalMotion = resource->second.variants.back().motion;
                }
                const auto variant = std::find_if(resource->second.variants.begin(), resource->second.variants.end(), [&](const auto& item) {
                    return item.nodeCount == binding->target.nodeCount && item.shortRot == binding->probe.short_rot;
                });
                if (variant == resource->second.variants.end()) {
                    throw std::runtime_error("selected motion interpretation was not retained");
                }
                std::set<std::pair<std::uint32_t, bool>> uniqueInterpretations{};
                for (const auto& interpretation : validInterpretations) {
                    uniqueInterpretations.emplace(interpretation.target.nodeCount, interpretation.probe.short_rot);
                }
                if (uniqueInterpretations.size() > 1U) {
                    addCanonicalDiagnostic(file, model::MldDiagnostic::Severity::Warning,
                        "Motion has multiple valid object/node-count interpretations; all variants were retained and one compatibility binding was selected.",
                        motionAddress);
                }
                file.animationBindings.push_back(model::MldAnimationBinding{
                    .tableIndex = entry.tableIndex,
                    .sourceEntryId = entry.entryId,
                    .motionSlot = slot,
                    .motionAddress = motionAddress,
                    .objectAddress = binding->target.objectAddress,
                    .nodeCount = binding->target.nodeCount,
                    .shortRot = binding->probe.short_rot,
                    .motionVariantIndex = static_cast<std::size_t>(std::distance(resource->second.variants.begin(), variant)),
                });
            } catch (const std::exception& ex) {
                addCanonicalDiagnostic(file, model::MldDiagnostic::Severity::Warning,
                    "Failed to decode bound motion: " + std::string(ex.what()), motionAddress);
            }
        }
    }

    buildCanonicalRanges(file);
    for (const auto& [_, resource] : file.objectResources) {
        file.parseDiagnostics.insert(file.parseDiagnostics.end(), resource.diagnostics.begin(), resource.diagnostics.end());
    }
    for (const auto& [_, resource] : file.motionResources) {
        file.parseDiagnostics.insert(file.parseDiagnostics.end(), resource.diagnostics.begin(), resource.diagnostics.end());
    }
    for (const auto& [_, resource] : file.groundResources) {
        file.parseDiagnostics.insert(file.parseDiagnostics.end(), resource.diagnostics.begin(), resource.diagnostics.end());
    }
    file.parseStatus = std::any_of(file.parseDiagnostics.begin(), file.parseDiagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity != model::MldDiagnostic::Severity::Info;
    }) ? model::MldParseStatus::Partial : model::MldParseStatus::Complete;
    return file;
}

model::MldFile MldParser::parseFile(std::span<const std::uint8_t> mldBytes, const ParseOptions& options) const {
    (void)options;
    return parseBytes(mldBytes);
}

ParseResult MldParser::project(const model::MldFile& file, const ParseOptions& options) const {
    ParseResult result{};
    const auto payload = std::span<const std::uint8_t>(file.decodedBytes.data(), file.decodedBytes.size());

    for (const auto& diagnostic : file.parseDiagnostics) {
        ParseDiagnostic::Severity severity = ParseDiagnostic::Severity::Info;
        if (diagnostic.severity == model::MldDiagnostic::Severity::Warning) {
            severity = ParseDiagnostic::Severity::Warning;
        } else if (diagnostic.severity == model::MldDiagnostic::Severity::Error) {
            severity = ParseDiagnostic::Severity::Error;
        }
        result.diagnostics.push_back(ParseDiagnostic{ .severity = severity, .message = diagnostic.message });
    }
    if (file.parseStatus == model::MldParseStatus::Failed || file.entries.empty()) {
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(ParseDiagnostic{
                .severity = ParseDiagnostic::Severity::Error,
                .message = "Cannot project an unparsed MLD file.",
            });
        }
        return result;
    }

    result.textureArchive = file.textureArchive;
    result.entryList.reserve(file.entries.size());
    for (const auto& record : file.entries) {
        result.entryList.push_back(makeEntryListItem(payload, record.entry, file.endian));
    }

    std::unordered_map<std::string, std::size_t> histogram{};
    const bool useEntryIdFilter = !options.filterEntryIdList.empty();
    const std::unordered_set<std::uint32_t> selectedEntryIds(
        options.filterEntryIdList.begin(), options.filterEntryIdList.end());
    const auto normalizedFilterFxn = useEntryIdFilter ? std::string{} : normalizeFxnName(options.filterFxnName);
    const bool useFxnFilter = !useEntryIdFilter && !normalizedFilterFxn.empty();
    const auto selected = [&](const model::IndexEntry& entry) {
        return useEntryIdFilter
            ? selectedEntryIds.contains(entry.entryId)
            : (!useFxnFilter || normalizeFxnName(entry.fxnName) == normalizedFilterFxn);
    };

    if (options.entryListOnly) {
        if (options.emitFxnHistogram) {
            for (const auto& record : file.entries) {
                addHistogram(histogram, options, record.entry.fxnName);
            }
        }
    } else {
        const std::array<std::unique_ptr<EntryHandler>, 2> handlers{
            std::make_unique<CollisionEntryHandler>(),
            std::make_unique<TriggerEntryHandler>(),
        };
        for (const auto& record : file.entries) {
            const auto& entry = record.entry;
            if (!selected(entry)) {
                continue;
            }
            addHistogram(histogram, options, entry.fxnName);

            auto nonzeroValues = [](const std::shared_ptr<model::U32List>& list) {
                std::vector<std::uint32_t> values{};
                if (list) {
                    std::copy_if(list->values.begin(), list->values.end(), std::back_inserter(values),
                        [](const std::uint32_t value) { return value != 0U; });
                }
                return values;
            };
            const auto objectAddresses = nonzeroValues(entry.objectAddresses);
            const auto groundAddresses = nonzeroValues(entry.groundAddresses);
            const auto entryOffset = static_cast<std::size_t>(file.header.indexTableOffset) + entry.tableIndex * 0x68U;
            std::span<const std::uint8_t> rawPayload{};
            if (entryOffset <= payload.size() && 0x68U <= payload.size() - entryOffset) {
                rawPayload = payload.subspan(entryOffset, 0x68U);
            }

            RawEntry rawEntry{
                .sourceEntryId = entry.entryId,
                .fxnName = entry.fxnName,
                .tblId = entry.tblId,
                .transform = entry.transform,
                .objectAddresses = objectAddresses,
                .payload = rawPayload,
            };
            result.rawEntries.push_back(ParsedRawEntry{
                .tableIndex = entry.tableIndex,
                .sourceEntryId = entry.entryId,
                .fxnName = entry.fxnName,
                .tblId = entry.tblId,
                .transform = entry.transform,
                .functionParameters = entry.functionParameters ? entry.functionParameters->values : std::vector<std::uint32_t>{},
                .objectAddresses = objectAddresses,
                .groundAddresses = groundAddresses,
                .motionAddresses = entry.motionAddresses ? entry.motionAddresses->values : std::vector<std::uint32_t>{},
                .payload = std::vector<std::uint8_t>(rawPayload.begin(), rawPayload.end()),
            });

            bool classified = false;
            for (const auto& handler : handlers) {
                if (handler->canHandle(entry.fxnName)) {
                    handler->parse(rawEntry, result.world);
                    classified = true;
                    break;
                }
            }
            const auto normalized = normalizeFxnName(entry.fxnName);
            if (normalized == "treasure" || normalized == "goscript" || normalized == "wallmot") {
                result.searchWorld.regions.push_back(EncounterOrTriggerRegion{
                    .sourceEntryId = entry.entryId,
                    .fxnName = entry.fxnName,
                    .tblId = entry.tblId,
                    .transform = entry.transform,
                });
            }
            if (!classified && options.preserveUnknownEntries) {
                result.world.unknownEntries.push_back(UnknownEntry{
                    .sourceEntryId = entry.entryId,
                    .fxnName = entry.fxnName,
                    .tblId = entry.tblId,
                    .transform = entry.transform,
                    .rawPayload = record.rawBytes,
                });
            }
        }

        for (const auto& [address, resource] : file.objectResources) {
            result.extractedNjBlocks.push_back(ExtractedNjBlock{
                .kind = ExtractedNjBlock::Kind::Object,
                .offset = resource.blockOffset,
                .size = resource.blockSize,
                .includesNjtlPrefix = resource.includesNjtlPrefix,
                .sourceObjectAddress = resource.sourceAddress,
                .modelBlockOffset = resource.modelBlockOffset,
                .modelReadOffset = resource.modelReadOffset,
                .textureListOffset = resource.textureListOffset,
                .wrapperLayout = resource.wrapperLayout,
                .bytes = resource.rawBytes,
            });
        }
        for (const auto& [address, resource] : file.motionResources) {
            result.extractedNjBlocks.push_back(ExtractedNjBlock{
                .kind = ExtractedNjBlock::Kind::Motion,
                .offset = resource.blockOffset,
                .size = resource.blockSize,
                .bytes = resource.rawBytes,
            });
        }
        std::sort(result.extractedNjBlocks.begin(), result.extractedNjBlocks.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.offset < rhs.offset || (lhs.offset == rhs.offset && lhs.kind < rhs.kind);
        });

        for (const auto& binding : file.animationBindings) {
            const auto resource = file.motionResources.find(binding.motionAddress);
            if (resource == file.motionResources.end() || binding.motionVariantIndex >= resource->second.variants.size()) {
                continue;
            }
            const auto& variant = resource->second.variants[binding.motionVariantIndex];
            result.animations.push_back(ParsedMldAnimation{
                .sourceEntryId = binding.sourceEntryId,
                .tableIndex = binding.tableIndex,
                .sourceObjectAddress = binding.objectAddress,
                .sourceMotionAddress = binding.motionAddress,
                .motionSlot = binding.motionSlot,
                .nodeCount = binding.nodeCount,
                .shortRot = binding.shortRot,
                .motion = variant.motion,
            });
        }

        for (const auto& [address, resource] : file.groundResources) {
            ExtractedMldSpatialBlock::Kind kind = ExtractedMldSpatialBlock::Kind::UnknownGround;
            if (resource.kind == model::MldGroundResource::Kind::Grnd) {
                kind = ExtractedMldSpatialBlock::Kind::Grnd;
            } else if (resource.kind == model::MldGroundResource::Kind::Gobj) {
                kind = ExtractedMldSpatialBlock::Kind::Gobj;
            }
            ExtractedMldSpatialBlock block{
                .kind = kind,
                .offset = address,
                .size = resource.blockSize,
                .endian = file.endian,
                .tag = resource.tag,
                .sizeSource = "canonical resource",
                .bytes = resource.rawBytes,
            };
            for (const auto& record : file.entries) {
                const auto& entry = record.entry;
                const auto addOwners = [&](const std::shared_ptr<model::U32List>& list, const char* role) {
                    if (list && std::find(list->values.begin(), list->values.end(), address) != list->values.end()) {
                        block.owners.push_back(BlockOwnerRef{
                            .sourceEntryId = entry.entryId,
                            .tableIndex = entry.tableIndex,
                            .fxnName = entry.fxnName,
                            .role = role,
                        });
                    }
                };
                addOwners(entry.groundAddresses, "ground");
                addOwners(entry.objectAddresses, "object");
            }
            result.extractedSpatialBlocks.push_back(std::move(block));

            if (resource.grnd.has_value()) {
                const model::IndexEntry* owner = nullptr;
                for (const auto& record : file.entries) {
                    if (record.entry.groundAddresses &&
                        std::find(record.entry.groundAddresses->values.begin(), record.entry.groundAddresses->values.end(), address)
                            != record.entry.groundAddresses->values.end()) {
                        owner = &record.entry;
                        break;
                    }
                }
                GrndSurface surface{};
                surface.id = address;
                surface.sourceOffset = address;
                surface.mesh = resource.grnd->mesh;
                for (auto& vertex : surface.mesh.vertices) {
                    vertex.position = applyCoordinates(vertex.position, options.coordinates);
                }
                if (options.coordinates.reverseTriangleWinding) {
                    for (std::size_t i = 0; i + 2U < surface.mesh.indices.size(); i += 3U) {
                        std::swap(surface.mesh.indices[i + 1U], surface.mesh.indices[i + 2U]);
                    }
                }
                if (owner != nullptr) {
                    surface.transform = owner->transform;
                    if (owner->groundLinks) {
                        surface.linkedGrndIds = owner->groundLinks->values;
                    }
                }
                result.world.grndSurfaces.push_back(std::move(surface));
            }
        }

        result.searchWorld.surfaces.reserve(result.world.grndSurfaces.size());
        for (const auto& grnd : result.world.grndSurfaces) {
            result.searchWorld.surfaces.push_back(WalkSurfaceNode{
                .grndId = grnd.id,
                .neighborGrndIds = grnd.linkedGrndIds,
                .mesh = grnd.mesh,
            });
        }

        if (options.buildBlenderIntermediateIr) {
            Sa3dBlenderIrBuilder builder{};
            result.blenderIrScene = builder.build(result);
            result.blenderIrDiagnostics = result.blenderIrScene->diagnostics;
        }
        if (options.exportBlenderIrJson) {
            result.diagnostics.push_back(ParseDiagnostic{
                .severity = ParseDiagnostic::Severity::Warning,
                .message = "Parser-owned Blender IR file output is deprecated; SpiceFileParsing must write projection artifacts.",
            });
        }
    }

    if (options.emitFxnHistogram) {
        for (const auto& [name, count] : histogram) {
            result.fxnHistogram.emplace_back(name, count);
        }
        std::sort(result.fxnHistogram.begin(), result.fxnHistogram.end());
    }
    if (!file.groundResources.empty()) {
        std::unordered_map<std::string, std::size_t> chunks{};
        for (const auto& [_, resource] : file.groundResources) {
            ++chunks[resource.tag.empty() ? "????" : resource.tag];
        }
        for (const auto& [tag, count] : chunks) {
            result.chunkTypeHistogram.emplace_back(tag, count);
        }
        std::sort(result.chunkTypeHistogram.begin(), result.chunkTypeHistogram.end());
    }
    return result;
}

ParseResult MldParser::parse(std::span<const std::uint8_t> mldBytes, const ParseOptions& options) const {
    return project(parseBytes(mldBytes), options);
}

std::vector<ExtractedNjBlock> MldParser::extractNjBlocks(
    std::span<const std::uint8_t> mldBytes,
    const ParseOptions& options) const {
    const auto parsed = parse(mldBytes, options);
    return parsed.extractedNjBlocks;
}

std::vector<ExtractedMldSpatialBlock> MldParser::extractGrndGobjBlocks(
    std::span<const std::uint8_t> mldBytes,
    const ParseOptions& options) const {
    ParseOptions extractOptions = options;
    extractOptions.extractGrndGobjBlocks = true;
    const auto parsed = parse(mldBytes, extractOptions);
    return parsed.extractedSpatialBlocks;
}

std::string formatParseSummary(const ParseResult& parseResult) {
    std::ostringstream out;
    out << "grndSurfaces=" << parseResult.world.grndSurfaces.size() << '\n';
    out << "collisions=" << parseResult.world.collisions.size() << '\n';
    out << "triggers=" << parseResult.world.triggers.size() << '\n';
    out << "unknownEntries=" << parseResult.world.unknownEntries.size() << '\n';
    out << "entryList=" << parseResult.entryList.size() << '\n';
    out << "searchSurfaces=" << parseResult.searchWorld.surfaces.size() << '\n';
    out << "searchRegions=" << parseResult.searchWorld.regions.size() << '\n';
    out << "extractedNjBlocks=" << parseResult.extractedNjBlocks.size() << '\n';
    out << "extractedSpatialBlocks=" << parseResult.extractedSpatialBlocks.size() << '\n';
    out << "blenderIrMeshes=" << (parseResult.blenderIrScene.has_value() ? parseResult.blenderIrScene->meshes.size() : 0) << '\n';
    out << "blenderIrIndexEntries=" << (parseResult.blenderIrScene.has_value() ? parseResult.blenderIrScene->indexEntries.size() : 0) << '\n';

    if (!parseResult.chunkTypeHistogram.empty()) {
        out << "chunkTypes:" << '\n';
        for (const auto& [tag, count] : parseResult.chunkTypeHistogram) {
            out << "  - " << tag << ": " << count << '\n';
        }
    }

    if (!parseResult.fxnHistogram.empty()) {
        out << "fxnHistogram:" << '\n';
        for (const auto& [fxn, count] : parseResult.fxnHistogram) {
            out << "  - " << fxn << ": " << count << '\n';
        }
    }

    if (!parseResult.diagnostics.empty()) {
        out << "diagnostics:" << '\n';
        for (const auto& diagnostic : parseResult.diagnostics) {
            const char* severity = "info";
            switch (diagnostic.severity) {
            case ParseDiagnostic::Severity::Info:
                severity = "info";
                break;
            case ParseDiagnostic::Severity::Warning:
                severity = "warning";
                break;
            case ParseDiagnostic::Severity::Error:
                severity = "error";
                break;
            }
            out << "  - [" << severity << "] " << diagnostic.message << '\n';
        }
    }

    return out.str();
}

} // namespace spice::mld::parsing
