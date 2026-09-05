#pragma once

#include "SstSmlDocument.h"
#include "SstSmlDocumentImporter.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace spice::sstsml {

enum class SstSmlFieldKind {
    ModelIndex,
    RuntimeSlot,
    LookupKey,
    RawWord,
    HalfwordParameter,
    FloatParameter,
    RuntimePointer,
    VectorComponent,
    RotationComponent,
    VectorDelta,
    Duration,
    Counter,
    AxisSelector,
    BufferPointer,
    ReservedRaw,
};

enum class SstSmlFieldWidth { I8, U8, I16, U16, U32, F32 };
enum class SstSmlFieldEvidence { Gekko, GekkoAndCorpus, CorpusStable, CodeSupportedCorpusAbsent, Provisional };
enum class SstSmlFieldScope { StructuralPayload, ConsumerTrailing, RuntimeLocal };

struct SstSmlCommandFieldAnalysis {
    SstCommandFieldId fieldId{};
    std::uint32_t payloadOffset{ 0U };
    SstSmlFieldWidth width{ SstSmlFieldWidth::U32 };
    SstSmlFieldKind kind{ SstSmlFieldKind::ReservedRaw };
    SstSmlFieldEvidence evidence{ SstSmlFieldEvidence::Provisional };
    SstSmlFieldScope scope{ SstSmlFieldScope::StructuralPayload };
    bool provisional{ true };
    std::string description{};
};

struct SstSmlCommandConsumerWindowAnalysis {
    std::string name{};
    bool available{ false };
    std::vector<std::uint8_t> bytes{};
    std::string description{};
};

struct SstSmlCommandAnalysis {
    SstCommandId commandId{};
    std::vector<SstSmlCommandFieldAnalysis> fields{};
    std::vector<SstSmlCommandConsumerWindowAnalysis> consumerWindows{};
};

struct SmlEmbeddedResourceInspection {
    SmlEmbeddedResourceId resourceId{};
    bool decoded{ false };
    bool validLookingHeader{ false };
    std::optional<std::uint32_t> entryCount{};
    std::optional<std::uint32_t> indexTableOffset{};
    std::optional<std::uint32_t> textureTableOffset{};
    std::optional<std::uint32_t> textureArchiveCount{};
    bool hasNjcm{ false };
    bool hasNjtl{ false };
    bool hasNmdm{ false };
    bool hasGcix{ false };
    bool hasGvrt{ false };
    bool hasGbix{ false };
    bool hasPvrt{ false };
    bool hasPvmh{ false };
};

struct SstSmlActiveRowFieldAnalysis {
    std::uint32_t offset{ 0U };
    std::uint32_t size{ 0U };
    std::string name{};
    std::string description{};
};

struct SstSmlActiveRowAnalysis {
    std::uint32_t provedRowStride{ 0x14U };
    std::uint32_t allocationWidthPerRecord{ 0x2CU };
    std::string allocationWidthNote{};
    std::vector<SstSmlActiveRowFieldAnalysis> fields{};
};

struct SstSmlLocalObjectSlotLink {
    SstSmlStageMemberId memberId{};
    SstCommandId commandId{};
    std::int16_t commandType{ 0 };
    std::int16_t localSlotIndex{ 0 };
    bool slotIndexRangeKnown{ false };
    bool slotIndexInRange{ false };
    std::optional<std::uint32_t> localSlotCount{};
    SmlEmbeddedResourceId owningResourceId{};
    std::optional<spice::mld::MldEntryId> resolvedEntryId{};
};

struct SstSmlDocumentAnalysis {
    SstSmlActiveRowAnalysis activeRows{};
    std::vector<SmlEmbeddedResourceInspection> embeddedResources{};
    std::vector<SstSmlCommandAnalysis> commands{};
    std::vector<SstSmlLocalObjectSlotLink> localObjectSlotLinks{};
    std::vector<std::pair<std::int16_t, std::uint32_t>> commandTypeHistogram{};
    std::vector<SstSmlDocumentDiagnostic> diagnostics{};
    [[nodiscard]] bool ok() const;
};

class SstSmlDocumentAnalyzer {
public:
    [[nodiscard]] static SstSmlDocumentAnalysis analyze(
        const SstSmlDocument& document,
        const SstSmlDocumentImportReceipt& receipt);
};

[[nodiscard]] const char* toString(SstSmlFieldKind kind) noexcept;
[[nodiscard]] const char* toString(SstSmlFieldWidth width) noexcept;
[[nodiscard]] const char* toString(SstSmlFieldEvidence evidence) noexcept;
[[nodiscard]] const char* toString(SstSmlFieldScope scope) noexcept;

} // namespace spice::sstsml
