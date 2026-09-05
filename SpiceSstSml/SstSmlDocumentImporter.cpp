#include "SstSmlDocumentImporter.h"

#include "SmlParser.h"
#include "SstParser.h"
#include "SstSmlSha256.h"
#include "SstSmlDocumentValidator.h"
#include "../Compression/Aklz.h"
#include "../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace spice::sstsml {
namespace {

using spice::root::Endian;
using spice::root::EndianReader;
using detail::CommandFieldSummary;
using detail::CommandFieldWidth;
using detail::DiagnosticSeverity;
using detail::ParseDiagnostic;
using detail::SmlParseResult;
using detail::SmlParser;
using detail::SstParseResult;
using detail::SstParser;

constexpr std::uint32_t kSmlTableOffset = 0x08U;
constexpr std::uint32_t kSmlRecordStride = 0x10U;
constexpr std::uint32_t kSstRecordStride = 0x10U;
constexpr std::uint32_t kSstCommandRecordStride = 0x10U;

struct DecodedMember {
    std::vector<std::uint8_t> storage{};
    std::span<const std::uint8_t> bytes{};
};

void addDiagnostic(SstSmlDocumentImportResult& result,
    SstSmlDiagnosticSeverity severity,
    SstSmlSourceMember source,
    std::string message,
    std::optional<std::uint64_t> decodedOffset = std::nullopt,
    std::optional<SmlEmbeddedResourceId> embeddedResourceId = std::nullopt) {
    result.diagnostics.push_back(SstSmlDocumentDiagnostic{
        severity, source, std::move(message), decodedOffset, embeddedResourceId });
}

SstSmlDiagnosticSeverity convertSeverity(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Info: return SstSmlDiagnosticSeverity::Info;
    case DiagnosticSeverity::Warning: return SstSmlDiagnosticSeverity::Warning;
    case DiagnosticSeverity::Error: return SstSmlDiagnosticSeverity::Error;
    }
    return SstSmlDiagnosticSeverity::Error;
}

void copyDiagnostics(SstSmlDocumentImportResult& destination,
    SstSmlSourceMember source,
    const std::vector<ParseDiagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        addDiagnostic(destination,
            convertSeverity(diagnostic.severity),
            source,
            diagnostic.message,
            diagnostic.offset);
    }
}

bool hasErrors(const SstSmlDocumentImportResult& result) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == SstSmlDiagnosticSeverity::Error;
    });
}

std::vector<std::uint8_t> copyRange(std::span<const std::uint8_t> bytes,
    std::uint64_t offset,
    std::uint64_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        return {};
    }
    const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
    return std::vector<std::uint8_t>(begin, begin + static_cast<std::ptrdiff_t>(size));
}

bool decodeMember(std::span<const std::uint8_t> raw,
    SstSmlSourceMember source,
    SstSmlSourceReceipt& receipt,
    DecodedMember& decoded,
    SstSmlDocumentImportResult& result) {
    receipt.bytesRead = true;
    receipt.rawSize = raw.size();
    receipt.rawSha256 = detail::sha256(raw);
    if (raw.empty()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, source, "Input is empty");
        return false;
    }

    if (!spice::compression::aklz::isAklz(raw)) {
        receipt.wrapper = SstSmlSourceWrapper::Raw;
        receipt.decodedSize = raw.size();
        decoded.bytes = raw;
        return true;
    }

    receipt.wrapper = SstSmlSourceWrapper::Aklz;
    auto decompressed = spice::compression::aklz::decompress(raw);
    if (!decompressed.ok()) {
        addDiagnostic(result,
            SstSmlDiagnosticSeverity::Error,
            source,
            "AKLZ decompression failed: " +
                std::string(spice::compression::aklz::errorToString(decompressed.error)));
        return false;
    }
    decoded.storage = std::move(decompressed.bytes);
    receipt.decodedSize = decoded.storage.size();
    decoded.bytes = decoded.storage;
    return true;
}

SstSmlOpaqueBlock makeOpaque(SstSmlOpaqueBlockId id,
    std::span<const std::uint8_t> bytes,
    std::uint64_t offset,
    std::uint64_t size) {
    return SstSmlOpaqueBlock{ id, copyRange(bytes, offset, size) };
}

