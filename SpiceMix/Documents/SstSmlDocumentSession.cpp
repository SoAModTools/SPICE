#include "SstSmlDocumentSession.h"

#include "DocumentSupport.h"
#include "MldInspectionSupport.h"
#include "../../SpiceMLD/Parsing/MldParser.h"
#include "../../SpiceSstSml/BattleStageParser.h"
#include "../../SpiceSstSml/SstCommandCatalog.h"
#include "../../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace spice::mix {
namespace {

using spice::sstsml::CommandFieldEvidence;
using spice::sstsml::CommandFieldKind;
using spice::sstsml::CommandFieldScope;
using spice::sstsml::CommandFieldSummary;
using spice::sstsml::CommandFieldWidth;
using spice::sstsml::DiagnosticSeverity;

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

struct PairPaths {
    std::filesystem::path sml{};
    std::filesystem::path sst{};
};

DocumentResult resolvePairPaths(const std::filesystem::path& input, PairPaths& out) {
    if (input.empty() || !std::filesystem::is_regular_file(input)) {
        return documents::failure("A readable SML or SST input file is required.");
    }
    const auto extension = asciiLower(input.extension().string());
    if (extension != ".sml" && extension != ".sst") {
        return documents::failure("The battle-stage workbench requires an .sml or .sst file.");
    }
    const auto wantedExtension = extension == ".sml" ? ".sst" : ".sml";
    const auto wantedStem = asciiLower(input.stem().string());
    const auto directory = input.has_parent_path() ? input.parent_path() : std::filesystem::current_path();
    std::vector<std::filesystem::path> companions{};
    std::error_code error{};
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        const auto& candidate = it->path();
        if (asciiLower(candidate.extension().string()) == wantedExtension
            && asciiLower(candidate.stem().string()) == wantedStem) {
            companions.push_back(candidate);
        }
    }
    if (error) {
        return documents::failure("Could not inspect the input directory for the companion file: " + error.message());
    }
    if (companions.empty()) {
        return documents::failure(std::string("The same-directory, same-stem ")
            + wantedExtension + " companion is missing.");
    }
    if (companions.size() != 1U) {
        return documents::failure("The SST/SML companion is ambiguous because multiple case-insensitive matches exist.");
    }
    if (extension == ".sml") {
        out.sml = input;
        out.sst = companions.front();
    } else {
        out.sst = input;
        out.sml = companions.front();
    }
    return { .message = "Resolved SST/SML pair." };
}

EventLevel eventLevel(const DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Warning: return EventLevel::Warning;
    case DiagnosticSeverity::Error: return EventLevel::Error;
    default: return EventLevel::Info;
    }
}

std::string hexBytes(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out{};
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return out.str();
}

std::size_t widthBytes(const CommandFieldWidth width) {
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

std::string widthName(const CommandFieldWidth width) {
    switch (width) {
    case CommandFieldWidth::I8: return "i8";
    case CommandFieldWidth::U8: return "u8";
    case CommandFieldWidth::I16: return "i16";
    case CommandFieldWidth::U16: return "u16";
    case CommandFieldWidth::U32: return "u32";
    case CommandFieldWidth::F32: return "f32";
    }
    return "unknown";
}

std::string kindName(const CommandFieldKind kind) {
    switch (kind) {
    case CommandFieldKind::ModelIndex: return "Model index";
    case CommandFieldKind::RuntimeSlot: return "Runtime slot";
    case CommandFieldKind::LookupKey: return "Lookup key";
    case CommandFieldKind::RawWord: return "Raw word";
    case CommandFieldKind::HalfwordParameter: return "Halfword parameter";
    case CommandFieldKind::FloatParameter: return "Float parameter";
    case CommandFieldKind::RuntimePointer: return "Runtime pointer";
    case CommandFieldKind::VectorComponent: return "Vector component";
    case CommandFieldKind::RotationComponent: return "Rotation component";
    case CommandFieldKind::VectorDelta: return "Vector delta";
    case CommandFieldKind::Duration: return "Duration";
    case CommandFieldKind::Counter: return "Counter";
    case CommandFieldKind::AxisSelector: return "Axis selector";
    case CommandFieldKind::BufferPointer: return "Buffer pointer";
    case CommandFieldKind::ReservedRaw: return "Reserved/raw";
    }
    return "Unknown";
}

SstSmlFieldEvidence fieldEvidence(const CommandFieldEvidence evidence) {
    switch (evidence) {
    case CommandFieldEvidence::Gekko: return SstSmlFieldEvidence::Gekko;
    case CommandFieldEvidence::GekkoAndCorpus: return SstSmlFieldEvidence::GekkoAndCorpus;
    case CommandFieldEvidence::CorpusStable: return SstSmlFieldEvidence::CorpusStable;
    case CommandFieldEvidence::CodeSupportedCorpusAbsent: return SstSmlFieldEvidence::CodeSupportedCorpusAbsent;
    case CommandFieldEvidence::Provisional: return SstSmlFieldEvidence::Provisional;
    }
    return SstSmlFieldEvidence::Provisional;
}

std::string scopeName(const CommandFieldScope scope) {
    switch (scope) {
    case CommandFieldScope::StructuralPayload: return "Structural payload";
    case CommandFieldScope::ConsumerTrailing: return "Consumer trailing";
    case CommandFieldScope::RuntimeLocal: return "Runtime local";
    }
    return "Unknown";
}

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U)
        | static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, const std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U)
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U)
        | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

