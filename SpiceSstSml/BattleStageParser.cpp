#include "BattleStageParser.h"

#include "SmlParser.h"
#include "SstParser.h"

#include <algorithm>
#include <map>

namespace spice::sstsml {
namespace {

void addDiagnostic(std::vector<ParseDiagnostic>& diagnostics,
    DiagnosticSeverity severity,
    std::string message,
    std::uint32_t offset = 0U) {
    diagnostics.push_back(ParseDiagnostic{ severity, std::move(message), offset });
}

ActiveRowRuntimeContext makeActiveRowRuntimeContext() {
    ActiveRowRuntimeContext context{};
    context.fields = {
        ActiveRowRuntimeField{
            0x00U,
            4U,
            "localModelObjectSlotTable",
            "Runtime pointer written by SST command type 0; not on-disk SML/SST data.",
        },
        ActiveRowRuntimeField{
            0x04U,
            4U,
            "loadedMldResourceRecord",
            "Runtime pointer to the same-index loaded MLD resource-list record.",
        },
        ActiveRowRuntimeField{
            0x08U,
            4U,
            "localRuntimePointerTable",
            "Runtime pointer table appended after the type 0 local slot table.",
        },
        ActiveRowRuntimeField{
            0x0CU,
            1U,
            "localSlotCount",
            "Runtime local slot count copied from the loaded MLD header entry count.",
        },
        ActiveRowRuntimeField{
            0x10U,
            4U,
            "secondaryModelEffectRuntimeBuffer",
            "Runtime-only secondary model/effect buffer pointer written by type 0 setup and read by type 11.",
        },
    };
    return context;
}

std::optional<std::uint32_t> localSlotCountForRecord(const SmlParseResult& sml, std::size_t topLevelRecordIndex) {
    if (topLevelRecordIndex >= sml.records.size()) {
        return std::nullopt;
    }
    const auto& record = sml.records[topLevelRecordIndex];
    if (!record.embeddedMldSummary.has_value() || !record.embeddedMldSummary->validLookingHeader ||
        !record.embeddedMldSummary->entryCount.has_value()) {
        return std::nullopt;
    }
    return record.embeddedMldSummary->entryCount;
}

} // namespace

bool BattleStageParseResult::ok() const {
    const bool ownErrors = std::none_of(diagnostics.begin(), diagnostics.end(), [](const ParseDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
    return ownErrors && sml.ok() && sst.ok();
}

BattleStageParseResult BattleStageParser::parsePair(std::span<const std::uint8_t> smlBytes,
    std::span<const std::uint8_t> sstBytes,
    std::string stem) {
    BattleStageParseResult result{};
    result.stem = std::move(stem);
    result.sml = SmlParser::parse(smlBytes);
    result.sst = SstParser::parse(sstBytes);
    result.activeRowRuntimeContext = makeActiveRowRuntimeContext();
    result.recordCountsAgree = result.sml.recordCount == result.sst.recordCount;
    if (!result.recordCountsAgree) {
        addDiagnostic(result.diagnostics,
            DiagnosticSeverity::Error,
            "SML and SST top-level record counts do not agree");
    }

    std::map<std::int16_t, std::uint32_t> histogram;
    for (const auto& block : result.sst.commandBlocks) {
        for (const auto& command : block.commands) {
            ++histogram[command.type];

            if (!command.modelIndexCandidate || !command.modelIndex.has_value()) {
                continue;
            }

            ResolvedLocalObjectSlotLink link{};
            link.topLevelRecordIndex = block.topLevelRecordIndex;
            link.commandIndex = command.index;
            link.commandType = command.type;
            link.localSlotIndex = *command.modelIndex;
            if (block.topLevelRecordIndex < result.sml.records.size()) {
                link.owningSmlRecordIndex = block.topLevelRecordIndex;
            }
            link.localSlotCount = localSlotCountForRecord(result.sml, block.topLevelRecordIndex);
            link.slotIndexRangeKnown = link.localSlotCount.has_value();
            if (link.slotIndexRangeKnown) {
                link.slotIndexInRange = link.localSlotIndex >= 0 &&
                    static_cast<std::uint32_t>(link.localSlotIndex) < *link.localSlotCount;
            }
            if (link.slotIndexRangeKnown && !link.slotIndexInRange) {
                addDiagnostic(result.diagnostics,
                    DiagnosticSeverity::Warning,
                    "SST command model-index candidate is outside the same-record local object slot range",
                    command.payloadOffset);
            }
            result.localObjectSlotLinks.push_back(link);
        }
    }

    result.commandTypeHistogram.assign(histogram.begin(), histogram.end());
    return result;
}

} // namespace spice::sstsml
