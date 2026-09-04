#include "SstSmlDocumentAnalysis.h"

#include "SstParser.h"
#include "../SpiceRoot/Binary/EndianReader.h"

#include <algorithm>
#include <array>
#include <map>
#include <string_view>

namespace spice::sstsml {
namespace {

using spice::root::Endian;
using spice::root::EndianReader;
using detail::CommandFieldEvidence;
using detail::CommandFieldKind;
using detail::CommandFieldScope;
using detail::CommandFieldWidth;
using detail::SstParser;

void addDiagnostic(SstSmlDocumentAnalysis& result,
    SstSmlDiagnosticSeverity severity,
    SstSmlSourceMember source,
    std::string message) {
    result.diagnostics.push_back(SstSmlDocumentDiagnostic{
        severity, source, std::move(message), std::nullopt });
}

bool containsTag(std::span<const std::uint8_t> bytes, std::string_view tag) {
    if (tag.size() != 4U || bytes.size() < 4U) return false;
    const std::array<std::uint8_t, 4U> needle{
        static_cast<std::uint8_t>(tag[0]), static_cast<std::uint8_t>(tag[1]),
        static_cast<std::uint8_t>(tag[2]), static_cast<std::uint8_t>(tag[3]) };
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

SmlEmbeddedResourceInspection inspectResource(const SmlEmbeddedResource& resource, Endian endian) {
    SmlEmbeddedResourceInspection inspection{};
    inspection.resourceId = resource.id;
    inspection.hasNjcm = containsTag(resource.bytes, "NJCM");
    inspection.hasNjtl = containsTag(resource.bytes, "NJTL");
    inspection.hasNmdm = containsTag(resource.bytes, "NMDM");
    inspection.hasGcix = containsTag(resource.bytes, "GCIX");
    inspection.hasGvrt = containsTag(resource.bytes, "GVRT");
    inspection.hasGbix = containsTag(resource.bytes, "GBIX");
    inspection.hasPvrt = containsTag(resource.bytes, "PVRT");
    inspection.hasPvmh = containsTag(resource.bytes, "PVMH");
    const EndianReader reader(resource.bytes, endian);
    inspection.entryCount = reader.try_read_u32(0U);
    inspection.indexTableOffset = reader.try_read_u32(4U);
    inspection.textureTableOffset = reader.try_read_u32(0x10U);
    if (inspection.textureTableOffset.has_value()) {
        inspection.textureArchiveCount = reader.try_read_u32(*inspection.textureTableOffset);
    }
    if (inspection.entryCount.has_value() && inspection.indexTableOffset.has_value()) {
        constexpr std::uint64_t kIndexStride = 0x68U;
        const auto end = static_cast<std::uint64_t>(*inspection.indexTableOffset) +
            static_cast<std::uint64_t>(*inspection.entryCount) * kIndexStride;
        inspection.validLookingHeader = *inspection.entryCount > 0U &&
            *inspection.indexTableOffset >= 0x14U && end <= resource.bytes.size();
    }
    return inspection;
}

SstSmlFieldKind convertKind(CommandFieldKind kind) {
    return static_cast<SstSmlFieldKind>(kind);
}

SstSmlFieldWidth convertWidth(CommandFieldWidth width) {
    return static_cast<SstSmlFieldWidth>(width);
}

SstSmlFieldEvidence convertEvidence(CommandFieldEvidence evidence) {
    return static_cast<SstSmlFieldEvidence>(evidence);
}

SstSmlFieldScope convertScope(CommandFieldScope scope) {
    return static_cast<SstSmlFieldScope>(scope);
}

std::optional<std::int16_t> modelIndex(const SstStageCommand& command) {
    const auto it = std::find_if(command.fields.begin(), command.fields.end(), [](const auto& field) {
        return field.name == "modelIndex";
    });
    if (it == command.fields.end()) return std::nullopt;
    if (const auto value = std::get_if<std::int16_t>(&it->value)) return *value;
    return std::nullopt;
}

SstSmlActiveRowAnalysis makeActiveRows() {
    SstSmlActiveRowAnalysis result{};
    result.allocationWidthNote =
        "Battle::Stage::JoinSmlSstRecords_8000cb44 allocates recordCount * 0x2c, "
        "but current direct Gekko evidence addresses active rows with recordIndex * 0x14.";
    result.fields = {
        { 0x00U, 4U, "localModelObjectSlotTable", "Runtime pointer written by SST command type 0; not on-disk SML/SST data." },
        { 0x04U, 4U, "loadedMldResourceRecord", "Runtime pointer to the same-index loaded MLD resource-list record." },
        { 0x08U, 4U, "localRuntimePointerTable", "Runtime pointer table appended after the type 0 local slot table." },
        { 0x0CU, 1U, "localSlotCount", "Runtime local slot count copied from the loaded MLD header entry count." },
        { 0x10U, 4U, "secondaryModelEffectRuntimeBuffer", "Runtime-only buffer pointer written by type 0 setup and read by type 11." },
    };
    return result;
}

} // namespace

bool SstSmlDocumentAnalysis::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == SstSmlDiagnosticSeverity::Error;
    });
}