std::optional<SstCommandFieldValue> readCommandFieldValue(
    std::span<const std::uint8_t> payload,
    Endian endian,
    const CommandFieldSummary& field) {
    const EndianReader reader(payload, endian);
    switch (field.width) {
    case CommandFieldWidth::I8:
        if (const auto value = reader.try_read_i8(field.offset)) return SstCommandFieldValue{ *value };
        break;
    case CommandFieldWidth::U8:
        if (const auto value = reader.try_read_u8(field.offset)) return SstCommandFieldValue{ *value };
        break;
    case CommandFieldWidth::I16:
        if (const auto value = reader.try_read_i16(field.offset)) return SstCommandFieldValue{ *value };
        break;
    case CommandFieldWidth::U16:
        if (const auto value = reader.try_read_u16(field.offset)) return SstCommandFieldValue{ *value };
        break;
    case CommandFieldWidth::U32:
        if (const auto value = reader.try_read_u32(field.offset)) return SstCommandFieldValue{ *value };
        break;
    case CommandFieldWidth::F32:
        if (const auto value = reader.try_read_f32(field.offset)) return SstCommandFieldValue{ *value };
        break;
    }
    return std::nullopt;
}

std::uint32_t commandFieldWidth(const CommandFieldWidth width) {
    switch (width) {
    case CommandFieldWidth::I8:
    case CommandFieldWidth::U8: return 1U;
    case CommandFieldWidth::I16:
    case CommandFieldWidth::U16: return 2U;
    case CommandFieldWidth::U32:
    case CommandFieldWidth::F32: return 4U;
    }
    return 0U;
}

bool isSpecializedField(const std::int16_t commandType, const CommandFieldSummary& field) {
    if (commandType == 1) return true;
    return commandType == 0 && field.offset >= 0x1CU && field.offset < 0x40U;
}

void markRange(std::vector<bool>& covered, const std::uint32_t offset, const std::uint32_t size) {
    if (offset > covered.size() || size > covered.size() - offset) return;
    std::fill(covered.begin() + offset, covered.begin() + offset + size, true);
}

void buildCommandOpaqueFragments(SstStageCommand& command,
    const detail::SstCommandRecord& source,
    std::uint64_t& nextOpaqueId) {
    if (!source.typeKnown || source.payloadBytes.empty()) return;
    std::vector<bool> covered(source.payloadBytes.size(), false);
    for (const auto& field : command.fields) {
        const auto width = std::visit([](const auto value) -> std::uint32_t { return sizeof(value); }, field.value);
        markRange(covered, field.payloadOffset, width);
    }
    if (command.placement) markRange(covered, 0x1CU, 0x24U);
    if (command.type == 1) {
        const auto summaries = SstParser::fieldSummariesForType(1);
        for (std::size_t row = 0U; row < command.lightingRows.size(); ++row) {
            const auto base = static_cast<std::uint32_t>(row * 0x68U);
            for (const auto& field : summaries) {
                markRange(covered, base + field.offset, commandFieldWidth(field.width));
            }
        }
    }
    std::size_t cursor = 0U;
    while (cursor < covered.size()) {
        if (covered[cursor]) {
            ++cursor;
            continue;
        }
        const auto begin = cursor;
        while (cursor < covered.size() && !covered[cursor]) ++cursor;
        command.opaquePayloadFragments.push_back({
            SstSmlOpaqueBlockId{ nextOpaqueId++ },
            static_cast<std::uint32_t>(begin),
            std::vector<std::uint8_t>(source.payloadBytes.begin() + static_cast<std::ptrdiff_t>(begin),
                source.payloadBytes.begin() + static_cast<std::ptrdiff_t>(cursor)),
        });
    }
}