SstSmlCommandFieldSnapshot projectField(const CommandFieldSummary& field,
    const std::vector<std::uint8_t>& bytes,
    spice::root::Endian endian,
    const std::uint32_t logicalBaseOffset = 0U) {
    SstSmlCommandFieldSnapshot out{};
    out.offset = field.offset;
    out.width = widthName(field.width);
    out.kind = kindName(field.kind);
    out.name = field.name;
    out.evidence = fieldEvidence(field.evidence);
    out.scope = scopeName(field.scope);
    out.provisional = field.provisional;
    out.description = field.description;
    if (field.offset < logicalBaseOffset) return out;
    const auto localOffset = static_cast<std::size_t>(field.offset - logicalBaseOffset);
    const auto size = widthBytes(field.width);
    if (localOffset > bytes.size() || size > bytes.size() - localOffset) return out;
    std::vector<std::uint8_t> raw(bytes.begin() + static_cast<std::ptrdiff_t>(localOffset),
        bytes.begin() + static_cast<std::ptrdiff_t>(localOffset + size));
    out.rawHex = hexBytes(raw);
    out.valueAvailable = true;
    std::ostringstream value{};
    value << std::setprecision(9);
    const spice::root::EndianReader reader(bytes, endian);
    switch (field.width) {
    case CommandFieldWidth::I8:
        value << static_cast<int>(static_cast<std::int8_t>(bytes[localOffset]));
        break;
    case CommandFieldWidth::U8:
        value << static_cast<unsigned>(bytes[localOffset]);
        break;
    case CommandFieldWidth::I16:
        value << reader.read_i16(localOffset);
        break;
    case CommandFieldWidth::U16:
        value << reader.read_u16(localOffset);
        break;
    case CommandFieldWidth::U32: {
        const auto number = reader.read_u32(localOffset);
        value << number << " (0x" << std::uppercase << std::hex << number << ')';
        break;
    }
    case CommandFieldWidth::F32:
        value << reader.read_f32(localOffset);
        break;
    }
    out.value = value.str();
    return out;
}

std::string mldStatusName(const spice::mld::model::MldParseStatus status) {
    switch (status) {
    case spice::mld::model::MldParseStatus::Empty: return "Empty";
    case spice::mld::model::MldParseStatus::Partial: return "Partial";
    case spice::mld::model::MldParseStatus::Complete: return "Complete";
    case spice::mld::model::MldParseStatus::Failed: return "Failed";
    }
    return "Unknown";
}

} // namespace

struct SstSmlDocumentSession::Impl {
    PairPaths paths{};
    std::uint64_t smlSourceSize = 0;
    std::uint64_t sstSourceSize = 0;
    spice::sstsml::BattleStageParseResult pair{};
    std::vector<std::optional<spice::mld::model::MldFile>> embeddedMlds{};
    std::vector<SstSmlDiagnosticSnapshot> diagnostics{};
};

SstSmlDocumentSession::SstSmlDocumentSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SstSmlDocumentSession::~SstSmlDocumentSession() = default;
SstSmlDocumentSession::SstSmlDocumentSession(SstSmlDocumentSession&&) noexcept = default;
SstSmlDocumentSession& SstSmlDocumentSession::operator=(SstSmlDocumentSession&&) noexcept = default;

