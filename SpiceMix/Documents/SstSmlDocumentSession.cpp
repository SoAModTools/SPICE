#include "SstSmlDocumentSession.h"

#include "DocumentSupport.h"
#include "MldInspectionSupport.h"
#include "../../SpiceMLD/Parsing/MldParser.h"
#include "../../SpiceSstSml/SstCommandCatalog.h"
#include "../../SpiceSstSml/SstSmlDocumentAnalysis.h"
#include "../../SpiceSstSml/SstSmlDocumentImporter.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace spice::mix {
namespace {

EventLevel eventLevel(const spice::sstsml::SstSmlDiagnosticSeverity severity) {
    switch (severity) {
    case spice::sstsml::SstSmlDiagnosticSeverity::Warning: return EventLevel::Warning;
    case spice::sstsml::SstSmlDiagnosticSeverity::Error: return EventLevel::Error;
    default: return EventLevel::Info;
    }
}

std::string hexBytes(std::span<const std::uint8_t> bytes) {
    std::ostringstream out{};
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return out.str();
}

std::size_t widthBytes(const spice::sstsml::SstSmlFieldWidth width) {
    using enum spice::sstsml::SstSmlFieldWidth;
    switch (width) {
    case I8:
    case U8: return 1U;
    case I16:
    case U16: return 2U;
    case U32:
    case F32: return 4U;
    }
    return 0U;
}

std::string kindName(const spice::sstsml::SstSmlFieldKind kind) {
    using enum spice::sstsml::SstSmlFieldKind;
    switch (kind) {
    case ModelIndex: return "Model index";
    case RuntimeSlot: return "Runtime slot";
    case LookupKey: return "Lookup key";
    case RawWord: return "Raw word";
    case HalfwordParameter: return "Halfword parameter";
    case FloatParameter: return "Float parameter";
    case RuntimePointer: return "Runtime pointer";
    case VectorComponent: return "Vector component";
    case RotationComponent: return "Rotation component";
    case VectorDelta: return "Vector delta";
    case Duration: return "Duration";
    case Counter: return "Counter";
    case AxisSelector: return "Axis selector";
    case BufferPointer: return "Buffer pointer";
    case ReservedRaw: return "Reserved/raw";
    }
    return "Unknown";
}

SstSmlFieldEvidence fieldEvidence(const spice::sstsml::SstSmlFieldEvidence evidence) {
    using Source = spice::sstsml::SstSmlFieldEvidence;
    switch (evidence) {
    case Source::Gekko: return SstSmlFieldEvidence::Gekko;
    case Source::GekkoAndCorpus: return SstSmlFieldEvidence::GekkoAndCorpus;
    case Source::CorpusStable: return SstSmlFieldEvidence::CorpusStable;
    case Source::CodeSupportedCorpusAbsent: return SstSmlFieldEvidence::CodeSupportedCorpusAbsent;
    case Source::Provisional: return SstSmlFieldEvidence::Provisional;
    }
    return SstSmlFieldEvidence::Provisional;
}

std::string scopeName(const spice::sstsml::SstSmlFieldScope scope) {
    using enum spice::sstsml::SstSmlFieldScope;
    switch (scope) {
    case StructuralPayload: return "Structural payload";
    case ConsumerTrailing: return "Consumer trailing";
    case RuntimeLocal: return "Runtime local";
    }
    return "Unknown";
}

std::string fieldValue(const spice::sstsml::SstCommandFieldValue& value) {
    std::ostringstream out{};
    out << std::setprecision(9);
    std::visit([&](const auto typed) {
        using Value = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Value, std::int8_t>) out << static_cast<int>(typed);
        else if constexpr (std::is_same_v<Value, std::uint8_t>) out << static_cast<unsigned>(typed);
        else if constexpr (std::is_same_v<Value, std::uint32_t>) {
            out << typed << " (0x" << std::uppercase << std::hex << typed << ')';
        } else out << typed;
    }, value);
    return out.str();
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

std::optional<std::int16_t> localSlotIndex(const spice::sstsml::SstStageCommand& command) {
    const auto found = std::find_if(command.fields.begin(), command.fields.end(), [](const auto& field) {
        return field.name == "modelIndex";
    });
    if (found == command.fields.end()) return std::nullopt;
    if (const auto value = std::get_if<std::int16_t>(&found->value)) return *value;
    return std::nullopt;
}

} // namespace