bool buildSmlLayout(SstSmlDocument& document,
    const SmlParseResult& parsed,
    std::span<const std::uint8_t> decoded,
    std::uint64_t& nextOpaqueId,
    SstSmlDocumentImportResult& result) {
    struct Range {
        std::uint64_t offset{};
        std::uint64_t size{};
        std::size_t memberIndex{};
    };

    const std::uint64_t tableEnd = kSmlTableOffset +
        static_cast<std::uint64_t>(parsed.records.size()) * kSmlRecordStride;
    std::vector<Range> ranges;
    ranges.reserve(parsed.records.size());
    for (std::size_t index = 0U; index < parsed.records.size(); ++index) {
        const auto& record = parsed.records[index];
        if (record.embeddedMldOffset < tableEnd ||
            record.embeddedMldOffset > decoded.size() ||
            record.embeddedMldSize > decoded.size() - record.embeddedMldOffset) {
            addDiagnostic(result,
                SstSmlDiagnosticSeverity::Error,
                SstSmlSourceMember::Sml,
                "SML embedded resource does not occupy a valid post-table span",
                record.embeddedMldOffset);
            return false;
        }
        ranges.push_back(Range{ record.embeddedMldOffset, record.embeddedMldSize, index });
    }
    std::stable_sort(ranges.begin(), ranges.end(), [](const Range& left, const Range& right) {
        if (left.offset != right.offset) return left.offset < right.offset;
        return left.memberIndex < right.memberIndex;
    });

    std::uint64_t cursor = tableEnd;
    for (const auto& range : ranges) {
        if (range.offset < cursor && range.size != 0U) {
            addDiagnostic(result,
                SstSmlDiagnosticSeverity::Error,
                SstSmlSourceMember::Sml,
                "SML embedded resource spans overlap",
                range.offset);
            return false;
        }
        if (range.offset > cursor) {
            document.smlBodyLayout.emplace_back(makeOpaque(
                SstSmlOpaqueBlockId{ nextOpaqueId++ }, decoded, cursor, range.offset - cursor));
        }
        document.smlBodyLayout.emplace_back(document.members[range.memberIndex].sml.resource.id);
        cursor = std::max(cursor, range.offset + range.size);
    }
    if (cursor < decoded.size()) {
        document.smlBodyLayout.emplace_back(makeOpaque(
            SstSmlOpaqueBlockId{ nextOpaqueId++ }, decoded, cursor, decoded.size() - cursor));
    }
    return true;
}