SstSmlDocumentSession::OpenResult SstSmlDocumentSession::open(
    const std::filesystem::path& eitherPairPath, const DocumentContext& context) {
    if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
    try {
        PairPaths paths{};
        auto resolution = resolvePairPaths(eitherPairPath, paths);
        if (!resolution.ok()) return { .result = std::move(resolution) };

        documents::emit(context, EventLevel::Progress, "Opening SML " + paths.sml.string());
        const auto smlBytes = documents::readBytes(paths.sml);
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
        documents::emit(context, EventLevel::Progress, "Opening SST " + paths.sst.string());
        const auto sstBytes = documents::readBytes(paths.sst);
        if (smlBytes.empty() || sstBytes.empty()) {
            return { .result = documents::failure("SST/SML pair members must not be empty.") };
        }
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };

        auto impl = std::make_unique<Impl>();
        impl->paths = paths;
        impl->smlSourceSize = smlBytes.size();
        impl->sstSourceSize = sstBytes.size();
        documents::emit(context, EventLevel::Progress, "Parsing paired SST/SML stage data.");
        impl->pair = spice::sstsml::BattleStageParser::parsePair(smlBytes, sstBytes,
            paths.sml.stem().string());

        auto appendDiagnostics = [&impl](const std::string& origin,
            const std::vector<spice::sstsml::ParseDiagnostic>& source) {
            for (const auto& diagnostic : source) {
                impl->diagnostics.push_back({
                    .level = eventLevel(diagnostic.severity),
                    .origin = origin,
                    .message = diagnostic.message,
                    .sourceOffset = diagnostic.offset,
                });
            }
        };
        appendDiagnostics("SML", impl->pair.sml.diagnostics);
        appendDiagnostics("SST", impl->pair.sst.diagnostics);
        appendDiagnostics("Pair", impl->pair.diagnostics);

        std::vector<std::string> diagnosticText{};
        for (const auto& diagnostic : impl->diagnostics) {
            diagnosticText.push_back(diagnostic.origin + ": " + diagnostic.message);
        }
        if (!impl->pair.ok()) {
            documents::emit(context, EventLevel::Error, "SST/SML pair parsing failed.");
            return { .result = documents::failure("SST/SML pair parsing failed.", std::move(diagnosticText)) };
        }
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };

        impl->embeddedMlds.resize(impl->pair.sml.records.size());
        for (std::size_t index = 0; index < impl->pair.sml.records.size(); ++index) {
            if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
            const auto& record = impl->pair.sml.records[index];
            if (!record.embeddedMldInBounds || record.embeddedMldBytes.empty()) continue;
            documents::emit(context, EventLevel::Progress,
                "Inspecting embedded MLD " + std::to_string(index + 1U) + "/"
                    + std::to_string(impl->pair.sml.records.size()) + ".");
            auto parsed = spice::mld::parsing::MldParser{}.parseBytes(record.embeddedMldBytes);
            const bool failed = parsed.parseStatus == spice::mld::model::MldParseStatus::Failed;
            for (const auto& diagnostic : parsed.parseDiagnostics) {
                EventLevel level = EventLevel::Info;
                if (diagnostic.severity == spice::mld::model::MldDiagnostic::Severity::Warning) level = EventLevel::Warning;
                if (diagnostic.severity == spice::mld::model::MldDiagnostic::Severity::Error) level = EventLevel::Error;
                impl->diagnostics.push_back({
                    .level = level,
                    .origin = "Embedded MLD",
                    .message = diagnostic.message,
                    .sourceOffset = diagnostic.sourceOffset,
                    .recordIndex = index,
                });
            }
            if (failed) {
                documents::emit(context, EventLevel::Warning,
                    "Embedded MLD record " + std::to_string(index) + " could not be fully parsed.");
            }
            impl->embeddedMlds[index] = std::move(parsed);
        }

        for (const auto& diagnostic : impl->diagnostics) {
            if (diagnostic.level == EventLevel::Warning) {
                documents::emit(context, EventLevel::Warning,
                    diagnostic.origin + ": " + diagnostic.message);
            }
        }
        auto session = std::shared_ptr<SstSmlDocumentSession>(new SstSmlDocumentSession(std::move(impl)));
        documents::emit(context, EventLevel::Info,
            "Opened SST/SML battle stage " + paths.sml.stem().string() + ".");
        return { .session = std::move(session),
            .result = { .message = "SST/SML document is ready.", .diagnostics = std::move(diagnosticText) } };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return { .result = documents::failure(error.what()) };
    }
}