struct SstSmlDocumentSession::Impl {
    spice::sstsml::SstSmlDocument document{};
    spice::sstsml::SstSmlDocumentImportReceipt receipt{};
    spice::sstsml::SstSmlDocumentAnalysis analysis{};
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
        documents::emit(context, EventLevel::Progress,
            "Opening paired SST/SML stage from " + eitherPairPath.string());
        auto imported = spice::sstsml::SstSmlDocumentImporter::importFile(eitherPairPath);
        std::vector<std::string> diagnosticText{};
        for (const auto& diagnostic : imported.diagnostics) {
            diagnosticText.push_back(std::string(spice::sstsml::toString(diagnostic.source)) + ": " + diagnostic.message);
        }
        if (!imported.ok()) {
            documents::emit(context, EventLevel::Error, "SST/SML pair import failed.");
            const auto message = diagnosticText.empty()
                ? std::string("SST/SML pair import failed.")
                : std::string("SST/SML pair import failed: ") + diagnosticText.front();
            return { .result = documents::failure(message, std::move(diagnosticText)) };
        }
        if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };

        auto impl = std::make_unique<Impl>();
        impl->document = std::move(*imported.document);
        impl->receipt = std::move(imported.receipt);
        impl->analysis = spice::sstsml::SstSmlDocumentAnalyzer::analyze(impl->document, impl->receipt);
        for (const auto& diagnostic : imported.diagnostics) {
            impl->diagnostics.push_back({
                .level = eventLevel(diagnostic.severity),
                .origin = spice::sstsml::toString(diagnostic.source),
                .message = diagnostic.message,
                .sourceOffset = diagnostic.decodedOffset.has_value()
                    ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(*diagnostic.decodedOffset))
                    : std::nullopt,
            });
        }
        for (const auto& diagnostic : impl->analysis.diagnostics) {
            impl->diagnostics.push_back({
                .level = eventLevel(diagnostic.severity),
                .origin = "Analysis",
                .message = diagnostic.message,
            });
            diagnosticText.push_back("Analysis: " + diagnostic.message);
        }
        if (!impl->analysis.ok()) {
            return { .result = documents::failure("SST/SML analysis failed.", std::move(diagnosticText)) };
        }

        impl->embeddedMlds.resize(impl->document.members.size());
        for (std::size_t index = 0U; index < impl->document.members.size(); ++index) {
            if (context.stopToken.stop_requested()) return { .result = documents::cancelled() };
            const auto& bytes = impl->document.members[index].sml.resource.bytes;
            if (bytes.empty()) continue;
            documents::emit(context, EventLevel::Progress,
                "Inspecting embedded MLD " + std::to_string(index + 1U) + "/" +
                    std::to_string(impl->document.members.size()) + ".");
            auto parsed = spice::mld::parsing::MldParser{}.parseBytes(bytes);
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
            impl->embeddedMlds[index] = std::move(parsed);
        }

        auto session = std::shared_ptr<SstSmlDocumentSession>(new SstSmlDocumentSession(std::move(impl)));
        documents::emit(context, EventLevel::Info, "Opened SST/SML battle-stage document.");
        return { .session = std::move(session),
            .result = { .message = "SST/SML document is ready.", .diagnostics = std::move(diagnosticText) } };
    } catch (const std::exception& error) {
        documents::emit(context, EventLevel::Error, error.what());
        return { .result = documents::failure(error.what()) };
    }
}