bool buildSstLayout(SstSmlDocument& document,
    const SstParseResult& parsed,
    std::span<const std::uint8_t> decoded,
    std::uint64_t& nextOpaqueId,
    SstSmlDocumentImportResult& result) {
    struct BlockRef {
        std::uint64_t offset{};
        std::size_t memberIndex{};
    };

    const std::uint64_t tableEnd = static_cast<std::uint64_t>(parsed.topLevelRecords.size()) * kSstRecordStride;
    std::vector<BlockRef> blocks;
    blocks.reserve(parsed.topLevelRecords.size());
    for (std::size_t index = 0U; index < parsed.topLevelRecords.size(); ++index) {
        const auto offset = parsed.topLevelRecords[index].commandBlockOffset;
        if (offset < tableEnd || offset >= decoded.size()) {
            addDiagnostic(result,
                SstSmlDiagnosticSeverity::Error,
                SstSmlSourceMember::Sst,
                "SST command block does not begin in the post-table body",
                offset);
            return false;
        }
        blocks.push_back(BlockRef{ offset, index });
    }
    std::stable_sort(blocks.begin(), blocks.end(), [](const BlockRef& left, const BlockRef& right) {
        if (left.offset != right.offset) return left.offset < right.offset;
        return left.memberIndex < right.memberIndex;
    });
    for (std::size_t index = 1U; index < blocks.size(); ++index) {
        if (blocks[index - 1U].offset == blocks[index].offset) {
            addDiagnostic(result,
                SstSmlDiagnosticSeverity::Error,
                SstSmlSourceMember::Sst,
                "SST top-level records share a command-block offset",
                blocks[index].offset);
            return false;
        }
    }

    std::uint64_t cursor = tableEnd;
    for (const auto& ref : blocks) {
        if (ref.offset < cursor) {
            addDiagnostic(result,
                SstSmlDiagnosticSeverity::Error,
                SstSmlSourceMember::Sst,
                "SST command blocks overlap",
                ref.offset);
            return false;
        }
        if (ref.offset > cursor) {
            document.sstBodyLayout.emplace_back(makeOpaque(
                SstSmlOpaqueBlockId{ nextOpaqueId++ }, decoded, cursor, ref.offset - cursor));
        }

        const auto& parsedBlock = parsed.commandBlocks[ref.memberIndex];
        const auto& block = document.members[ref.memberIndex].sst.commandBlock;
        const std::uint64_t representedSize = 4U +
            static_cast<std::uint64_t>(block.commands.size()) * kSstCommandRecordStride +
            kSstCommandRecordStride +
            [&]() {
                std::uint64_t total = 0U;
                for (const auto& command : block.commands) {
                    if (command.payloadSpanKnown) total += SstParser::commandPayloadSize(command.type);
                }
                return total;
            }() +
            (block.battleGrid.has_value() ? block.battleGrid->values.size() : 0U) +
            (block.trailingOpaque ? block.trailingOpaque->bytes.size() : 0U);
        const std::uint64_t expectedSize = parsedBlock.nextCommandBlockOffset - parsedBlock.blockOffset;
        if (representedSize != expectedSize) {
            addDiagnostic(result,
                SstSmlDiagnosticSeverity::Error,
                SstSmlSourceMember::Sst,
                "SST command block byte ownership is incomplete",
                ref.offset);
            return false;
        }

        document.sstBodyLayout.emplace_back(block.id);
        cursor = ref.offset + expectedSize;
    }
    if (cursor < decoded.size()) {
        document.sstBodyLayout.emplace_back(makeOpaque(
            SstSmlOpaqueBlockId{ nextOpaqueId++ }, decoded, cursor, decoded.size() - cursor));
    }
    return true;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

std::optional<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path,
    SstSmlSourceMember source,
    SstSmlDocumentImportResult& result) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, source,
            "Could not open input file: " + path.string());
        return std::nullopt;
    }
    const auto end = input.tellg();
    if (end < 0) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, source,
            "Could not determine input file size: " + path.string());
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), end)) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, source,
            "Could not read complete input file: " + path.string());
        return std::nullopt;
    }
    return bytes;
}

} // namespace

bool SstSmlDocumentImportResult::ok() const {
    return document.has_value() && !hasErrors(*this);
}

const spice::mld::MldImportReceipt* SstSmlDocumentImportReceipt::embeddedMld(
    const SmlEmbeddedResourceId resourceId) const noexcept {
    const auto found = std::find_if(embeddedMlds.begin(), embeddedMlds.end(), [&](const auto& value) {
        return value.resourceId == resourceId;
    });
    return found == embeddedMlds.end() ? nullptr : &found->receipt;
}