SstSmlPairOverviewSnapshot SstSmlDocumentSession::overview() const {
    SstSmlPairOverviewSnapshot out{};
    out.stem = impl_->pair.stem;
    out.smlPath = impl_->paths.sml;
    out.sstPath = impl_->paths.sst;
    out.smlSourceSize = impl_->smlSourceSize;
    out.sstSourceSize = impl_->sstSourceSize;
    out.smlDecodedSize = impl_->pair.sml.decodedSize;
    out.sstDecodedSize = impl_->pair.sst.decodedSize;
    out.smlWasAklz = impl_->pair.sml.sourceWasCompressedAklz;
    out.sstWasAklz = impl_->pair.sst.sourceWasCompressedAklz;
    out.smlEndian = impl_->pair.sml.sourceEndian == spice::root::Endian::Little ? "Little endian" : "Big endian";
    out.sstEndian = impl_->pair.sst.sourceEndian == spice::root::Endian::Little ? "Little endian" : "Big endian";
    out.platformContext = impl_->pair.sml.sourceEndian == spice::root::Endian::Little
        && impl_->pair.sst.sourceEndian == spice::root::Endian::Little ? "Dreamcast"
        : impl_->pair.sml.sourceEndian == spice::root::Endian::Big
            && impl_->pair.sst.sourceEndian == spice::root::Endian::Big ? "GameCube" : "Mixed / invalid";
    out.recordCount = impl_->pair.sml.recordCount;
    out.recordCountsAgree = impl_->pair.recordCountsAgree;
    for (const auto& mld : impl_->embeddedMlds) {
        if (!mld.has_value()) continue;
        if (mld->parseStatus == spice::mld::model::MldParseStatus::Failed) ++out.embeddedMldFailedCount;
        else ++out.embeddedMldParsedCount;
    }
    return out;
}

std::vector<std::filesystem::path> SstSmlDocumentSession::sourcePaths() const {
    return { impl_->paths.sml, impl_->paths.sst };
}

std::vector<SstSmlRecordSnapshot> SstSmlDocumentSession::records() const {
    std::vector<SstSmlRecordSnapshot> out{};
    out.reserve(impl_->pair.sml.records.size());
    for (std::size_t index = 0; index < impl_->pair.sml.records.size(); ++index) {
        const auto& sml = impl_->pair.sml.records[index];
        const auto top = index < impl_->pair.sst.topLevelRecords.size()
            ? &impl_->pair.sst.topLevelRecords[index] : nullptr;
        const auto block = std::find_if(impl_->pair.sst.commandBlocks.begin(), impl_->pair.sst.commandBlocks.end(),
            [index](const auto& candidate) { return candidate.topLevelRecordIndex == index; });
        const auto* mld = index < impl_->embeddedMlds.size() && impl_->embeddedMlds[index].has_value()
            ? &*impl_->embeddedMlds[index] : nullptr;
        out.push_back({
            .index = index,
            .smlRecordOffset = sml.recordOffset,
            .embeddedMldOffset = sml.embeddedMldOffset,
            .embeddedMldSize = sml.embeddedMldSize,
            .embeddedMldInBounds = sml.embeddedMldInBounds,
            .embeddedMldParsed = mld && mld->parseStatus != spice::mld::model::MldParseStatus::Failed,
            .embeddedMldParseStatus = mld ? mldStatusName(mld->parseStatus) : "Unavailable",
            .embeddedMldEntryCount = mld ? mld->entries.size() : 0U,
            .embeddedMldTextureCount = mld && mld->textureArchive.has_value() ? mld->textureArchive->entries.size() : 0U,
            .sstRecordOffset = top ? top->recordOffset : 0U,
            .commandBlockOffset = top ? top->commandBlockOffset : 0U,
            .commandCount = block != impl_->pair.sst.commandBlocks.end() ? block->commandCount : 0U,
            .commandBlockValid = block != impl_->pair.sst.commandBlocks.end() && block->valid,
        });
    }
    return out;
}