SstSmlPairOverviewSnapshot SstSmlDocumentSession::overview() const {
    SstSmlPairOverviewSnapshot out{};
    out.smlPath = impl_->receipt.sml.path.value_or(std::filesystem::path{});
    out.sstPath = impl_->receipt.sst.path.value_or(std::filesystem::path{});
    out.stem = out.smlPath.stem().string();
    out.smlSourceSize = impl_->receipt.sml.rawSize;
    out.sstSourceSize = impl_->receipt.sst.rawSize;
    out.smlDecodedSize = static_cast<std::uint32_t>(impl_->receipt.sml.decodedSize.value_or(0U));
    out.sstDecodedSize = static_cast<std::uint32_t>(impl_->receipt.sst.decodedSize.value_or(0U));
    out.smlWasAklz = impl_->receipt.sml.wrapper == spice::sstsml::SstSmlSourceWrapper::Aklz;
    out.sstWasAklz = impl_->receipt.sst.wrapper == spice::sstsml::SstSmlSourceWrapper::Aklz;
    const auto endianName = [](const auto endian) {
        return endian == spice::root::Endian::Little ? "Little endian" : "Big endian";
    };
    if (impl_->receipt.sml.endian) out.smlEndian = endianName(*impl_->receipt.sml.endian);
    if (impl_->receipt.sst.endian) out.sstEndian = endianName(*impl_->receipt.sst.endian);
    out.platformContext = impl_->receipt.sml.endian == spice::root::Endian::Little &&
            impl_->receipt.sst.endian == spice::root::Endian::Little ? "Little-endian convention" :
        impl_->receipt.sml.endian == spice::root::Endian::Big &&
            impl_->receipt.sst.endian == spice::root::Endian::Big ? "Big-endian convention" : "Mixed / invalid";
    out.recordCount = static_cast<std::uint32_t>(impl_->document.members.size());
    out.recordCountsAgree = true;
    for (const auto& mld : impl_->embeddedMlds) {
        if (!mld) continue;
        if (mld->parseStatus == spice::mld::model::MldParseStatus::Failed) ++out.embeddedMldFailedCount;
        else ++out.embeddedMldParsedCount;
    }
    return out;
}

std::vector<std::filesystem::path> SstSmlDocumentSession::sourcePaths() const {
    return { impl_->receipt.sml.path.value_or(std::filesystem::path{}),
        impl_->receipt.sst.path.value_or(std::filesystem::path{}) };
}

std::vector<SstSmlRecordSnapshot> SstSmlDocumentSession::records() const {
    std::vector<SstSmlRecordSnapshot> out{};
    out.reserve(impl_->document.members.size());
    for (std::size_t index = 0U; index < impl_->document.members.size(); ++index) {
        const auto& member = impl_->document.members[index];
        const auto* mld = index < impl_->embeddedMlds.size() && impl_->embeddedMlds[index]
            ? &*impl_->embeddedMlds[index] : nullptr;
        out.push_back({
            .index = index,
            .memberId = member.id.value,
            .embeddedMldSize = static_cast<std::uint32_t>(member.sml.resource.bytes.size()),
            .embeddedMldParsed = mld && mld->parseStatus != spice::mld::model::MldParseStatus::Failed,
            .embeddedMldParseStatus = mld ? mldStatusName(mld->parseStatus) : "Unavailable",
            .embeddedMldEntryCount = mld ? mld->entries.size() : 0U,
            .embeddedMldTextureCount = mld && mld->textureArchive ? mld->textureArchive->entries.size() : 0U,
            .commandCount = static_cast<std::uint32_t>(member.sst.commandBlock.commands.size()),
            .commandBlockValid = member.sst.commandBlock.sentinelType < 0,
        });
    }
    return out;
}