SstSmlDocumentImportResult SstSmlDocumentImporter::importBytes(
    std::span<const std::uint8_t> smlBytes,
    std::span<const std::uint8_t> sstBytes) {
    SstSmlDocumentImportResult result{};
    DecodedMember decodedSml{};
    DecodedMember decodedSst{};
    const bool smlDecoded = decodeMember(smlBytes,
        SstSmlSourceMember::Sml, result.receipt.sml, decodedSml, result);
    const bool sstDecoded = decodeMember(sstBytes,
        SstSmlSourceMember::Sst, result.receipt.sst, decodedSst, result);
    if (!smlDecoded || !sstDecoded) return result;
    if (decodedSml.bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        decodedSst.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "Decoded pair exceeds the supported 32-bit file-size range");
        return result;
    }

    const auto sml = SmlParser::parse(decodedSml.bytes);
    const auto sst = SstParser::parse(decodedSst.bytes);
    copyDiagnostics(result, SstSmlSourceMember::Sml, sml.diagnostics);
    copyDiagnostics(result, SstSmlSourceMember::Sst, sst.diagnostics);
    if (!sml.ok() || !sst.ok()) return result;
    result.receipt.sml.endian = sml.sourceEndian;
    result.receipt.sst.endian = sst.sourceEndian;

    if (sml.sourceEndian != sst.sourceEndian) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "SML and SST byte orders do not agree");
    }
    if (sml.records.empty() || sst.topLevelRecords.empty()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "A battle-stage pair must contain at least one stage member");
    }
    if (sml.records.size() != sst.topLevelRecords.size() ||
        sst.topLevelRecords.size() != sst.commandBlocks.size()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "SML and SST top-level record counts do not agree");
    }
    const EndianReader smlReader(decodedSml.bytes, sml.sourceEndian);
    const EndianReader sstReader(decodedSst.bytes, sst.sourceEndian);
    const auto smlStageId = smlReader.read_u16(0U);
    const auto smlStageSentinel = smlReader.read_u16(2U);
    const auto smlCountSentinel = smlReader.read_u16(6U);
    if (!sst.topLevelRecords.empty() &&
        (smlStageId != sstReader.read_u16(0U) ||
            smlStageSentinel != sstReader.read_u16(2U) ||
            sml.recordCount != sstReader.read_u16(4U) ||
            smlCountSentinel != sstReader.read_u16(6U))) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "SML and SST paired header fields do not agree");
    }
    if (hasErrors(result)) return result;

    SstSmlDocument document{};
    document.stageId = smlStageId;
    document.stageHeaderSentinel = smlStageSentinel;
    document.recordCountSentinel = smlCountSentinel;
    document.members.reserve(sml.records.size());
    std::uint64_t nextCommandId = 1U;
    std::uint64_t nextCommandFieldId = 1U;
    std::uint64_t nextPlacementId = 1U;
    std::uint64_t nextLightingRowId = 1U;
    std::uint64_t nextTerrainId = 1U;
    std::uint64_t nextOpaqueId = 1U;
    for (std::size_t index = 0U; index < sml.records.size(); ++index) {
        const auto& smlSource = sml.records[index];
        const auto& sstSource = sst.topLevelRecords[index];
        const auto& blockSource = sst.commandBlocks[index];

        SstSmlStageMember member{};
        member.id = SstSmlStageMemberId{ index + 1U };
        member.sml.id = SmlRecordId{ index + 1U };
        member.sml.resourceIndexWord = smlSource.rawWord0;
        member.sml.reservedWord = smlSource.rawWord12;
        member.sml.resource.id = SmlEmbeddedResourceId{ index + 1U };
        auto embeddedMld = spice::mld::MldDocumentImporter::importBytes(smlSource.embeddedMldBytes);
        if (embeddedMld.ok()) {
            member.sml.resource.content = std::move(*embeddedMld.document);
            result.receipt.embeddedMlds.push_back({ member.sml.resource.id, std::move(embeddedMld.receipt) });
            for (const auto& diagnostic : embeddedMld.diagnostics) {
                addDiagnostic(result,
                    diagnostic.severity == spice::mld::MldDiagnosticSeverity::Info
                        ? SstSmlDiagnosticSeverity::Info : SstSmlDiagnosticSeverity::Warning,
                    SstSmlSourceMember::Sml,
                    "Embedded MLD: " + diagnostic.message,
                    diagnostic.decodedOffset,
                    member.sml.resource.id);
            }
        } else {
            member.sml.resource.content = SmlOpaqueEmbeddedResource{ smlSource.embeddedMldBytes };
            if (embeddedMld.diagnostics.empty()) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Warning, SstSmlSourceMember::Sml,
                    "Embedded resource could not be decoded as MLD and remains opaque",
                    smlSource.embeddedMldOffset, member.sml.resource.id);
            }
            for (const auto& diagnostic : embeddedMld.diagnostics) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Warning, SstSmlSourceMember::Sml,
                    "Embedded MLD decode failed; payload remains opaque: " + diagnostic.message,
                    diagnostic.decodedOffset, member.sml.resource.id);
            }
        }

        member.sst.id = SstRecordId{ index + 1U };
        if (index != 0U) {
            member.sst.previousCommandBlockLength = sstSource.rawWord0;
            member.sst.reservedWord = sstSource.rawWord4;
        }
        member.sst.recordIndexWord = sstSource.rawWord8;
        auto& block = member.sst.commandBlock;
        block.id = SstCommandBlockId{ index + 1U };
        block.sentinelType = blockSource.sentinelType;
        block.sentinelArgument = blockSource.sentinelArgument;
        EndianReader sentinelReader(decodedSst.bytes, sst.sourceEndian);
        block.sentinelRawWord4 = sentinelReader.read_u32(blockSource.sentinelOffset + 4U);
        block.sentinelRawWord8 = sentinelReader.read_u32(blockSource.sentinelOffset + 8U);
        block.sentinelRawWord12 = sentinelReader.read_u32(blockSource.sentinelOffset + 12U);
        block.commands.reserve(blockSource.commands.size());
        for (const auto& commandSource : blockSource.commands) {
            SstStageCommand command{};
            command.id = SstCommandId{ nextCommandId++ };
            command.type = commandSource.type;
            command.argument = commandSource.argument;
            command.rawWord4 = commandSource.rawWord4;
            command.rawWord8 = commandSource.rawWord8;
            command.onDiskWord12 = commandSource.onDiskWord12;
            command.payloadSpanKnown = commandSource.typeKnown;
            for (const auto& fieldSource : commandSource.fieldSummaries) {
                if (isSpecializedField(commandSource.type, fieldSource)) continue;
                const auto value = readCommandFieldValue(commandSource.payloadBytes,
                    sst.sourceEndian,
                    fieldSource);
                if (!value.has_value()) {
                    addDiagnostic(result,
                        SstSmlDiagnosticSeverity::Error,
                        SstSmlSourceMember::Sst,
                        "Supported SST command field extends beyond its payload",
                        commandSource.payloadOffset + fieldSource.offset);
                    return result;
                }
                command.fields.push_back(SstCommandField{
                    SstCommandFieldId{ nextCommandFieldId++ }, fieldSource.offset, fieldSource.name, *value });
            }
            if (commandSource.type == 0 && commandSource.payloadBytes.size() >= 0x40U) {
                const EndianReader payloadReader(commandSource.payloadBytes, sst.sourceEndian);
                command.placement = SstPlacement{
                    SstPlacementId{ nextPlacementId++ },
                    payloadReader.read_f32(0x1CU),
                    payloadReader.read_f32(0x20U),
                    payloadReader.read_f32(0x24U),
                    payloadReader.read_u32(0x28U),
                    payloadReader.read_u32(0x2CU),
                    payloadReader.read_u32(0x30U),
                    payloadReader.read_f32(0x34U),
                    payloadReader.read_f32(0x38U),
                    payloadReader.read_f32(0x3CU),
                };
            }
            command.lightingRows.reserve(commandSource.type1LightingRows.size());
            for (const auto& rowSource : commandSource.type1LightingRows) {
                command.lightingRows.push_back(SstLightingRow{
                    SstLightingRowId{ nextLightingRowId++ },
                    rowSource.state,
                    rowSource.classSelector,
                    rowSource.flags,
                    rowSource.runtimeSlotId,
                    { rowSource.lightVector.x, rowSource.lightVector.y, rowSource.lightVector.z },
                    { rowSource.slotRgb.x, rowSource.slotRgb.y, rowSource.slotRgb.z },
                    { rowSource.globalRgb.x, rowSource.globalRgb.y, rowSource.globalRgb.z },
                    rowSource.attenuationOrSpot0,
                    rowSource.attenuationOrSpot1,
                    rowSource.rawTailWord,
                    rowSource.sentinel,
                });
            }
            buildCommandOpaqueFragments(command, commandSource, nextOpaqueId);
            block.commands.push_back(std::move(command));
        }
        const bool hasUnknownCommand = std::any_of(blockSource.commands.begin(), blockSource.commands.end(),
            [](const auto& command) { return !command.typeKnown; });
        if (!hasUnknownCommand && blockSource.battleGridTerrainSource.has_value()) {
            block.battleGrid = SstBattleGridTerrain{
                SstBattleGridTerrainId{ nextTerrainId++ },
                blockSource.battleGridTerrainSource->source9x9 };
            if (!blockSource.battleGridTerrainSource->paddingAfterSource.empty()) {
                block.trailingOpaque = SstSmlOpaqueBlock{
                    SstSmlOpaqueBlockId{ nextOpaqueId++ },
                    blockSource.battleGridTerrainSource->paddingAfterSource };
            }
        } else {
            if (!blockSource.postCommandTailBytes.empty()) {
                block.trailingOpaque = SstSmlOpaqueBlock{
                    SstSmlOpaqueBlockId{ nextOpaqueId++ },
                    blockSource.postCommandTailBytes };
            }
        }
        document.members.push_back(std::move(member));
    }

    if (!buildSmlLayout(document, sml, decodedSml.bytes, nextOpaqueId, result) ||
        !buildSstLayout(document, sst, decodedSst.bytes, nextOpaqueId, result)) {
        return result;
    }
    const auto validation = SstSmlDocumentValidator::validate(document, &result.receipt);
    result.diagnostics.insert(result.diagnostics.end(),
        validation.diagnostics.begin(), validation.diagnostics.end());
    if (!validation.ok()) return result;
    result.document = std::move(document);
    return result;
}