std::vector<SstSmlCommandSummarySnapshot> SstSmlDocumentSession::commands(const std::size_t recordIndex) const {
    std::vector<SstSmlCommandSummarySnapshot> out{};
    const auto block = std::find_if(impl_->pair.sst.commandBlocks.begin(), impl_->pair.sst.commandBlocks.end(),
        [recordIndex](const auto& candidate) { return candidate.topLevelRecordIndex == recordIndex; });
    if (block == impl_->pair.sst.commandBlocks.end()) return out;
    out.reserve(block->commands.size());
    for (const auto& command : block->commands) {
        SstSmlCommandSummarySnapshot snapshot{};
        snapshot.index = command.index;
        snapshot.type = command.type;
        if (const auto catalog = spice::sstsml::commandCatalogEntry(command.type)) {
            snapshot.typeLabel = catalog->label;
            snapshot.typeDescription = catalog->description;
        } else {
            snapshot.typeLabel = "Unknown command";
            snapshot.typeDescription = "The command type is not in the current Gekko-backed catalog.";
        }
        snapshot.argument = command.argument;
        snapshot.recordOffset = command.recordOffset;
        snapshot.payloadOffset = command.payloadOffset;
        snapshot.payloadSize = command.payloadSize;
        snapshot.typeKnown = command.typeKnown;
        snapshot.payloadInBounds = command.payloadInBounds;
        snapshot.localSlotIndex = command.modelIndex;
        const auto link = std::find_if(impl_->pair.localObjectSlotLinks.begin(), impl_->pair.localObjectSlotLinks.end(),
            [recordIndex, &command](const auto& candidate) {
                return candidate.topLevelRecordIndex == recordIndex && candidate.commandIndex == command.index;
            });
        if (link != impl_->pair.localObjectSlotLinks.end()) {
            snapshot.localSlotRangeKnown = link->slotIndexRangeKnown;
            snapshot.localSlotInRange = link->slotIndexInRange;
            snapshot.localSlotCount = link->localSlotCount;
        }
        out.push_back(std::move(snapshot));
    }
    return out;
}

std::optional<SstSmlCommandDetailSnapshot> SstSmlDocumentSession::commandDetail(
    const std::size_t recordIndex, const std::size_t commandIndex) const {
    const auto block = std::find_if(impl_->pair.sst.commandBlocks.begin(), impl_->pair.sst.commandBlocks.end(),
        [recordIndex](const auto& candidate) { return candidate.topLevelRecordIndex == recordIndex; });
    if (block == impl_->pair.sst.commandBlocks.end()) return std::nullopt;
    const auto command = std::find_if(block->commands.begin(), block->commands.end(),
        [commandIndex](const auto& candidate) { return candidate.index == commandIndex; });
    if (command == block->commands.end()) return std::nullopt;
    const auto summaries = commands(recordIndex);
    const auto summary = std::find_if(summaries.begin(), summaries.end(),
        [commandIndex](const auto& candidate) { return candidate.index == commandIndex; });
    if (summary == summaries.end()) return std::nullopt;

    SstSmlCommandDetailSnapshot out{};
    out.summary = *summary;
    out.rawWord4 = command->rawWord4;
    out.rawWord8 = command->rawWord8;
    out.onDiskWord12 = command->onDiskWord12;
    out.payloadHex = hexBytes(command->payloadBytes);
    for (const auto& field : command->fieldSummaries) {
        out.fields.push_back(projectField(field, command->payloadBytes, impl_->pair.sst.sourceEndian));
    }
    for (const auto& window : command->consumerWindows) {
        SstSmlConsumerWindowSnapshot projected{};
        projected.name = window.name;
        projected.offset = window.offset;
        projected.size = window.size;
        projected.inBounds = window.inBounds;
        projected.rawHex = hexBytes(window.bytes);
        projected.description = window.description;
        const std::uint32_t logicalBase = window.offset >= command->payloadOffset
            ? window.offset - command->payloadOffset : 0U;
        for (const auto& field : window.fieldSummaries) {
            projected.fields.push_back(projectField(field, window.bytes, impl_->pair.sst.sourceEndian, logicalBase));
        }
        out.consumerWindows.push_back(std::move(projected));
    }
    for (const auto& row : command->type1LightingRows) {
        out.lightingRows.push_back({
            .index = row.index,
            .rowOffset = row.rowOffset,
            .state = row.state,
            .sentinel = row.sentinel,
            .classSelector = row.classSelector,
            .flags = row.flags,
            .enablesLightSetup = row.enablesLightSetup,
            .enablesVectorSetup = row.enablesVectorSetup,
            .runtimeSlotId = row.runtimeSlotId,
            .lightVector = { row.lightVector.x, row.lightVector.y, row.lightVector.z },
            .slotRgb = { row.slotRgb.x, row.slotRgb.y, row.slotRgb.z },
            .globalRgb = { row.globalRgb.x, row.globalRgb.y, row.globalRgb.z },
            .attenuationOrSpot0 = row.attenuationOrSpot0,
            .attenuationOrSpot1 = row.attenuationOrSpot1,
            .rawTailWord = row.rawTailWord,
            .rawHex = hexBytes(row.rawBytes),
        });
    }
    return out;
}