std::vector<SstSmlCommandSummarySnapshot> SstSmlDocumentSession::commands(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->document.members.size()) return {};
    std::vector<SstSmlCommandSummarySnapshot> out{};
    const auto& commands = impl_->document.members[recordIndex].sst.commandBlock.commands;
    out.reserve(commands.size());
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const auto& command = commands[index];
        SstSmlCommandSummarySnapshot snapshot{};
        snapshot.index = index;
        snapshot.commandId = command.id.value;
        snapshot.type = command.type;
        if (const auto catalog = spice::sstsml::commandCatalogEntry(command.type)) {
            snapshot.typeLabel = catalog->label;
            snapshot.typeDescription = catalog->description;
        } else {
            snapshot.typeLabel = "Unknown command";
            snapshot.typeDescription = "The command type is not in the current Gekko-backed catalog.";
        }
        snapshot.argument = command.argument;
        snapshot.payloadSize = static_cast<std::uint32_t>(command.payloadBytes.size());
        snapshot.typeKnown = command.payloadSpanKnown;
        snapshot.localSlotIndex = localSlotIndex(command);
        const auto link = std::find_if(impl_->analysis.localObjectSlotLinks.begin(),
            impl_->analysis.localObjectSlotLinks.end(), [&](const auto& candidate) {
                return candidate.commandId == command.id;
            });
        if (link != impl_->analysis.localObjectSlotLinks.end()) {
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
    if (recordIndex >= impl_->document.members.size()) return std::nullopt;
    const auto& commands = impl_->document.members[recordIndex].sst.commandBlock.commands;
    if (commandIndex >= commands.size()) return std::nullopt;
    const auto summaries = this->commands(recordIndex);
    const auto& command = commands[commandIndex];
    SstSmlCommandDetailSnapshot out{};
    out.summary = summaries[commandIndex];
    out.rawWord4 = command.rawWord4;
    out.rawWord8 = command.rawWord8;
    out.onDiskWord12 = command.onDiskWord12;
    out.payloadHex = hexBytes(command.payloadBytes);
    const auto analysis = std::find_if(impl_->analysis.commands.begin(), impl_->analysis.commands.end(),
        [&](const auto& candidate) { return candidate.commandId == command.id; });
    if (analysis != impl_->analysis.commands.end()) {
        for (const auto& fieldAnalysis : analysis->fields) {
            const auto field = std::find_if(command.fields.begin(), command.fields.end(),
                [&](const auto& candidate) { return candidate.id == fieldAnalysis.fieldId; });
            if (field == command.fields.end()) continue;
            SstSmlCommandFieldSnapshot projected{};
            projected.offset = fieldAnalysis.payloadOffset;
            projected.width = spice::sstsml::toString(fieldAnalysis.width);
            projected.kind = kindName(fieldAnalysis.kind);
            projected.name = field->name;
            projected.evidence = fieldEvidence(fieldAnalysis.evidence);
            projected.scope = scopeName(fieldAnalysis.scope);
            projected.provisional = fieldAnalysis.provisional;
            projected.valueAvailable = true;
            projected.value = fieldValue(field->value);
            projected.description = fieldAnalysis.description;
            const auto size = widthBytes(fieldAnalysis.width);
            if (fieldAnalysis.payloadOffset <= command.payloadBytes.size() &&
                size <= command.payloadBytes.size() - fieldAnalysis.payloadOffset) {
                projected.rawHex = hexBytes(std::span(command.payloadBytes).subspan(fieldAnalysis.payloadOffset, size));
            }
            out.fields.push_back(std::move(projected));
        }
        for (const auto& window : analysis->consumerWindows) {
            out.consumerWindows.push_back({
                .name = window.name,
                .available = window.available,
                .rawHex = hexBytes(window.bytes),
                .description = window.description,
            });
        }
    }
    for (std::size_t index = 0U; index < command.lightingRows.size(); ++index) {
        const auto& row = command.lightingRows[index];
        constexpr std::size_t rowStride = 0x68U;
        const auto rowOffset = index * rowStride;
        out.lightingRows.push_back({
            .index = index,
            .rowOffset = static_cast<std::uint32_t>(rowOffset),
            .state = row.state,
            .sentinel = row.sentinel,
            .classSelector = row.classSelector,
            .flags = row.flags,
            .enablesLightSetup = (row.flags & 0x40000000U) != 0U,
            .enablesVectorSetup = (row.flags & 0x20000000U) != 0U,
            .runtimeSlotId = row.runtimeSlotId,
            .lightVector = row.lightVector,
            .slotRgb = row.slotRgb,
            .globalRgb = row.globalRgb,
            .attenuationOrSpot0 = row.attenuationOrSpot0,
            .attenuationOrSpot1 = row.attenuationOrSpot1,
            .rawTailWord = row.rawTailWord,
            .rawHex = rowOffset <= command.payloadBytes.size() && rowStride <= command.payloadBytes.size() - rowOffset
                ? hexBytes(std::span(command.payloadBytes).subspan(rowOffset, rowStride)) : std::string{},
        });
    }
    return out;
}

