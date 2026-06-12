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

            ResolvedCommandLink link{};
            link.topLevelRecordIndex = block.topLevelRecordIndex;
            link.commandIndex = command.index;
            link.commandType = command.type;
            link.modelIndex = *command.modelIndex;
            if (link.modelIndex >= 0 &&
                static_cast<std::size_t>(link.modelIndex) < result.sml.records.size()) {
                link.resolved = true;
                link.smlRecordIndex = static_cast<std::size_t>(link.modelIndex);
            } else {
                addDiagnostic(result.diagnostics,
                    DiagnosticSeverity::Warning,
                    "SST command model-index candidate does not resolve to an SML record",
                    command.payloadOffset);
            }
            result.commandLinks.push_back(link);
        }
    }

    result.commandTypeHistogram.assign(histogram.begin(), histogram.end());
    return result;
}

} // namespace spice::sstsml