std::vector<std::pair<std::int16_t, std::uint32_t>> SstSmlDocumentSession::commandTypeHistogram() const {
    return impl_->pair.commandTypeHistogram;
}

SstSmlRuntimeContextSnapshot SstSmlDocumentSession::runtimeContext() const {
    const auto& source = impl_->pair.activeRowRuntimeContext;
    SstSmlRuntimeContextSnapshot out{};
    out.provedRowStride = source.provedRowStride;
    out.allocationWidthPerRecord = source.allocationWidthPerRecord;
    out.allocationWidthNote = source.allocationWidthNote;
    for (const auto& field : source.fields) {
        out.fields.push_back({ field.offset, field.size, field.name, field.description });
    }
    return out;
}

std::vector<SstSmlLocalSlotLinkSnapshot> SstSmlDocumentSession::localSlotLinks() const {
    std::vector<SstSmlLocalSlotLinkSnapshot> out{};
    out.reserve(impl_->pair.localObjectSlotLinks.size());
    for (const auto& link : impl_->pair.localObjectSlotLinks) {
        out.push_back({
            .recordIndex = link.topLevelRecordIndex,
            .commandIndex = link.commandIndex,
            .commandType = link.commandType,
            .localSlotIndex = link.localSlotIndex,
            .rangeKnown = link.slotIndexRangeKnown,
            .inRange = link.slotIndexInRange,
            .localSlotCount = link.localSlotCount,
        });
    }
    return out;
}

std::optional<SstSmlBattleGridSnapshot> SstSmlDocumentSession::battleGrid() const {
    const auto block = std::find_if(impl_->pair.sst.commandBlocks.begin(), impl_->pair.sst.commandBlocks.end(),
        [](const auto& candidate) { return candidate.topLevelRecordIndex == 0U; });
    if (block == impl_->pair.sst.commandBlocks.end() || !block->battleGridTerrainSource.has_value()) {
        return std::nullopt;
    }
    const auto& grid = *block->battleGridTerrainSource;
    return SstSmlBattleGridSnapshot{
        .sourceOffset = grid.sourceOffset,
        .sourceSize = grid.sourceSize,
        .inBounds = grid.inBounds,
        .values = grid.source9x9,
        .paddingHex = hexBytes(grid.paddingAfterSource),
    };
}

std::vector<SstSmlDiagnosticSnapshot> SstSmlDocumentSession::diagnostics() const {
    return impl_->diagnostics;
}

std::optional<MldOverviewSnapshot> SstSmlDocumentSession::embeddedMldOverview(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex].has_value()) return std::nullopt;
    return documents::projectMldOverview(*impl_->embeddedMlds[recordIndex]);
}

std::vector<MldEntrySnapshot> SstSmlDocumentSession::embeddedMldEntries(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex].has_value()) return {};
    return documents::projectMldEntries(*impl_->embeddedMlds[recordIndex]);
}

std::vector<MldEntryDetailSnapshot> SstSmlDocumentSession::embeddedMldEntryDetails(
    const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex].has_value()) return {};
    return documents::projectMldEntryDetails(*impl_->embeddedMlds[recordIndex]);
}

std::vector<MldTextureSnapshot> SstSmlDocumentSession::embeddedMldTextures(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex].has_value()) return {};
    return documents::projectMldTextures(*impl_->embeddedMlds[recordIndex]);
}

std::vector<DocumentDiagnostic> SstSmlDocumentSession::embeddedMldDiagnostics(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex].has_value()) return {};
    return documents::projectMldDiagnostics(*impl_->embeddedMlds[recordIndex]);
}

std::optional<RgbaImageSnapshot> SstSmlDocumentSession::embeddedMldTexturePreview(
    const std::size_t recordIndex, const std::size_t textureIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex].has_value()) return std::nullopt;
    return documents::projectMldTexturePreview(*impl_->embeddedMlds[recordIndex], textureIndex);
}

} // namespace spice::mix