std::vector<std::pair<std::int16_t, std::uint32_t>> SstSmlDocumentSession::commandTypeHistogram() const {
    return impl_->analysis.commandTypeHistogram;
}

SstSmlRuntimeContextSnapshot SstSmlDocumentSession::runtimeContext() const {
    SstSmlRuntimeContextSnapshot out{};
    out.provedRowStride = impl_->analysis.activeRows.provedRowStride;
    out.allocationWidthPerRecord = impl_->analysis.activeRows.allocationWidthPerRecord;
    out.allocationWidthNote = impl_->analysis.activeRows.allocationWidthNote;
    for (const auto& field : impl_->analysis.activeRows.fields) {
        out.fields.push_back({ field.offset, field.size, field.name, field.description });
    }
    return out;
}

std::vector<SstSmlLocalSlotLinkSnapshot> SstSmlDocumentSession::localSlotLinks() const {
    std::vector<SstSmlLocalSlotLinkSnapshot> out{};
    for (const auto& link : impl_->analysis.localObjectSlotLinks) {
        const auto member = std::find_if(impl_->document.members.begin(), impl_->document.members.end(),
            [&](const auto& candidate) { return candidate.id == link.memberId; });
        if (member == impl_->document.members.end()) continue;
        const auto command = std::find_if(member->sst.commandBlock.commands.begin(), member->sst.commandBlock.commands.end(),
            [&](const auto& candidate) { return candidate.id == link.commandId; });
        if (command == member->sst.commandBlock.commands.end()) continue;
        out.push_back({
            .recordIndex = static_cast<std::size_t>(member - impl_->document.members.begin()),
            .commandIndex = static_cast<std::size_t>(command - member->sst.commandBlock.commands.begin()),
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
    if (impl_->document.members.empty()) return std::nullopt;
    const auto& block = impl_->document.members.front().sst.commandBlock;
    if (!block.battleGrid) return std::nullopt;
    return SstSmlBattleGridSnapshot{
        .terrainId = block.battleGrid->id.value,
        .values = block.battleGrid->values,
        .paddingHex = block.trailingOpaque ? hexBytes(block.trailingOpaque->bytes) : std::string{},
    };
}

std::vector<SstSmlDiagnosticSnapshot> SstSmlDocumentSession::diagnostics() const { return impl_->diagnostics; }

std::optional<MldOverviewSnapshot> SstSmlDocumentSession::embeddedMldOverview(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex]) return std::nullopt;
    return documents::projectMldOverview(*impl_->embeddedMlds[recordIndex]);
}

std::vector<MldEntrySnapshot> SstSmlDocumentSession::embeddedMldEntries(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex]) return {};
    return documents::projectMldEntries(*impl_->embeddedMlds[recordIndex]);
}

std::vector<MldEntryDetailSnapshot> SstSmlDocumentSession::embeddedMldEntryDetails(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex]) return {};
    return documents::projectMldEntryDetails(*impl_->embeddedMlds[recordIndex]);
}

std::vector<MldTextureSnapshot> SstSmlDocumentSession::embeddedMldTextures(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex]) return {};
    return documents::projectMldTextures(*impl_->embeddedMlds[recordIndex]);
}

std::vector<DocumentDiagnostic> SstSmlDocumentSession::embeddedMldDiagnostics(const std::size_t recordIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex]) return {};
    return documents::projectMldDiagnostics(*impl_->embeddedMlds[recordIndex]);
}

std::optional<RgbaImageSnapshot> SstSmlDocumentSession::embeddedMldTexturePreview(
    const std::size_t recordIndex, const std::size_t textureIndex) const {
    if (recordIndex >= impl_->embeddedMlds.size() || !impl_->embeddedMlds[recordIndex]) return std::nullopt;
    return documents::projectMldTexturePreview(*impl_->embeddedMlds[recordIndex], textureIndex);
}

} // namespace spice::mix