SstSmlDocumentAnalysis SstSmlDocumentAnalyzer::analyze(
    const SstSmlDocument& document,
    const SstSmlDocumentImportReceipt& receipt) {
    SstSmlDocumentAnalysis result{};
    result.activeRows = makeActiveRows();
    if (!receipt.sml.endian.has_value() || !receipt.sst.endian.has_value()) {
        addDiagnostic(result, SstSmlDiagnosticSeverity::Error, SstSmlSourceMember::Pair,
            "SST/SML analysis requires detected byte order in both receipt members");
        return result;
    }

    std::map<std::int16_t, std::uint32_t> histogram;
    for (const auto& member : document.members) {
        const auto inspection = inspectResource(member.sml.resource, *receipt.sml.endian);
        const auto localSlotCount = inspection.validLookingHeader ? inspection.entryCount : std::nullopt;
        result.embeddedResources.push_back(inspection);
        for (const auto& command : member.sst.commandBlock.commands) {
            ++histogram[command.type];
            SstSmlCommandAnalysis commandAnalysis{};
            commandAnalysis.commandId = command.id;
            const auto catalog = SstParser::fieldSummariesForType(command.type);
            const auto count = std::min(catalog.size(), command.fields.size());
            commandAnalysis.fields.reserve(count);
            for (std::size_t index = 0U; index < count; ++index) {
                const auto& source = catalog[index];
                commandAnalysis.fields.push_back(SstSmlCommandFieldAnalysis{
                    command.fields[index].id,
                    source.offset,
                    convertWidth(source.width),
                    convertKind(source.kind),
                    convertEvidence(source.evidence),
                    convertScope(source.scope),
                    source.provisional,
                    source.description,
                });
            }
            if (command.type == 11) {
                commandAnalysis.consumerWindows.push_back(SstSmlCommandConsumerWindowAnalysis{
                    "type11TrailingConsumerFields",
                    false,
                    {},
                    "The runtime walker may consume fields following the structural type 11 payload; no source map is retained in the first document contract.",
                });
            }
            result.commands.push_back(std::move(commandAnalysis));

            const auto index = modelIndex(command);
            if (!index.has_value()) continue;
            SstSmlLocalObjectSlotLink link{};
            link.memberId = member.id;
            link.commandId = command.id;
            link.commandType = command.type;
            link.localSlotIndex = *index;
            link.localSlotCount = localSlotCount;
            link.slotIndexRangeKnown = localSlotCount.has_value();
            link.slotIndexInRange = localSlotCount.has_value() && *index >= 0 &&
                static_cast<std::uint32_t>(*index) < *localSlotCount;
            link.owningResourceId = member.sml.resource.id;
            if (link.slotIndexRangeKnown && !link.slotIndexInRange) {
                addDiagnostic(result, SstSmlDiagnosticSeverity::Warning, SstSmlSourceMember::Sst,
                    "SST command model index is outside its same-member embedded-resource slot range");
            }
            result.localObjectSlotLinks.push_back(link);
        }
    }
    result.commandTypeHistogram.assign(histogram.begin(), histogram.end());
    return result;
}

const char* toString(SstSmlFieldKind kind) noexcept {
    static constexpr const char* names[]{ "modelIndex", "runtimeSlot", "lookupKey", "rawWord",
        "halfwordParameter", "floatParameter", "runtimePointer", "vectorComponent", "rotationComponent",
        "vectorDelta", "duration", "counter", "axisSelector", "bufferPointer", "reservedRaw" };
    const auto index = static_cast<std::size_t>(kind);
    return index < std::size(names) ? names[index] : "unknown";
}

const char* toString(SstSmlFieldWidth width) noexcept {
    static constexpr const char* names[]{ "i8", "u8", "i16", "u16", "u32", "f32" };
    const auto index = static_cast<std::size_t>(width);
    return index < std::size(names) ? names[index] : "unknown";
}

const char* toString(SstSmlFieldEvidence evidence) noexcept {
    static constexpr const char* names[]{ "gekko", "gekkoAndCorpus", "corpusStable", "codeSupportedCorpusAbsent", "provisional" };
    const auto index = static_cast<std::size_t>(evidence);
    return index < std::size(names) ? names[index] : "unknown";
}

const char* toString(SstSmlFieldScope scope) noexcept {
    static constexpr const char* names[]{ "structuralPayload", "consumerTrailing", "runtimeLocal" };
    const auto index = static_cast<std::size_t>(scope);
    return index < std::size(names) ? names[index] : "unknown";
}

} // namespace spice::sstsml