SstSmlDocumentImportResult SstSmlDocumentImporter::importFile(
    const std::filesystem::path& eitherPairPath) {
    SstSmlDocumentImportResult result{};
    std::error_code error;
    if (!std::filesystem::is_regular_file(eitherPairPath, error)) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "Input path is not a regular file: " + eitherPairPath.string());
        return result;
    }
    const auto extension = lowercase(eitherPairPath.extension().string());
    if (extension != ".sml" && extension != ".sst") {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "Input path must name either an .sml or .sst file");
        return result;
    }

    const auto desiredExtension = extension == ".sml" ? ".sst" : ".sml";
    const auto desiredStem = lowercase(eitherPairPath.stem().string());
    std::vector<std::filesystem::path> siblings;
    for (std::filesystem::directory_iterator iterator(eitherPairPath.parent_path(), error), end;
        !error && iterator != end;
        iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) continue;
        const auto candidate = iterator->path();
        if (lowercase(candidate.stem().string()) == desiredStem &&
            lowercase(candidate.extension().string()) == desiredExtension) {
            siblings.push_back(candidate);
        }
    }
    if (error) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "Could not enumerate the input directory: " + error.message());
        return result;
    }
    if (siblings.size() != 1U) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            siblings.empty()
                ? "The side-by-side SML/SST companion is missing"
                : "More than one case-insensitive SML/SST sibling matched the input");
        return result;
    }

    const auto smlPath = extension == ".sml" ? eitherPairPath : siblings.front();
    const auto sstPath = extension == ".sst" ? eitherPairPath : siblings.front();
    result.receipt.sml.path = smlPath;
    result.receipt.sst.path = sstPath;
    auto smlBytes = readFile(smlPath, SstSmlSourceMember::Sml, result);
    auto sstBytes = readFile(sstPath, SstSmlSourceMember::Sst, result);
    if (!smlBytes.has_value() || !sstBytes.has_value()) return result;

    auto imported = importBytes(*smlBytes, *sstBytes);
    imported.receipt.sml.path = smlPath;
    imported.receipt.sst.path = sstPath;
    return imported;
}

const char* toString(SstSmlDiagnosticSeverity severity) noexcept {
    switch (severity) {
    case SstSmlDiagnosticSeverity::Info: return "info";
    case SstSmlDiagnosticSeverity::Warning: return "warning";
    case SstSmlDiagnosticSeverity::Error: return "error";
    }
    return "unknown";
}

const char* toString(SstSmlSourceMember source) noexcept {
    switch (source) {
    case SstSmlSourceMember::Pair: return "pair";
    case SstSmlSourceMember::Sml: return "sml";
    case SstSmlSourceMember::Sst: return "sst";
    }
    return "unknown";
}

const char* toString(SstSmlSourceWrapper wrapper) noexcept {
    switch (wrapper) {
    case SstSmlSourceWrapper::Raw: return "raw";
    case SstSmlSourceWrapper::Aklz: return "aklz";
    }
    return "unknown";
}

} // namespace spice::sstsml
